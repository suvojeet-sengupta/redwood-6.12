#!/usr/bin/env python3
# Double tap to wake for mainline goodix_berlin on redwood: with the screen
# off and Xiaomi's touch mode 14 (Touch_Doubletap_Mode) on, the IC is put in
# gesture mode instead of staying awake, and a double tap reports KEY_WAKEUP.
# The gesture command and event layout are from Xiaomi's gt9916 driver.
# Build with xiaomi_touch's Module.symvers in KBUILD_EXTRA_SYMBOLS.
# Usage: dt2w.py <dir with goodix_berlin_core.c> <xiaomi_touch.h> <panel_event_notifier.h>
import os, shutil, sys

d, xmh, pnh = sys.argv[1:4]
c = os.path.join(d, "goodix_berlin_core.c")
shutil.copy(xmh, os.path.join(d, "xiaomi_touch.h"))
shutil.copy(pnh, os.path.join(d, "panel_event_notifier.h"))
s = open(c).read()


def sub(old, new):
    global s
    if s.count(old) != 1:
        sys.exit("dt2w: no unique match for %r" % old[:60])
    s = s.replace(old, new)


sub("""#include <linux/unaligned.h>

#include "goodix_berlin.h"
""", """#include <linux/unaligned.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <drm/drm_panel.h>

#include "goodix_berlin.h"
#include "panel_event_notifier.h"
#include "xiaomi_touch.h"
""")

sub("""#define GOODIX_BERLIN_REQUEST_EVENT		BIT(6)
""", """#define GOODIX_BERLIN_REQUEST_EVENT		BIT(6)
#define GOODIX_BERLIN_GESTURE_EVENT		BIT(5)
#define GOODIX_BERLIN_GESTURE_DOUBLE_TAP	0xcc

#define GOODIX_BERLIN_CMD_GESTURE		0xa6
#define GOODIX_BERLIN_CMD_BUF_LEN		16
#define GOODIX_BERLIN_CMD_ACK_OK		0x80
#define GOODIX_BERLIN_CMD_ACK_BUSY		0x02
#define GOODIX_BERLIN_CMD_RETRY			6
""")

sub("""	/* Runtime parameters extracted from IC_INFO buffer  */
	u32 touch_data_addr;
""", """	/* Runtime parameters extracted from IC_INFO buffer  */
	u32 touch_data_addr;
	u32 cmd_addr;

	/* double tap to wake */
	struct mutex dt2w_lock;
	struct delayed_work panel_work;
	struct work_struct blank_work;
	void *panel_cookie;
	int panel_tries;
	bool dt2w;
	bool screen_off;
	bool gesture_mode;
	bool irq_wake;
""")

sub("""	misc = (struct goodix_berlin_ic_info_misc *)&data[offset];
	cd->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
""", """	misc = (struct goodix_berlin_ic_info_misc *)&data[offset];
	cd->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
	cd->cmd_addr = le32_to_cpu(misc->cmd_addr);
""")

