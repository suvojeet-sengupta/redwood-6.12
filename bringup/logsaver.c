// /init wrapper for testing Android on 6.12: fork a child that keeps the
// kernel log (which also carries init's own messages) on the logdump
// partition, then exec Android's first stage init as PID 1.
#define _GNU_SOURCE
#include <dirent.h>
#include <poll.h>
#include <sched.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define BUF_MAX (16 << 20)

static int find_logdump(void)
{
	DIR *d = opendir("/ls/sys/class/block");
	struct dirent *e;
	char path[512], buf[1024];
	int fd = -1;

	if (!d)
		return -1;
	while (fd < 0 && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/ls/sys/class/block/%s/uevent", e->d_name);
		int u = open(path, O_RDONLY | O_CLOEXEC);
		ssize_t n = u >= 0 ? read(u, buf, sizeof(buf) - 1) : -1;
		if (u >= 0)
			close(u);
		if (n <= 0)
			continue;
		buf[n] = 0;
		if (!strstr(buf, "PARTNAME=logdump\n"))
			continue;
		unsigned int maj, min;
		snprintf(path, sizeof(path), "/ls/sys/class/block/%s/dev", e->d_name);
		u = open(path, O_RDONLY | O_CLOEXEC);
		n = u >= 0 ? read(u, buf, sizeof(buf) - 1) : -1;
		if (u >= 0)
			close(u);
		if (n <= 0)
			continue;
		buf[n] = 0;
		if (sscanf(buf, "%u:%u", &maj, &min) != 2)
			continue;
		unlink("/ls/logdump");
		mknod("/ls/logdump", S_IFBLK | 0600, makedev(maj, min));
		fd = open("/ls/logdump", O_WRONLY | O_CLOEXEC);
	}
	closedir(d);
	return fd;
}

/*
 * The 5.4 vendor allocates through ION, which 6.12 does not have. Once
 * Android's init has /vendor mounted, bind mount a libion that uses the
 * DMA-BUF heaps over the vendor's, in init's mount namespace. It only
 * lives in RAM; the vendor partition stays as it is.
 */
static int shim_fd[3] = { -1, -1, -1 };
/* fstab.test mounts /data read-only, so a broken key setup can't write to it */
static const char *shim_src[3] = { "/ionshim64.so", "/ionshim32.so", "/fstab.test" };
static const char *shim_tmp[3] = { "/dev/.ionshim64.so", "/dev/.ionshim32.so", "/dev/.fstab.test" };
static const char *shim_dst[3] = { "/vendor/lib64/libion.so", "/vendor/lib/libion.so", "/vendor/etc/fstab.default" };

