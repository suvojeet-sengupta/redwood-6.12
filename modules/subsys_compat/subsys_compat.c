// SPDX-License-Identifier: GPL-2.0
/*
 * The 5.4 subsystem (PIL) interface on top of remoteproc, for redwood's
 * 5.4 era vendor on 6.12:
 *
 *  - /sys/bus/msm_subsys/devices/subsysN with name and state, which
 *    libmdmdetect reads to find the modem (qcril gives up without it)
 *  - /dev/subsys_<name>: opening it boots the processor, which is how
 *    per_mgr brings the modem up; it keeps running after the last close
 *  - the devices sit where 5.4 had them, /sys/devices/platform/soc/
 *    4080000.qcom,mss/subsys3 and so on, because the vendor's SELinux
 *    policy labels their name files by those paths (vendor_sysfs_ssr);
 *    anywhere else libmdmdetect is denied reading them
 *  - the ADSP and CDSP are booted once their firmware can be loaded, as
 *    the 5.4 loaders did, and every processor recovers from a crash
 *    instead of taking the phone down
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

struct subsys {
	const char *name;	/* the 5.4 name */
	const char *compat;	/* the remoteproc node */
	const char *node;	/* the 5.4 PIL device it sat under */
	int idx;		/* its 5.4 subsysN */
	struct device *parent;
	bool autoboot;
	struct rproc *rproc;
	struct device dev;
	struct cdev cdev;
	struct mutex lock;
	int users;
	bool booted;		/* booted by autoboot, held for good */
	int tries;
};

static struct subsys subsystems[] = {
	{ .name = "modem", .compat = "qcom,yupik-modem-pas", .node = "4080000.qcom,mss", .idx = 3 },
	{ .name = "adsp", .compat = "qcom,yupik-adsp-pas", .node = "17300000.qcom,lpass", .idx = 0,
	  .autoboot = true },
	{ .name = "cdsp", .compat = "qcom,yupik-cdsp-pas", .node = "98900000.qcom,turing", .idx = 1,
	  .autoboot = true },
	{ .name = "wpss", .compat = "qcom,yupik-wpss-pas", .node = "soc:qcom,wpss@8a00000", .idx = 2 },
};

static dev_t devt;
static struct class *subsys_class;
static struct delayed_work boot_work;

static struct bus_type subsys_bus = {
	.name = "msm_subsys",
};

static struct rproc *subsys_rproc(struct subsys *s)
{
	struct device_node *np;

	if (s->rproc)
		return s->rproc;
	np = of_find_compatible_node(NULL, NULL, s->compat);
	if (!np)
		return NULL;
	s->rproc = rproc_get_by_phandle(np->phandle);
	of_node_put(np);
	if (s->rproc)
		s->rproc->recovery_disabled = false;
	return s->rproc;
}

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct subsys *s = container_of(dev, struct subsys, dev);

	return sysfs_emit(buf, "%s\n", s->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct subsys *s = container_of(dev, struct subsys, dev);
	struct rproc *rproc = subsys_rproc(s);
	bool up = rproc && (rproc->state == RPROC_RUNNING ||
			    rproc->state == RPROC_CRASHED);

	return sysfs_emit(buf, "%s\n", up ? "ONLINE" : "OFFLINE");
}
static DEVICE_ATTR_RO(state);

static ssize_t restart_level_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "RELATED\n");
}

static ssize_t restart_level_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	/* recovery is always on; accept what the vendor scripts write */
	return count;
}
static DEVICE_ATTR_RW(restart_level);

static struct attribute *subsys_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_state.attr,
	&dev_attr_restart_level.attr,
	NULL,
};
ATTRIBUTE_GROUPS(subsys);

static int subsys_open(struct inode *inode, struct file *file)
{
	struct subsys *s = container_of(inode->i_cdev, struct subsys, cdev);
	struct rproc *rproc = subsys_rproc(s);
	int ret = 0;

	if (!rproc)
		return -ENODEV;
	mutex_lock(&s->lock);
	if (!s->users)
		ret = rproc_boot(rproc);
	if (!ret)
		s->users++;
	mutex_unlock(&s->lock);
	if (ret)
		pr_err("subsys_compat: booting %s failed: %d\n", s->name, ret);
	file->private_data = s;
	return ret;
}

static int subsys_release(struct inode *inode, struct file *file)
{
	struct subsys *s = file->private_data;

	/*
	 * Keep the processor running when the last user goes away: per_mgr
	 * and pm-proxy reopen the node as they restart, and shutting the
	 * modem down in between looks like a crash to everything else.
	 */
	mutex_lock(&s->lock);
	s->users--;
	mutex_unlock(&s->lock);
	return 0;
}

