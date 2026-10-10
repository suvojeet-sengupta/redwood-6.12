// Bring-up init for testing 6.12 on redwood. Loads the first stage modules
// and the USB ones, turns the phone into a USB network (NCM) and serial (ACM)
// device, and serves the kernel log on TCP port 2323 over IPv6 link-local.
// After 20 minutes it reboots to the bootloader.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <linux/reboot.h>
#include <unistd.h>

static void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void klog(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int fd, n = snprintf(buf, sizeof(buf), "<6>usbcatcher: ");

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

static int put(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int ret = -1;

	if (fd >= 0) {
		ret = write(fd, val, strlen(val)) < 0 ? -1 : 0;
		close(fd);
	}
	if (ret)
		klog("write %s=%s: %s\n", path, val, strerror(errno));
	return ret;
}

static void load_list(const char *dir, const char *list)
{
	char path[512], line[256];
	int ok = 0, bad = 0;
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, list);
	f = fopen(path, "r");
	if (!f) {
		klog("no %s\n", path);
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0])
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, line);
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0 || syscall(SYS_finit_module, fd, "", 0) != 0) {
			if (errno != EEXIST) {
				klog("load %s failed: %s\n", line, strerror(errno));
				bad++;
			}
		} else {
			ok++;
		}
		if (fd >= 0)
			close(fd);
	}
	fclose(f);
	klog("%s: %d loaded, %d failed\n", list, ok, bad);
}

static int first_entry(const char *dir, char *out, size_t len)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int found = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(out, len, "%s", e->d_name);
		found = 1;
		break;
	}
	closedir(d);
	return found;
}

#define G "/sys/kernel/config/usb_gadget/g1"

static void setup_gadget(char *ifname, size_t len)
{
	char udc[128];
	int i;

	mkdir("/sys/kernel/config", 0755);
	mount("configfs", "/sys/kernel/config", "configfs", 0, NULL);
	mkdir(G, 0755);
	put(G "/idVendor", "0x18d1");
	put(G "/idProduct", "0x4e40");
	mkdir(G "/strings/0x409", 0755);
	put(G "/strings/0x409/serialnumber", "redwood612");
	put(G "/strings/0x409/manufacturer", "Xiaomi");
	put(G "/strings/0x409/product", "redwood 6.12 bring-up");
	mkdir(G "/functions/ncm.usb0", 0755);
	put(G "/functions/ncm.usb0/dev_addr", "02:00:00:61:20:01");
	put(G "/functions/ncm.usb0/host_addr", "02:00:00:61:20:02");
	mkdir(G "/functions/acm.gs0", 0755);
	mkdir(G "/configs/c.1", 0755);
	mkdir(G "/configs/c.1/strings/0x409", 0755);
	put(G "/configs/c.1/strings/0x409/configuration", "ncm+acm");
	symlink(G "/functions/ncm.usb0", G "/configs/c.1/ncm.usb0");
	symlink(G "/functions/acm.gs0", G "/configs/c.1/acm.gs0");

	for (i = 0; i < 60 && !first_entry("/sys/class/udc", udc, sizeof(udc)); i++)
		sleep(1);
	if (i == 60) {
		klog("no UDC showed up\n");
		return;
	}
	klog("binding to UDC %s\n", udc);
	put(G "/UDC", udc);

	int fd = open(G "/functions/ncm.usb0/ifname", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		ssize_t n = read(fd, ifname, len - 1);
		close(fd);
		if (n > 0) {
			ifname[n] = 0;
			ifname[strcspn(ifname, "\n")] = 0;
		}
	}
	klog("ncm interface %s\n", ifname);
}

static void iface_up(const char *name)
{
	int s = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	struct ifreq ifr = { 0 };

	if (s < 0)
		return;
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	if (!ioctl(s, SIOCGIFFLAGS, &ifr)) {
		ifr.ifr_flags |= IFF_UP;
		if (ioctl(s, SIOCSIFFLAGS, &ifr))
			klog("up %s: %s\n", name, strerror(errno));
	}
	close(s);
	snprintf(ifr.ifr_name, IFNAMSIZ, "lo");
	s = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s >= 0 && !ioctl(s, SIOCGIFFLAGS, &ifr)) {
		ifr.ifr_flags |= IFF_UP;
		ioctl(s, SIOCSIFFLAGS, &ifr);
	}
	if (s >= 0)
		close(s);
}

// Send the whole log so far, then follow it until the client leaves.
static void serve(int c, time_t deadline)
{
	int k = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	char rec[8192];

	if (k < 0)
		return;
	while (time(NULL) < deadline) {
		ssize_t n = read(k, rec, sizeof(rec));
		if (n > 0) {
			if (send(c, rec, n, MSG_NOSIGNAL) < 0)
				break;
			continue;
		}
		if (n < 0 && errno == EPIPE)
			continue;
		struct pollfd p[2] = { { k, POLLIN, 0 }, { c, POLLIN, 0 } };
		if (poll(p, 2, 1000) > 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR))) {
			char junk[64];
			if (recv(c, junk, sizeof(junk), MSG_DONTWAIT) <= 0)
				break;
		}
	}
	close(k);
}

int main(void)
{
	char dir[256], ifname[32] = "usb0";
	struct utsname u;
	time_t deadline;

	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	klog("started\n");

	uname(&u);
	snprintf(dir, sizeof(dir), "/lib/modules/%s", u.release);
	load_list(dir, "modules.load");
	load_list(dir, "usb.load");

	setup_gadget(ifname, sizeof(ifname));
	sleep(2);
	iface_up(ifname);

	deadline = time(NULL) + 20 * 60;
	int s = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
	int one = 1;
	struct sockaddr_in6 a = { .sin6_family = AF_INET6, .sin6_port = htons(2323),
				  .sin6_addr = IN6ADDR_ANY_INIT };

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 2)) {
		klog("listen: %s\n", strerror(errno));
	} else {
		klog("serving the log on port 2323\n");
		while (time(NULL) < deadline) {
			struct pollfd p = { s, POLLIN, 0 };
			if (poll(&p, 1, 1000) <= 0)
				continue;
			int c = accept4(s, NULL, NULL, SOCK_CLOEXEC);
			if (c >= 0) {
				serve(c, deadline);
				close(c);
			}
		}
	}

	sync();
	syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		LINUX_REBOOT_CMD_RESTART2, "bootloader");
	for (;;)
		pause();
}
