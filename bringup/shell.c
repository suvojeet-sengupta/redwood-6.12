// Bring-up init for 6.12 on redwood with a shell over USB. Loads the modules,
// starts the ADSP, sets up a USB ACM + NCM gadget with the role forced to
// device, and serves a toybox shell on TCP port 2323 (192.168.90.1, or the
// IPv6 link-local address fe80::ff:fe61:2001). It keeps running; `rb` in the
// shell reboots to the bootloader.
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
#include <sys/wait.h>
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


static char logdev[128];

static void snapshot(const char *why)
{
	klog("snapshot: %s\n", why);
	if (logdev[0])
		save_log(logdev);
}

static int put(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int ret = -1;

	if (fd >= 0) {
		ret = write(fd, val, strlen(val)) < 0 ? -1 : 0;
		close(fd);
	}
	klog("%s <- %s: %s\n", path, val, ret ? strerror(errno) : "ok");
	return ret;
}

static void start_adsp(void)
{
	DIR *d = opendir("/sys/class/remoteproc");
	struct dirent *e;
	char path[256], name[128];

	if (!d) {
		klog("no remoteprocs\n");
		return;
	}
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/remoteproc/%s/name", e->d_name);
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		ssize_t n = fd >= 0 ? read(fd, name, sizeof(name) - 1) : -1;
		if (fd >= 0)
			close(fd);
		if (n <= 0)
			continue;
		name[n] = 0;
		name[strcspn(name, "\n")] = 0;
		klog("remoteproc %s is %s\n", e->d_name, name);
		if (strstr(name, "adsp")) {
			snprintf(path, sizeof(path), "/sys/class/remoteproc/%s/state", e->d_name);
			put(path, "start");
		}
	}
	closedir(d);
}


/* Log a sysfs file's contents, or that it is missing. */
static void show(const char *path)
{
	char buf[256];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;

	if (fd >= 0)
		close(fd);
	if (n < 0) {
		klog("%s: %s\n", path, strerror(errno));
		return;
	}
	buf[n] = 0;
	buf[strcspn(buf, "\n")] = 0;
	klog("%s = %s\n", path, buf);
}

static void show_usb_state(void)
{
	DIR *d;
	struct dirent *e;
	char path[256];

	d = opendir("/sys/class/typec");
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		klog("typec: %s\n", e->d_name);
		snprintf(path, sizeof(path), "/sys/class/typec/%s/data_role", e->d_name);
		show(path);
		snprintf(path, sizeof(path), "/sys/class/typec/%s/power_role", e->d_name);
		show(path);
	}
	if (d)
		closedir(d);
	else
		klog("no /sys/class/typec\n");
	d = opendir("/sys/class/usb_role");
	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/usb_role/%s/role", e->d_name);
		show(path);
	}
	if (d)
		closedir(d);
}

static void force_device_role(void)
{
	DIR *d = opendir("/sys/class/usb_role");
	struct dirent *e;
	char path[256];

	while (d && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/usb_role/%s/role", e->d_name);
		put(path, "device");
	}
	if (d)
		closedir(d);
}

