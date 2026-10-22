// /init wrapper for testing Android on 6.12: fork a child that puts the
// test shims over /vendor once first stage init has mounted it, then exec
// Android's first stage init as PID 1. modules/bootlog keeps the log.
#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/*
 * The 5.4 vendor allocates through ION, which 6.12 does not have. Once
 * Android's init has /vendor mounted, bind mount a libion that uses the
 * DMA-BUF heaps over the vendor's, in init's mount namespace. It only
 * lives in RAM; the vendor partition stays as it is.
 */
static int shim_fd[4] = { -1, -1, -1, -1 };
/* fstab.test mounts /data read-only, so a broken key setup can't write to it */
/* media.sh picks the yupik v1 media profiles, which the missing video driver would report */
static const char *shim_src[4] = { "/ionshim64.so", "/ionshim32.so", "/fstab.test", "/media.sh" };
static const char *shim_tmp[4] = { "/dev/.ionshim64.so", "/dev/.ionshim32.so", "/dev/.fstab.test", "/dev/.media.sh" };
static const char *shim_dst[4] = { "/vendor/lib64/libion.so", "/vendor/lib/libion.so", "/vendor/etc/fstab.default", "/vendor/bin/init.qti.media.sh" };

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
		for (i = 0; i < 4; i++) {
			char b[65536];
			ssize_t n;
			off_t off = 0;
			int out = open(shim_tmp[i], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);

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
	int wfd, i;

	/* keep these mounts out of init's way: first stage init moves every
	 * mount it sees into the new root and dies if one of them fails */
	unshare(CLONE_NEWNS);
	mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
	mkdir("/ls", 0700);
	mkdir("/ls/proc", 0700);
	mount("proc", "/ls/proc", "proc", 0, NULL);
	mknod("/ls/kmsg", S_IFCHR | 0600, makedev(1, 11));
	wfd = open("/ls/kmsg", O_WRONLY | O_CLOEXEC);
	for (i = 0; i < 4; i++)
		shim_fd[i] = open(shim_src[i], O_RDONLY | O_CLOEXEC);

	dprintf(wfd, "<6>logsaver: waiting for /vendor\n");
	/* there is only about 100 ms between first stage init mounting /vendor
	 * and second stage init turning SELinux enforcing on, after which this
	 * process, still in the kernel domain, can't reach init's mounts */
	sched_setscheduler(0, SCHED_FIFO, &(struct sched_param){ .sched_priority = 99 });
	for (i = 0; i < 60000; i++) {
		if (place_shims(wfd))
			return;
		usleep(1000);
	}
	dprintf(wfd, "<3>logsaver: /vendor never showed up, no shims\n");
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
