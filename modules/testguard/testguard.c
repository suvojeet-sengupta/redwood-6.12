// SPDX-License-Identifier: GPL-2.0
/*
 * Safety net for testing Android on redwood 6.12 next to a daily 5.4 slot:
 * - userdata is held exclusively and read-only, so nothing on 6.12 can mount
 *   it or write to it, encrypted or not;
 * - misc is read-only, so 6.12 can't leave a wipe command for recovery that
 *   a later 5.4 boot would then carry out;
 * - a reboot into recovery goes to the bootloader instead, and a recovery
 *   boot of 6.12 goes straight back to the bootloader;
 * - if userdata and misc aren't guarded within a few seconds, before second
 *   stage init could mount anything, it reboots to the bootloader.
 */
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/sched/task_stack.h>
#include <linux/string.h>

#define SCAN_MINORS	1024
#define DEADLINE_MS	6000

static struct file *userdata, *misc;
static int holder;

static struct file *open_part(const char *name, bool claim)
{
	static const unsigned int majors[] = { 8, 259 };
	struct file *f;
	int i, m;

	for (i = 0; i < ARRAY_SIZE(majors); i++) {
		for (m = 0; m < SCAN_MINORS; m++) {
			f = bdev_file_open_by_dev(MKDEV(majors[i], m), BLK_OPEN_READ, NULL, NULL);
			if (IS_ERR(f))
				continue;
			if (file_bdev(f)->bd_meta_info &&
			    !strcmp(file_bdev(f)->bd_meta_info->volname, name)) {
				dev_t devt = file_bdev(f)->bd_dev;

				if (!claim)
					return f;
				fput(f);
				return bdev_file_open_by_dev(devt, BLK_OPEN_READ, &holder, NULL);
			}
			fput(f);
		}
	}
	return NULL;
}

static int guard_reboot(struct notifier_block *nb, unsigned long action, void *data)
{
	char *cmd = data;

	if (action != SYS_RESTART || !cmd || !strstr(cmd, "recovery"))
		return NOTIFY_DONE;
	if (object_is_on_stack(cmd) && strlen(cmd) >= strlen("bootloader")) {
		pr_emerg("testguard: \"%s\" turned into a reboot to the bootloader\n", cmd);
		strcpy(cmd, "bootloader");
	} else {
		pr_emerg("testguard: can't redirect reboot \"%s\"\n", cmd);
	}
	return NOTIFY_DONE;
}

static struct notifier_block guard_nb = {
	.notifier_call = guard_reboot,
	.priority = INT_MAX,
};

static void to_bootloader(const char *why)
{
	pr_emerg("testguard: %s, rebooting to the bootloader\n", why);
	msleep(500);
	kernel_restart("bootloader");
}

static int guard_thread(void *unused)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(DEADLINE_MS);
	struct file *f;

	while (!userdata || !misc) {
		if (!userdata) {
			f = open_part("userdata", true);
			if (IS_ERR(f)) {
				to_bootloader("can't claim userdata");
				return 0;
			}
			if (f) {
				bdev_set_flag(file_bdev(f), BD_READ_ONLY);
				userdata = f;
				pr_info("testguard: holding %pg (userdata) read-only\n", file_bdev(f));
			}
		}
		if (!misc) {
			f = open_part("misc", false);
			if (f) {
				bdev_set_flag(file_bdev(f), BD_READ_ONLY);
				misc = f;
				pr_info("testguard: %pg (misc) read-only\n", file_bdev(f));
			}
		}
		if (userdata && misc)
			break;
		if (time_after(jiffies, deadline)) {
			to_bootloader("userdata or misc not found in time");
			return 0;
		}
		msleep_interruptible(50);
	}
	while (!kthread_should_stop())
		msleep_interruptible(1000);
	return 0;
}

static int __init testguard_init(void)
{
	const char *args = NULL;
	struct task_struct *t;

	if (of_chosen)
		of_property_read_string(of_chosen, "bootargs", &args);
	if (!args || !strstr(args, "androidboot.force_normal_boot=1")) {
		to_bootloader("not a normal boot");
		return -EPERM;
	}
	register_reboot_notifier(&guard_nb);
	t = kthread_run(guard_thread, NULL, "testguard");
	if (IS_ERR(t)) {
		to_bootloader("no guard thread");
		return PTR_ERR(t);
	}
	return 0;
}

module_init(testguard_init);
MODULE_DESCRIPTION("Keep userdata and misc safe while testing Android on redwood 6.12");
MODULE_LICENSE("GPL");