/* Run a command and wait for it */
static void run(char *const argv[])
{
	pid_t pid = fork();

	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

/* Start a command in the background */
static void spawn(char *const argv[])
{
	if (fork() == 0) {
		setsid();
		execv(argv[0], argv);
		_exit(127);
	}
}

#define G "/sys/kernel/config/usb_gadget/g1"

static void setup_gadget(void)
{
	DIR *d;
	struct dirent *e;
	char udc[128] = "";
	int i;

	mkdir("/sys/kernel/config", 0755);
	mount("configfs", "/sys/kernel/config", "configfs", 0, NULL);
	mkdir(G, 0755);
	put(G "/idVendor", "0x18d1");
	put(G "/idProduct", "0x4e40");
	mkdir(G "/strings/0x409", 0755);
	put(G "/strings/0x409/serialnumber", "redwood612");
	put(G "/strings/0x409/product", "redwood 6.12 bring-up");
	mkdir(G "/functions/acm.gs0", 0755);
	mkdir(G "/functions/ncm.usb0", 0755);
	put(G "/functions/ncm.usb0/dev_addr", "02:00:00:61:20:01");
	put(G "/functions/ncm.usb0/host_addr", "02:00:00:61:20:02");
	mkdir(G "/configs/c.1", 0755);
	symlink(G "/functions/acm.gs0", G "/configs/c.1/acm.gs0");
	symlink(G "/functions/ncm.usb0", G "/configs/c.1/ncm.usb0");
	for (i = 0; i < 20 && !udc[0]; i++) {
		d = opendir("/sys/class/udc");
		while (d && (e = readdir(d)))
			if (e->d_name[0] != '.')
				snprintf(udc, sizeof(udc), "%s", e->d_name);
		if (d)
			closedir(d);
		if (!udc[0])
			sleep(1);
	}
	if (!udc[0]) {
		klog("no UDC\n");
		return;
	}
	put(G "/UDC", udc);
}

static const char *slot_suffix(void)
{
	static char suffix[4] = "_a";
	char buf[4096], *p;
	FILE *f = fopen("/proc/cmdline", "r");

	if (!f)
		return suffix;
	if (fgets(buf, sizeof(buf), f) && (p = strstr(buf, "androidboot.slot_suffix=")))
		snprintf(suffix, sizeof(suffix), "%.2s", p + strlen("androidboot.slot_suffix="));
	fclose(f);
	return suffix;
}

int main(void)
{
	struct utsname u;
	char dev[128], modem[16];
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
	load_list("modules.load");
	for (i = 0; i < 30 && !find_part("logdump", logdev, sizeof(logdev)); i++)
		sleep(1);
	snapshot("first stage modules loaded");

	/* The firmware lives on the booted slot's modem partition, FAT, as on 5.4 */
	snprintf(modem, sizeof(modem), "modem%s", slot_suffix());
	if (find_part(modem, dev, sizeof(dev))) {
		mkdir("/firmware", 0755);
		if (mount(dev, "/firmware", "vfat", MS_RDONLY, "shortname=lower"))
			klog("mount %s: %s\n", dev, strerror(errno));
		else
			put("/sys/module/firmware_class/parameters/path", "/firmware/image");
	} else {
		klog("no %s\n", modem);
	}

	load_list("vendor_dlkm.modules.load");
	sleep(3);
	snapshot("vendor_dlkm modules loaded");

	start_adsp();
	sleep(15);
	snapshot("adsp started");

	setup_gadget();
	sleep(3);
	force_device_role();
	sleep(3);
	show_usb_state();

	setenv("PATH", "/bin", 1);
	setenv("HOME", "/", 1);
	setenv("ENV", "/etc/shrc", 1);
	mkdir("/tmp", 01777);
	mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
	mkdir("/sys/kernel/debug", 0755);
	mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL);
	mkdir("/dev/pts", 0755);
	mount("devpts", "/dev/pts", "devpts", 0, NULL);

	char *ifup[] = { "/bin/toybox", "ifconfig", "usb0", "192.168.90.1",
			 "netmask", "255.255.255.0", "up", NULL };
	char *loup[] = { "/bin/toybox", "ifconfig", "lo", "127.0.0.1", "up", NULL };
	run(loup);
	run(ifup);
	char *shell4[] = { "/bin/toybox", "nc", "-L", "-p", "2323", "/bin/sh", "-i", NULL };
	char *shell6[] = { "/bin/toybox", "nc", "-6", "-L", "-p", "2323", "/bin/sh", "-i", NULL };
	spawn(shell4);
	spawn(shell6);
	klog("shell on 192.168.90.1:2323 and [fe80::ff:fe61:2001]:2323\n");
	snapshot("shell up");

	/* Keep the latest log on logdump while the shell runs */
	for (;;) {
		sleep(30);
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		snapshot("periodic");
	}
	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_RESTART2, "bootloader");
	for (;;)
		pause();
}