static int place_shims(int wfd)
{
	pid_t pid;
	int st;

	if (access("/ls/proc/1/root/vendor/lib64/libion.so", F_OK))
		return 0;
	pid = fork();
	if (pid == 0) {
		int ns = open("/ls/proc/1/ns/mnt", O_RDONLY | O_CLOEXEC);
		int root = open("/ls/proc/1/root", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		int i;

		if (ns < 0 || root < 0 || setns(ns, CLONE_NEWNS) || fchdir(root) || chroot(".")) {
			dprintf(wfd, "<3>logsaver: can't enter init's mounts: %d\n", errno);
			_exit(1);
		}
		for (i = 0; i < 3; i++) {
			char b[65536];
			ssize_t n;
			off_t off = 0;
			int out = open(shim_tmp[i], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

			if (out < 0 || shim_fd[i] < 0) {
				if (i == 2)
					goto unsafe;
				continue;
			}
			while ((n = pread(shim_fd[i], b, sizeof(b), off)) > 0) {
				if (write(out, b, n) != n)
					break;
				off += n;
			}
			close(out);
			if (mount(shim_tmp[i], shim_dst[i], NULL, MS_BIND, NULL)) {
				dprintf(wfd, "<3>logsaver: bind %s failed: %d\n", shim_dst[i], errno);
				if (i == 2)
					goto unsafe;
			} else
				dprintf(wfd, "<6>logsaver: %s on %s\n", shim_src[i], shim_dst[i]);
		}
		/* the vendor's ueventd.rc only opens up the AOSP heaps */
		{
			static const char rule[] =
				"\n/dev/dma_heap/qcom,*      0444   system     system\n";
			static char rc[262144];
			int in = open("/vendor/etc/ueventd.rc", O_RDONLY | O_CLOEXEC);
			ssize_t n = in >= 0 ? read(in, rc, sizeof(rc) - sizeof(rule)) : -1;
			int out;

			if (in >= 0)
				close(in);
			out = n > 0 ? open("/dev/.ueventd.rc", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;
			if (out >= 0) {
				memcpy(rc + n, rule, sizeof(rule) - 1);
				n += sizeof(rule) - 1;
				if (write(out, rc, n) == n &&
				    !mount("/dev/.ueventd.rc", "/vendor/etc/ueventd.rc", NULL, MS_BIND, NULL))
					dprintf(wfd, "<6>logsaver: ueventd.rc with the qcom heaps\n");
				else
					dprintf(wfd, "<3>logsaver: ueventd.rc bind failed: %d\n", errno);
				close(out);
			}
		}
		_exit(0);
unsafe:
		/* never let /data get mounted writable during these tests */
		dprintf(wfd, "<3>logsaver: no read-only fstab, rebooting to the bootloader\n");
		sync();
		syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
			LINUX_REBOOT_CMD_RESTART2, "bootloader");
		_exit(1);
	}
	if (pid < 0)
		return 0;
	waitpid(pid, &st, 0);
	return 1;
}

static void saver(void)
{
	static char buf[BUF_MAX];
	size_t used;
	int kfd, wfd, out = -1, i;

	/* keep these mounts out of init's way: first stage init moves every
	 * mount it sees into the new root and dies if one of them fails */
	unshare(CLONE_NEWNS);
	mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
	mkdir("/ls", 0700);
	mkdir("/ls/sys", 0700);
	mount("sysfs", "/ls/sys", "sysfs", 0, NULL);
	mkdir("/ls/proc", 0700);
	mount("proc", "/ls/proc", "proc", 0, NULL);
	mknod("/ls/kmsg", S_IFCHR | 0600, makedev(1, 11));
	kfd = open("/ls/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	wfd = open("/ls/kmsg", O_WRONLY | O_CLOEXEC);
	if (kfd < 0)
		return;
	used = snprintf(buf, sizeof(buf), "REDWOOD612ANDROID\n");
	shim_fd[0] = open(shim_src[0], O_RDONLY | O_CLOEXEC);
	shim_fd[1] = open(shim_src[1], O_RDONLY | O_CLOEXEC);
	shim_fd[2] = open(shim_src[2], O_RDONLY | O_CLOEXEC);
	int shimmed = 0;

	/* write new kernel log as soon as it shows up, so that the last
	 * messages before a reboot make it out too */
	size_t done = 0;
	struct timespec last = {0}, now;
	struct pollfd p = { .fd = kfd, .events = POLLIN };

	for (i = 0; i < 600;) {	/* about 20 minutes */
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec - last.tv_sec >= 2) {
			last = now;
			/* a heartbeat shows whether the kernel is still alive,
			 * and stack dumps show where things are stuck */
			dprintf(wfd, "<6>logsaver: tick %d\n", i);
			if (i == 15 || i == 45) {
				int t = open("/ls/proc/sysrq-trigger", O_WRONLY | O_CLOEXEC);
				if (t >= 0) {
					if (write(t, "w", 1) < 0 || write(t, "l", 1) < 0)
						;
					close(t);
				}
			}
			i++;
		}
		for (;;) {
			ssize_t n = read(kfd, buf + used, sizeof(buf) - used - 1);
			if (n < 0) {
				if (errno == EPIPE)
					continue;
				break;
			}
			used += n;
			if (used >= sizeof(buf) - 8192)
				break;
		}
		if (out < 0)
			out = find_logdump();
		if (out >= 0 && used > done) {
			buf[used] = 0;
			if (pwrite(out, buf + done, used + 1 - done, done) > 0)
				done = used;
			fdatasync(out);
		}
		if (!shimmed)
			shimmed = place_shims(wfd);
		poll(&p, 1, shimmed ? 50 : 10);
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	if (fork() == 0) {
		setsid();
		saver();
		_exit(0);
	}
	execv("/init.android", argv);
	return 127;
}
