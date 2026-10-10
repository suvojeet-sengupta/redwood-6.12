// Print touch events from an evdev node: finger contacts and X/Y.
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct input_event ev;
	int x = -1, y = -1, n = 0;
	int fd = open(argc > 1 ? argv[1] : "/dev/input/event0", O_RDONLY);
	int limit = argc > 2 ? atoi(argv[2]) : 40;

	if (fd < 0) {
		perror("open");
		return 1;
	}
	while (n < limit && read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
		if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_X)
			x = ev.value;
		else if (ev.type == EV_ABS && ev.code == ABS_MT_POSITION_Y)
			y = ev.value;
		else if (ev.type == EV_KEY && ev.code == BTN_TOUCH)
			printf("touch %s\n", ev.value ? "down" : "up");
		else if (ev.type == EV_SYN && ev.code == SYN_REPORT && x >= 0) {
			printf("x=%d y=%d\n", x, y);
			fflush(stdout);
			n++;
		}
	}
	return 0;
}
