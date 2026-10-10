// SPDX-License-Identifier: GPL-2.0
/*
 * Keep redwood's USB controller in device mode for bring-up.
 *
 * On 6.12 the ADSP's UCSI reports nothing connected, so the role stays at
 * none and adb can't come up; once the ADSP runs, it also sets the role
 * back to none whenever it looks. Until UCSI is fixed, find the role
 * switch dwc3-msm registers and keep putting it back to device.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>

static struct delayed_work work;
static struct usb_role_switch *sw;
static int tries;

static void usbrole_work(struct work_struct *w)
{
	struct device_node *np;
	enum usb_role role;

	if (!sw) {
		np = of_find_compatible_node(NULL, NULL, "qcom,dwc-usb3-msm");
		sw = np ? usb_role_switch_find_by_fwnode(of_fwnode_handle(np)) : NULL;
		of_node_put(np);
		if (IS_ERR_OR_NULL(sw)) {
			sw = NULL;
			if (++tries < 60)
				schedule_delayed_work(&work, HZ);
			else
				pr_err("usbrole: no USB role switch\n");
			return;
		}
	}

	role = usb_role_switch_get_role(sw);
	if (role != USB_ROLE_DEVICE) {
		if (usb_role_switch_set_role(sw, USB_ROLE_DEVICE))
			pr_err("usbrole: setting device role failed\n");
		else
			pr_info("usbrole: USB role %d, set back to device\n", role);
	}
	schedule_delayed_work(&work, 2 * HZ);
}

static int __init usbrole_init(void)
{
	INIT_DELAYED_WORK(&work, usbrole_work);
	schedule_delayed_work(&work, 0);
	return 0;
}

static void __exit usbrole_exit(void)
{
	cancel_delayed_work_sync(&work);
	if (sw)
		usb_role_switch_put(sw);
}

module_init(usbrole_init);
module_exit(usbrole_exit);
MODULE_DESCRIPTION("Keep the USB role at device for bring-up");
MODULE_LICENSE("GPL");