# command helper and gesture mode, before the IRQ handler
sub("""static irqreturn_t goodix_berlin_irq(int irq, void *data)
{""", """/*
 * A command is { state, ack, len, cmd, data..., checksum }: the u16 checksum
 * adds up the bytes from len to the end of the data, and the IC answers in
 * the ack byte.
 */
static int goodix_berlin_send_cmd(struct goodix_berlin_core *cd, u8 cmd,
				  const u8 *data, u8 data_len)
{
	u8 buf[GOODIX_BERLIN_CMD_BUF_LEN] = { 0 };
	u8 ack[GOODIX_BERLIN_CMD_BUF_LEN];
	u16 sum = 0;
	int retry, i, error;

	if (!cd->cmd_addr || data_len > GOODIX_BERLIN_CMD_BUF_LEN - 6)
		return -EINVAL;

	buf[2] = data_len + 4;
	buf[3] = cmd;
	memcpy(&buf[4], data, data_len);
	for (i = 2; i < buf[2]; i++)
		sum += buf[i];
	put_unaligned_le16(sum, &buf[buf[2]]);

	for (retry = 0; retry < GOODIX_BERLIN_CMD_RETRY; retry++) {
		error = regmap_raw_write(cd->regmap, cd->cmd_addr, buf,
					 sizeof(buf));
		if (error)
			return error;

		for (i = 0; i < GOODIX_BERLIN_CMD_RETRY; i++) {
			error = regmap_raw_read(cd->regmap, cd->cmd_addr, ack,
						sizeof(ack));
			if (error)
				return error;
			if (ack[1] == GOODIX_BERLIN_CMD_ACK_OK)
				return 0;
			usleep_range(1000, 1100);
			if (ack[1] != GOODIX_BERLIN_CMD_ACK_BUSY && ack[1])
				break;
		}
	}

	dev_err(cd->dev, "command 0x%02x not acked\\n", cmd);
	return -EIO;
}

/* Double tap only: the first two bytes mask off the gestures not wanted */
static int goodix_berlin_arm_gesture(struct goodix_berlin_core *cd)
{
	static const u8 data[] = { 0x00, 0x10 };

	return goodix_berlin_send_cmd(cd, GOODIX_BERLIN_CMD_GESTURE, data,
				      sizeof(data));
}

static int goodix_berlin_request_handle_reset(struct goodix_berlin_core *cd);

/* Called with dt2w_lock held */
static void goodix_berlin_update_gesture(struct goodix_berlin_core *cd)
{
	bool want = cd->dt2w && cd->screen_off;

	if (want == cd->gesture_mode)
		return;

	if (want) {
		if (goodix_berlin_arm_gesture(cd))
			return;
		if (!enable_irq_wake(cd->irq))
			cd->irq_wake = true;
		cd->gesture_mode = true;
	} else {
		if (cd->irq_wake && !disable_irq_wake(cd->irq))
			cd->irq_wake = false;
		cd->gesture_mode = false;
		/* a reset takes the IC back to normal reporting */
		if (cd->reset_gpio)
			goodix_berlin_request_handle_reset(cd);
	}
	dev_info(cd->dev, "double tap gesture mode %s\\n",
		 cd->gesture_mode ? "on" : "off");
}

static void goodix_berlin_blank_work(struct work_struct *work)
{
	struct goodix_berlin_core *cd =
		container_of(work, struct goodix_berlin_core, blank_work);

	mutex_lock(&cd->dt2w_lock);
	goodix_berlin_update_gesture(cd);
	mutex_unlock(&cd->dt2w_lock);
}

static void goodix_berlin_panel_event(enum panel_event_notifier_tag tag,
				      struct panel_event_notification *event,
				      void *data)
{
	struct goodix_berlin_core *cd = data;
	bool off;

	if (!event || event->notif_data.early_trigger)
		return;

	switch (event->notif_type) {
	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_BLANK_LP:
		off = true;
		break;
	case DRM_PANEL_EVENT_UNBLANK:
		off = false;
		break;
	default:
		return;
	}

	WRITE_ONCE(cd->screen_off, off);
	schedule_work(&cd->blank_work);
}

static struct goodix_berlin_core *goodix_berlin_xm_cd;

static int goodix_berlin_xm_set_mode(int mode, int value)
{
	struct goodix_berlin_core *cd = goodix_berlin_xm_cd;

	if (!cd || mode != Touch_Doubletap_Mode)
		return 0;

	mutex_lock(&cd->dt2w_lock);
	cd->dt2w = !!value;
	goodix_berlin_update_gesture(cd);
	mutex_unlock(&cd->dt2w_lock);

	return 0;
}

/*
 * The panel registers once the display driver probes, and xiaomi_touch may
 * finish probing after us too, so keep looking for both for a while.
 */
static void goodix_berlin_panel_work(struct work_struct *work)
{
	struct goodix_berlin_core *cd = container_of(to_delayed_work(work),
					struct goodix_berlin_core, panel_work);
	struct device_node *np = NULL;
	struct drm_panel *panel;

	if (!cd->panel_cookie) {
		while ((np = of_find_node_with_property(np,
					"qcom,mdss-dsi-panel-name"))) {
			panel = of_drm_find_panel(np);
			if (IS_ERR(panel))
				continue;
			cd->panel_cookie = panel_event_notifier_register(
					PANEL_EVENT_NOTIFICATION_PRIMARY,
					PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH,
					panel, goodix_berlin_panel_event, cd);
			if (IS_ERR(cd->panel_cookie))
				cd->panel_cookie = NULL;
			of_node_put(np);
			break;
		}
	}

	if (!goodix_berlin_xm_cd && get_xiaomi_touch_dev()) {
		struct xiaomi_touch_interface *xm;

		xm = devm_kzalloc(cd->dev, sizeof(*xm), GFP_KERNEL);
		if (xm) {
			goodix_berlin_xm_cd = cd;
			xm->setModeValue = goodix_berlin_xm_set_mode;
			xiaomitouch_register_modedata(0, xm);
		}
	}

	if ((!cd->panel_cookie || !goodix_berlin_xm_cd) &&
	    ++cd->panel_tries < 120)
		schedule_delayed_work(&cd->panel_work, HZ);
	else
		dev_info(cd->dev, "double tap: panel %s, xiaomi_touch %s\\n",
			 cd->panel_cookie ? "found" : "missing",
			 goodix_berlin_xm_cd ? "found" : "missing");
}

static void goodix_berlin_gesture_irq(struct goodix_berlin_core *cd)
{
	if ((cd->event.hdr.status & GOODIX_BERLIN_GESTURE_EVENT) &&
	    cd->event.hdr.reserved2[1] == GOODIX_BERLIN_GESTURE_DOUBLE_TAP) {
		input_report_key(cd->input_dev, KEY_WAKEUP, 1);
		input_sync(cd->input_dev);
		input_report_key(cd->input_dev, KEY_WAKEUP, 0);
		input_sync(cd->input_dev);
	}

	regmap_write(cd->regmap, cd->touch_data_addr, 0);
	/* the IC wants the gesture command again after each gesture */
	goodix_berlin_arm_gesture(cd);
}

static irqreturn_t goodix_berlin_irq(int irq, void *data)
{""")

