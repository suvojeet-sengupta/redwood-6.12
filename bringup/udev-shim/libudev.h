/* SPDX-License-Identifier: GPL-2.0 */
/* The few libudev calls rmtfs makes, answered from sysfs. */
#ifndef UDEV_SHIM_H
#define UDEV_SHIM_H

#include <sys/types.h>

struct udev;
struct udev_device;

struct udev *udev_new(void);
struct udev *udev_unref(struct udev *udev);
struct udev_device *udev_device_new_from_devnum(struct udev *udev, char type, dev_t devnum);
const char *udev_device_get_sysattr_value(struct udev_device *dev, const char *sysattr);
struct udev_device *udev_device_unref(struct udev_device *dev);

#endif
