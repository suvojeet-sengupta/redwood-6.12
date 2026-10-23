/*
 * bttest: power the WCN6750 BT core through /dev/btpower and send an HCI
 * Reset on /dev/ttyHS0
 *
 *   bttest [baud]
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <termios.h>
#include <unistd.h>

#define BT_CMD_PWR_CTRL		0xbfad
#define BT_CMD_GET_CHIPSET_ID	0xbfaf

static int major_of(const char *name)
{
	char line[128], n[64];
	int maj = -1, m, chr = 0;
	FILE *f = fopen("/proc/devices", "r");

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "Character", 9))
			chr = 1;
		else if (!strncmp(line, "Block", 5))
			chr = 0;
		else if (chr && sscanf(line, "%d %63s", &m, n) == 2 && !strcmp(n, name))
			maj = m;
	}
	fclose(f);
	return maj;
}

static int node(const char *path, const char *name)
{
	int maj = major_of(name);

	if (maj < 0) {
		fprintf(stderr, "%s not in /proc/devices\n", name);
		return -1;
	}
	unlink(path);
	if (mknod(path, S_IFCHR | 0600, makedev(maj, 0))) {
		perror(path);
		return -1;
	}
	return 0;
}

static speed_t speed(int baud)
{
	switch (baud) {
	case 3000000: return B3000000;
	case 921600: return B921600;
	default: return B115200;
	}
}

int main(int argc, char **argv)
{
	static const unsigned char reset[] = { 0x01, 0x03, 0x0c, 0x00 };
	static const unsigned char version[] = { 0x01, 0x01, 0x10, 0x00 };
	unsigned char buf[256];
	struct termios t;
	int bt, tty, n, baud = argc > 1 ? atoi(argv[1]) : 115200;
	char id[32] = { 0 };

	if (node("/dev/btpower", "bt") || node("/dev/ttyHS0", "ttyHS"))
		return 1;
	bt = open("/dev/btpower", O_RDWR);
	if (bt < 0) {
		perror("open btpower");
		return 1;
	}
	if (ioctl(bt, BT_CMD_PWR_CTRL, 1)) {
		perror("power on");
		return 1;
	}
	printf("powered on\n");
	if (!ioctl(bt, BT_CMD_GET_CHIPSET_ID, id))
		printf("chipset id: %s\n", id);
	usleep(100000);

	tty = open("/dev/ttyHS0", O_RDWR | O_NOCTTY);
	if (tty < 0) {
		perror("open ttyHS0");
		goto off;
	}
	tcgetattr(tty, &t);
	cfmakeraw(&t);
	t.c_cflag |= CRTSCTS | CLOCAL | CREAD;
	cfsetispeed(&t, speed(baud));
	cfsetospeed(&t, speed(baud));
	tcsetattr(tty, TCSANOW, &t);
	tcflush(tty, TCIOFLUSH);

	for (int c = 0; c < 2; c++) {
		const unsigned char *cmd = c ? version : reset;
		struct pollfd p = { tty, POLLIN, 0 };

		printf("%s: ", c ? "read local version" : "hci reset");
		if (write(tty, cmd, 4) != 4) {
			perror("write");
			break;
		}
		if (poll(&p, 1, 2000) <= 0) {
			printf("no reply\n");
			continue;
		}
		usleep(50000);
		n = read(tty, buf, sizeof(buf));
		for (int i = 0; i < n; i++)
			printf("%02x ", buf[i]);
		printf("\n");
	}
	close(tty);
off:
	ioctl(bt, BT_CMD_PWR_CTRL, 0);
	printf("powered off\n");
	return 0;
}