sub("""	if (cd->event.hdr.status & GOODIX_BERLIN_TOUCH_EVENT)
		goodix_berlin_touch_handler(cd);
""", """	if (READ_ONCE(cd->gesture_mode)) {
		goodix_berlin_gesture_irq(cd);
		goto out;
	}

	if (cd->event.hdr.status & GOODIX_BERLIN_TOUCH_EVENT)
		goodix_berlin_touch_handler(cd);
""")

sub("""	input_set_abs_params(cd->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
""", """	input_set_abs_params(cd->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_capability(cd->input_dev, EV_KEY, KEY_WAKEUP);
""")

# keep the IC powered in gesture mode across system suspend; the wake IRQ
# stays armed and a pending one is handled once SPI is back
sub("""	struct goodix_berlin_core *cd = dev_get_drvdata(dev);

	disable_irq(cd->irq);
	goodix_berlin_power_off(cd);

	return 0;
}""", """	struct goodix_berlin_core *cd = dev_get_drvdata(dev);

	disable_irq(cd->irq);
	if (READ_ONCE(cd->gesture_mode))
		return 0;
	goodix_berlin_power_off(cd);

	return 0;
}""")

sub("""	struct goodix_berlin_core *cd = dev_get_drvdata(dev);
	int error;

	error = goodix_berlin_power_on(cd);
	if (error)
		return error;

	enable_irq(cd->irq);""", """	struct goodix_berlin_core *cd = dev_get_drvdata(dev);
	int error;

	if (READ_ONCE(cd->gesture_mode)) {
		enable_irq(cd->irq);
		return 0;
	}

	error = goodix_berlin_power_on(cd);
	if (error)
		return error;

	enable_irq(cd->irq);""")

sub("""	dev_set_drvdata(dev, cd);

	dev_dbg(dev, "Goodix Berlin %s Touchscreen Controller",""", """	dev_set_drvdata(dev, cd);

	mutex_init(&cd->dt2w_lock);
	INIT_WORK(&cd->blank_work, goodix_berlin_blank_work);
	INIT_DELAYED_WORK(&cd->panel_work, goodix_berlin_panel_work);
	schedule_delayed_work(&cd->panel_work, 0);

	dev_dbg(dev, "Goodix Berlin %s Touchscreen Controller",""")

open(c, "w").write(s)
print("dt2w: double tap to wake added")
