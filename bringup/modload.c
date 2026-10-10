// Bring-up init for 6.12 on redwood. Loads the first stage modules the way
// Android's init does, dependencies first from modules.dep, saves the kernel
// log to the empty logdump partition and goes back to the bootloader.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <linux/reboot.h>
#include <unistd.h>

#define MAX_MODS 1024

static char dir[256];
static struct mod {
	char *name;		/* path relative to dir, as in modules.dep */
	char *deps;		/* the rest of its modules.dep line */
	int state;		/* 0 not tried, 1 loading, 2 loaded, 3 failed */
} mods[MAX_MODS];
static int nmods, nok, nbad;

static void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void klog(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int fd, n = snprintf(buf, sizeof(buf), "<6>modload: ");

	va_start(ap, fmt);
	n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
	va_end(ap);
	if (n >= (int)sizeof(buf))
		n = sizeof(buf) - 1;
	fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		write(fd, buf, n);
		close(fd);
	}
}

static void read_deps(void)
{
	char path[512], line[8192];
	FILE *f;

	snprintf(path, sizeof(path), "%s/modules.dep", dir);
	f = fopen(path, "r");
	if (!f) {
		klog("no modules.dep\n");
		return;
	}
	while (nmods < MAX_MODS && fgets(line, sizeof(line), f)) {
		char *colon = strchr(line, ':');

		if (!colon)
			continue;
		*colon = 0;
		line[strlen(line)] = 0;
		colon[1 + strcspn(colon + 1, "\n")] = 0;
		mods[nmods].name = strdup(line);
		mods[nmods].deps = strdup(colon + 1);
		nmods++;
	}
	fclose(f);
}

static struct mod *find(const char *name)
{
	int i;

	for (i = 0; i < nmods; i++)
		if (!strcmp(mods[i].name, name))
			return &mods[i];
	return NULL;
}

static int load(const char *name)
{
	struct mod *m = find(name);
	char path[512], *deps, *tok, *save;
	int ret = 0;

	if (!m) {
		klog("%s is not in modules.dep\n", name);
		return -1;
	}
	if (m->state == 2)
		return 0;
	if (m->state == 1 || m->state == 3)
		return -1;
	m->state = 1;

	/* modules.dep lists every module this one needs, directly or not */
	deps = strdup(m->deps);
	for (tok = strtok_r(deps, " ", &save); tok; tok = strtok_r(NULL, " ", &save))
		if (load(tok))
			ret = -1;
	free(deps);

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || (syscall(SYS_finit_module, fd, "", 0) && errno != EEXIST)) {
		klog("load %s failed: %s\n", name, strerror(errno));
		ret = -1;
	}
	if (fd >= 0)
		close(fd);
	m->state = ret ? 3 : 2;
	if (ret)
		nbad++;
	else
		nok++;
	return ret;
}

static void load_list(const char *list)
{
	char path[512], line[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, list);
	f = fopen(path, "r");
	if (!f) {
		klog("no %s\n", list);
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0])
			load(line);
	}
	fclose(f);
	klog("%s done: %d loaded, %d failed\n", list, nok, nbad);
}

static int find_part(const char *name, char *dev, size_t len)
{
	DIR *d = opendir("/sys/class/block");
	struct dirent *e;
	char path[512], buf[1024], want[128];
	int found = 0;

	if (!d)
		return 0;
	snprintf(want, sizeof(want), "PARTNAME=%s\n", name);
	while (!found && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", e->d_name);
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		close(fd);
		if (n > 0) {
			buf[n] = 0;
			if (strstr(buf, want)) {
				/* This GKI has no devtmpfs, so make the node from
				 * the major:minor sysfs reports. */
				unsigned int maj, min;
				snprintf(path, sizeof(path), "/sys/class/block/%s/dev", e->d_name);
				fd = open(path, O_RDONLY | O_CLOEXEC);
				n = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;
				if (fd >= 0)
					close(fd);
				if (n > 0) {
					buf[n] = 0;
					if (sscanf(buf, "%u:%u", &maj, &min) == 2) {
						snprintf(dev, len, "/dev/%s", name);
						unlink(dev);
						mknod(dev, S_IFBLK | 0600, makedev(maj, min));
						found = 1;
					}
				}
			}
		}
	}
	closedir(d);
	return found;
}

static void save_log(const char *dev)
{
	static char out[8 << 20];
	size_t used = snprintf(out, sizeof(out), "REDWOOD612LOG\n");
	int k = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	char rec[8192];

	if (k < 0)
		return;
	for (;;) {
		ssize_t n = read(k, rec, sizeof(rec));
		if (n < 0) {
			if (errno == EPIPE)
				continue;
			break;
		}
		if (used + n + 1 >= sizeof(out))
			break;
		memcpy(out + used, rec, n);
		used += n;
	}
	close(k);
	out[used++] = 0;
	int fd = open(dev, O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		write(fd, out, used);
		fsync(fd);
		close(fd);
	}
}

int main(void)
{
	struct utsname u;
	char dev[128];
	int i;

	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mknod("/dev/kmsg", S_IFCHR | 0600, makedev(1, 11));
	mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3));
	klog("started\n");

	uname(&u);
	snprintf(dir, sizeof(dir), "/lib/modules/%s", u.release);
	read_deps();
	klog("%d modules in modules.dep\n", nmods);
	load_list("modules.load");

	for (i = 0; i < 30 && !find_part("logdump", dev, sizeof(dev)); i++)
		sleep(1);
	if (i < 30) {
		char back[16] = { 0 };

		klog("logdump is %s after %ds\n", dev, i);
		sleep(5);
		save_log(dev);
		int fd = open(dev, O_RDONLY | O_CLOEXEC | O_DIRECT);
		if (fd < 0)
			fd = open(dev, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			static char blk[4096] __attribute__((aligned(4096)));
			if (read(fd, blk, sizeof(blk)) > 0)
				memcpy(back, blk, 13);
			close(fd);
		}
		if (!strcmp(back, "REDWOOD612LOG")) {
			klog("log saved and read back\n");
			sync();
			syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
				LINUX_REBOOT_CMD_RESTART2, "bootloader");
		}
		klog("log did not read back\n");
	} else {
		klog("logdump never showed up\n");
	}

	/* Crash on purpose: after an unclean reset the bootloader saves the
	 * boot log to the minidump partition, which can be read from 5.4. */
	sync();
	int t = open("/proc/sysrq-trigger", O_WRONLY | O_CLOEXEC);
	if (t >= 0)
		write(t, "c", 1);
	for (;;)
		pause();
}
