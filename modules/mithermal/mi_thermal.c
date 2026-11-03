// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi's /sys/class/thermal/thermal_message for redwood on 6.12.
 *
 * On 5.4, Xiaomi's patched thermal core registers it, and mi_thermald
 * talks to the thermal policy through its nodes: mostly plain values it
 * writes and reads back, cpu_limits, which caps a CPU's frequency, and
 * screen_state and usb_online, which it watches. 6.12's thermal core
 * doesn't export its class, so borrow it from a cooling device of our own.
 * cpu_limits becomes a cpufreq QoS maximum on the CPU's policy.
 */

#include <linux/atomic.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/thermal.h>

static char *board_sensor = "VIRTUAL-SENSOR";
module_param(board_sensor, charp, 0444);

static struct thermal_cooling_device *cdev;
static struct device *msg_dev;

#define INT_NODE(name)								\
static atomic_t name##_val = ATOMIC_INIT(0);					\
static ssize_t name##_show(struct device *d, struct device_attribute *a,	\
			   char *buf)						\
{										\
	return sysfs_emit(buf, "%d\n", atomic_read(&name##_val));		\
}										\
static ssize_t name##_store(struct device *d, struct device_attribute *a,	\
			    const char *buf, size_t len)			\
{										\
	int v;									\
										\
	if (kstrtoint(buf, 10, &v))						\
		return -EINVAL;							\
	atomic_set(&name##_val, v);						\
	return len;								\
}										\
static DEVICE_ATTR(name, 0664, name##_show, name##_store)

#define STR_NODE(name, mode)							\
static char name##_buf[128];							\
static DEFINE_MUTEX(name##_lock);						\
static ssize_t name##_show(struct device *d, struct device_attribute *a,	\
			   char *buf)						\
{										\
	ssize_t n;								\
										\
	mutex_lock(&name##_lock);						\
	n = sysfs_emit(buf, "%s", name##_buf);					\
	mutex_unlock(&name##_lock);						\
	return n;								\
}										\
static ssize_t name##_store(struct device *d, struct device_attribute *a,	\
			    const char *buf, size_t len)			\
{										\
	mutex_lock(&name##_lock);						\
	strscpy(name##_buf, buf, sizeof(name##_buf));				\
	mutex_unlock(&name##_lock);						\
	return len;								\
}										\
static DEVICE_ATTR(name, mode, name##_show, name##_store)

INT_NODE(sconfig);
INT_NODE(balance_mode);
INT_NODE(temp_state);
INT_NODE(charger_temp);
INT_NODE(modem_limit);
INT_NODE(market_download_limit);
INT_NODE(flash_state);
INT_NODE(wifi_limit);
INT_NODE(poor_modem_limit);
STR_NODE(boost, 0644);
STR_NODE(board_sensor_temp, 0664);
STR_NODE(board_sensor_second_temp, 0664);

static ssize_t board_sensor_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%s", board_sensor);
}
static DEVICE_ATTR(board_sensor, 0444, board_sensor_show, NULL);

/* nothing on 6.12 says when the panel turns off; report it on */
static ssize_t screen_state_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "1\n");
}
static DEVICE_ATTR(screen_state, 0444, screen_state_show, NULL);

static atomic_t usb_online = ATOMIC_INIT(0);
static ssize_t usb_online_show(struct device *d, struct device_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", atomic_read(&usb_online));
}
static DEVICE_ATTR(usb_online, 0444, usb_online_show, NULL);

/*
 * mi_thermald caps the big cores hard as soon as its virtual skin sensor
 * passes 35 C (down to 940 MHz on gold), which made the UI stutter in plain
 * use, often while charging. Its configs are encrypted, so keep its caps
 * from going below these floors instead; 0 leaves its cap as it is. The
 * SoC's own thermal limits and the charging limits are not affected.
 */
static unsigned int gold_limit_floor = 2400000;
module_param(gold_limit_floor, uint, 0644);
MODULE_PARM_DESC(gold_limit_floor, "Lowest cap mi_thermald may set on the gold CPUs, in kHz");
static unsigned int prime_limit_floor = 2400000;
module_param(prime_limit_floor, uint, 0644);
MODULE_PARM_DESC(prime_limit_floor, "Lowest cap mi_thermald may set on the prime CPU, in kHz");

/* "cpu<N> <max kHz>": cap the frequency of the policy CPU N belongs to */
static DEFINE_MUTEX(limits_lock);
static struct freq_qos_request limit_req[NR_CPUS];
static struct cpufreq_policy *limit_policy[NR_CPUS];

static ssize_t cpu_limits_show(struct device *d, struct device_attribute *a, char *buf)
{
	return 0;
}

static ssize_t cpu_limits_store(struct device *d, struct device_attribute *a,
				const char *buf, size_t len)
{
	unsigned int cpu, max;
	struct cpufreq_policy *policy;
	int ret = 0;

	if (sscanf(buf, "cpu%u %u", &cpu, &max) != 2 || cpu >= nr_cpu_ids)
		return -EINVAL;
	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;
	cpu = policy->cpu;
	if (cpu == 7)
		max = max(max, READ_ONCE(prime_limit_floor));
	else if (cpu >= 4)
		max = max(max, READ_ONCE(gold_limit_floor));
	mutex_lock(&limits_lock);
	if (!limit_policy[cpu]) {
		ret = freq_qos_add_request(&policy->constraints, &limit_req[cpu],
					   FREQ_QOS_MAX, max);
		if (ret >= 0) {
			limit_policy[cpu] = policy;
			policy = NULL;	/* the request keeps the reference */
		}
	} else {
		ret = freq_qos_update_request(&limit_req[cpu], max);
	}
	mutex_unlock(&limits_lock);
	if (policy)
		cpufreq_cpu_put(policy);
	return ret < 0 ? ret : len;
}
static DEVICE_ATTR(cpu_limits, 0664, cpu_limits_show, cpu_limits_store);

static struct attribute *msg_attrs[] = {
	&dev_attr_sconfig.attr,
	&dev_attr_balance_mode.attr,
	&dev_attr_boost.attr,
	&dev_attr_temp_state.attr,
	&dev_attr_cpu_limits.attr,
	&dev_attr_board_sensor.attr,
	&dev_attr_board_sensor_temp.attr,
	&dev_attr_board_sensor_second_temp.attr,
	&dev_attr_charger_temp.attr,
	&dev_attr_modem_limit.attr,
	&dev_attr_market_download_limit.attr,
	&dev_attr_flash_state.attr,
	&dev_attr_wifi_limit.attr,
	&dev_attr_poor_modem_limit.attr,
	&dev_attr_screen_state.attr,
	&dev_attr_usb_online.attr,
	NULL,
};
ATTRIBUTE_GROUPS(msg);

static int usb_changed(struct notifier_block *nb, unsigned long event, void *data)
{
	struct power_supply *psy = data;
	union power_supply_propval val;

	if (strcmp(psy->desc->name, "usb") ||
	    power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val))
		return NOTIFY_OK;
	if (atomic_xchg(&usb_online, val.intval) != val.intval && msg_dev)
		sysfs_notify(&msg_dev->kobj, NULL, "usb_online");
	return NOTIFY_OK;
}
static struct notifier_block usb_nb = { .notifier_call = usb_changed };

/* a cooling device that never cools, only there for its class */
static int dummy_get_max(struct thermal_cooling_device *c, unsigned long *s)
{
	*s = 0;
	return 0;
}
static int dummy_get_cur(struct thermal_cooling_device *c, unsigned long *s)
{
	*s = 0;
	return 0;
}
static int dummy_set_cur(struct thermal_cooling_device *c, unsigned long s)
{
	return 0;
}
static const struct thermal_cooling_device_ops dummy_ops = {
	.get_max_state = dummy_get_max,
	.get_cur_state = dummy_get_cur,
	.set_cur_state = dummy_set_cur,
};

static int __init mi_thermal_init(void)
{
	int ret;

	cdev = thermal_cooling_device_register("mi_thermal_message", NULL, &dummy_ops);
	if (IS_ERR(cdev))
		return PTR_ERR(cdev);
	msg_dev = device_create_with_groups(cdev->device.class, NULL, MKDEV(0, 0),
					    NULL, msg_groups, "thermal_message");
	if (IS_ERR(msg_dev)) {
		ret = PTR_ERR(msg_dev);
		msg_dev = NULL;
		thermal_cooling_device_unregister(cdev);
		return ret;
	}
	ret = power_supply_reg_notifier(&usb_nb);
	if (ret)
		pr_warn("mi_thermal: no usb_online updates: %d\n", ret);
	return 0;
}

static void __exit mi_thermal_exit(void)
{
	int cpu;

	power_supply_unreg_notifier(&usb_nb);
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!limit_policy[cpu])
			continue;
		freq_qos_remove_request(&limit_req[cpu]);
		cpufreq_cpu_put(limit_policy[cpu]);
	}
	device_unregister(msg_dev);
	thermal_cooling_device_unregister(cdev);
}

module_init(mi_thermal_init);
module_exit(mi_thermal_exit);

MODULE_DESCRIPTION("Xiaomi thermal_message nodes for mi_thermald");
MODULE_LICENSE("GPL");
