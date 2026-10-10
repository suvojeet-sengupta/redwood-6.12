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
		poll(&p, 1, 50);
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