static const struct file_operations subsys_fops = {
	.owner = THIS_MODULE,
	.open = subsys_open,
	.release = subsys_release,
	.llseek = noop_llseek,
};

static void subsys_dev_release(struct device *dev)
{
}

/* boot the processors the 5.4 loaders brought up on their own */
static void subsys_boot_work(struct work_struct *work)
{
	bool again = false;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		struct subsys *s = &subsystems[i];
		struct rproc *rproc;

		/*
		 * Look up the ones nothing boots through here too (the WPSS is
		 * started by icnss2): finding the processor turns its recovery
		 * on, and with it off a firmware crash panics the phone
		 */
		if (!s->autoboot) {
			again |= !subsys_rproc(s) && ++s->tries <= 60;
			continue;
		}
		if (s->booted)
			continue;
		rproc = subsys_rproc(s);
		if (!rproc || ++s->tries > 60) {
			again |= !rproc && s->tries <= 60;
			continue;
		}
		if (rproc->state == RPROC_RUNNING) {
			s->booted = true;
			continue;
		}
		ret = rproc_boot(rproc);
		if (ret) {
			again = true;
			continue;
		}
		s->booted = true;
		pr_info("subsys_compat: %s booted\n", s->name);
	}
	if (again)
		schedule_delayed_work(&boot_work, 5 * HZ);
}

static void parent_release(struct device *dev)
{
	kfree(dev);
}

/* an empty device with the 5.4 PIL device's name, under the soc device */
static struct device *make_parent(struct device *soc, const char *name)
{
	struct device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return NULL;
	device_initialize(dev);
	dev->parent = soc;
	dev->release = parent_release;
	dev_set_name(dev, "%s", name);
	if (device_add(dev)) {
		put_device(dev);
		return NULL;
	}
	return dev;
}

static int __init subsys_compat_init(void)
{
	struct device_node *np = of_find_node_by_path("/soc");
	struct platform_device *soc = np ? of_find_device_by_node(np) : NULL;
	int i, ret;

	of_node_put(np);

	ret = bus_register(&subsys_bus);
	if (ret)
		return ret;
	ret = alloc_chrdev_region(&devt, 0, ARRAY_SIZE(subsystems), "subsys");
	if (ret)
		goto err_bus;
	subsys_class = class_create("subsys");
	if (IS_ERR(subsys_class)) {
		ret = PTR_ERR(subsys_class);
		goto err_chrdev;
	}

	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		struct subsys *s = &subsystems[i];

		mutex_init(&s->lock);
		if (soc)
			s->parent = make_parent(&soc->dev, s->node);
		s->dev.parent = s->parent;
		s->dev.bus = &subsys_bus;
		s->dev.groups = subsys_groups;
		s->dev.release = subsys_dev_release;
		dev_set_name(&s->dev, "subsys%d", s->idx);
		ret = device_register(&s->dev);
		if (ret) {
			put_device(&s->dev);
			continue;
		}
		cdev_init(&s->cdev, &subsys_fops);
		s->cdev.owner = THIS_MODULE;
		if (cdev_add(&s->cdev, MKDEV(MAJOR(devt), i), 1))
			continue;
		device_create(subsys_class, &s->dev, MKDEV(MAJOR(devt), i), NULL,
			      "subsys_%s", s->name);
	}

	if (soc)
		put_device(&soc->dev);
	INIT_DELAYED_WORK(&boot_work, subsys_boot_work);
	schedule_delayed_work(&boot_work, 10 * HZ);
	return 0;

err_chrdev:
	unregister_chrdev_region(devt, ARRAY_SIZE(subsystems));
err_bus:
	bus_unregister(&subsys_bus);
	return ret;
}

static void __exit subsys_compat_exit(void)
{
	int i;

	cancel_delayed_work_sync(&boot_work);
	for (i = 0; i < ARRAY_SIZE(subsystems); i++) {
		struct subsys *s = &subsystems[i];

		device_destroy(subsys_class, MKDEV(MAJOR(devt), i));
		cdev_del(&s->cdev);
		if (device_is_registered(&s->dev))
			device_unregister(&s->dev);
		if (s->rproc)
			rproc_put(s->rproc);
		if (s->parent)
			device_unregister(s->parent);
	}
	class_destroy(subsys_class);
	unregister_chrdev_region(devt, ARRAY_SIZE(subsystems));
	bus_unregister(&subsys_bus);
}

module_init(subsys_compat_init);
module_exit(subsys_compat_exit);
MODULE_DESCRIPTION("5.4 subsystem interface on top of remoteproc");
MODULE_LICENSE("GPL");
