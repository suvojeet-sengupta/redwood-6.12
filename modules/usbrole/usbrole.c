// SPDX-License-Identifier: GPL-2.0
/*
 * Set redwood's USB role from the charger's view of the cable.
 *
 * On 6.12 the ADSP's UCSI reports nothing connected, so the role stays at
 * none and adb can't come up; once the ADSP runs, it also sets the role
 * back to none whenever it looks. Until UCSI is fixed, follow the "usb"
 * power supply instead: device role while a cable is in, none when it is
 * out or it is a wall charger (DCP). Holding the device role all the time
 * kept dwc3-msm's wakeup source active for good, so the phone never
 * suspended.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>

static struct delayed_work work;
static struct notifier_block psy_nb;
static struct usb_role_switch *sw;
static int tries;

static enum usb_role usbrole_wanted(void)
{
	struct power_supply *psy = power_supply_get_by_name("usb");
	union power_supply_propval val;
	enum usb_role role = USB_ROLE_NONE;

	if (!psy)
		return USB_ROLE_DEVICE;

	if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val) &&
	    val.intval) {
		/*
		 * Only a dedicated wall charger has nobody to talk to; a
		 * computer's Type-C port may well show up as C or PD.
		 */
		if (power_supply_get_property(psy, POWER_SUPPLY_PROP_USB_TYPE,
					      &val) ||
		    val.intval != POWER_SUPPLY_USB_TYPE_DCP)
			role = USB_ROLE_DEVICE;
	}

	power_supply_put(psy);
	return role;
}

static void usbrole_work(struct work_struct *w)
{
	struct device_node *np;
	enum usb_role role, wanted;

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
	wanted = usbrole_wanted();
	if (role != wanted) {
		if (usb_role_switch_set_role(sw, wanted))
			pr_err("usbrole: setting role %d failed\n", wanted);
		else
			pr_info("usbrole: USB role %d -> %d\n", role, wanted);
	}

	/* UCSI may still drop the role behind our back while a cable is in */
	if (wanted == USB_ROLE_DEVICE)
		schedule_delayed_work(&work, 2 * HZ);
}

static int usbrole_psy_changed(struct notifier_block *nb, unsigned long event,
			       void *data)
{
	struct power_supply *psy = data;

	if (event == PSY_EVENT_PROP_CHANGED && psy->desc->name &&
	    !strcmp(psy->desc->name, "usb"))
		mod_delayed_work(system_wq, &work, HZ / 2);

	return NOTIFY_OK;
}

static int __init usbrole_init(void)
{
	INIT_DELAYED_WORK(&work, usbrole_work);
	psy_nb.notifier_call = usbrole_psy_changed;
	power_supply_reg_notifier(&psy_nb);
	schedule_delayed_work(&work, 0);
	return 0;
}

static void __exit usbrole_exit(void)
{
	power_supply_unreg_notifier(&psy_nb);
	cancel_delayed_work_sync(&work);
	if (sw)
		usb_role_switch_put(sw);
}

module_init(usbrole_init);
module_exit(usbrole_exit);
MODULE_DESCRIPTION("Follow the charger's view of the USB cable for the role");
MODULE_LICENSE("GPL");
