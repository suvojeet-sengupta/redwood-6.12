// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include "libudev.h"

struct udev {
	int unused;
};

struct udev_device {
	char syspath[64];
	char value[256];
};

struct udev *udev_new(void)
{
	return calloc(1, sizeof(struct udev));
}

struct udev *udev_unref(struct udev *udev)
{
	free(udev);
	return NULL;
}

struct udev_device *udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum)
{
	struct udev_device *dev = calloc(1, sizeof(*dev));

	if (!dev)
		return NULL;
	snprintf(dev->syspath, sizeof(dev->syspath), "/sys/dev/%s/%u:%u",
		 type == 'b' ? "block" : "char", major(devnum), minor(devnum));
	return dev;
}

const char *udev_device_get_sysattr_value(struct udev_device *dev, const char *sysattr)
{
	char path[192];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dev->syspath, sysattr);
	f = fopen(path, "r");
	if (!f)
		return NULL;
	if (!fgets(dev->value, sizeof(dev->value), f)) {
		fclose(f);
		return NULL;
	}
	fclose(f);
	dev->value[strcspn(dev->value, "\n")] = 0;
	return dev->value;
}

struct udev_device *udev_device_unref(struct udev_device *dev)
{
	free(dev);
	return NULL;
}
