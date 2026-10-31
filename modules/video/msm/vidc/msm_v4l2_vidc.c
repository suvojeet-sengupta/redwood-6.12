// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/compat.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include "msm_vidc.h"
#include "msm_vidc_common.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_internal.h"
#include "msm_vidc_res_parse.h"
#include "msm_vidc_resources.h"
#include "vidc_hfi_api.h"
#include "msm_vidc_clocks.h"

#define BASE_DEVICE_NUMBER 32

struct msm_vidc_drv *vidc_driver;


static inline struct msm_vidc_inst *get_vidc_inst(struct file *filp, void *fh)
{
	if (!filp->private_data)
		return NULL;
	return container_of(filp->private_data,
					struct msm_vidc_inst, event_handler);
}

#ifdef CONFIG_COMPAT
/* The 32-bit buffer layouts of v4l2-compat-ioctl32.c, whose plane conversion drops reserved[] */
struct msm_v4l2_plane32 {
	__u32 bytesused;
	__u32 length;
	union {
		__u32 mem_offset;
		compat_long_t userptr;
		__s32 fd;
	} m;
	__u32 data_offset;
	__u32 reserved[11];
};

struct msm_v4l2_buffer32 {
	__u32 index;
	__u32 type;
	__u32 bytesused;
	__u32 flags;
	__u32 field;
	struct {
		compat_s64 tv_sec;
		compat_s64 tv_usec;
	} timestamp;
	struct v4l2_timecode timecode;
	__u32 sequence;
	__u32 memory;
	union {
		__u32 offset;
		compat_long_t userptr;
		compat_caddr_t planes;
		__s32 fd;
	} m;
	__u32 length;
	__u32 reserved2;
	__s32 request_fd;
};

struct msm_v4l2_buffer32_time32 {
	__u32 index;
	__u32 type;
	__u32 bytesused;
	__u32 flags;
	__u32 field;
	struct old_timeval32 timestamp;
	struct v4l2_timecode timecode;
	__u32 sequence;
	__u32 memory;
	union {
		__u32 offset;
		compat_long_t userptr;
		compat_caddr_t planes;
		__s32 fd;
	} m;
	__u32 length;
	__u32 reserved2;
	__s32 request_fd;
};

#define MSM_VIDIOC_QBUF32		_IOWR('V', 15, struct msm_v4l2_buffer32)
#define MSM_VIDIOC_DQBUF32		_IOWR('V', 17, struct msm_v4l2_buffer32)
#define MSM_VIDIOC_QBUF32_TIME32	_IOWR('V', 15, struct msm_v4l2_buffer32_time32)
#define MSM_VIDIOC_DQBUF32_TIME32	_IOWR('V', 17, struct msm_v4l2_buffer32_time32)

static_assert(sizeof_field(struct msm_v4l2_plane32, reserved) ==
	MSM_VIDC_PLANE_WORDS * sizeof(__u32));

static u32 *msm_v4l2_compat_slot(struct msm_vidc_inst *inst, u32 type,
		u32 index, u32 plane)
{
	u32 q;

	if (!inst->compat_reserved || index >= VB2_MAX_FRAME ||
			plane >= VIDEO_MAX_PLANES)
		return NULL;
	if (type == INPUT_MPLANE)
		q = 0;
	else if (type == OUTPUT_MPLANE)
		q = 1;
	else
		return NULL;
	return inst->compat_reserved[q][index][plane];
}

static int msm_v4l2_compat_buf(unsigned int cmd, void __user *arg, u32 *type,
		u32 *index, u32 *length, struct msm_v4l2_plane32 __user **planes)
{
	compat_caddr_t p;

	if (cmd == MSM_VIDIOC_QBUF32 || cmd == MSM_VIDIOC_DQBUF32) {
		struct msm_v4l2_buffer32 __user *b = arg;

		if (get_user(*index, &b->index) || get_user(*type, &b->type) ||
				get_user(*length, &b->length) ||
				get_user(p, &b->m.planes))
			return -EFAULT;
	} else {
		struct msm_v4l2_buffer32_time32 __user *b = arg;

		if (get_user(*index, &b->index) || get_user(*type, &b->type) ||
				get_user(*length, &b->length) ||
				get_user(p, &b->m.planes))
			return -EFAULT;
	}
	*planes = compat_ptr(p);
	return 0;
}

static int msm_v4l2_compat_words(struct msm_vidc_inst *inst,
		unsigned int cmd, void __user *arg, bool to_user)
{
	struct msm_v4l2_plane32 __user *planes;
	u32 type, index, length, i, *slot;
	int rc;

	rc = msm_v4l2_compat_buf(cmd, arg, &type, &index, &length, &planes);
	if (rc || !V4L2_TYPE_IS_MULTIPLANAR(type))
		return rc;
	for (i = 0; i < length; i++) {
		slot = msm_v4l2_compat_slot(inst, type, index, i);
		if (!slot)
			break;
		if (to_user)
			rc = copy_to_user(planes[i].reserved, slot,
				sizeof(planes[i].reserved));
		else
			rc = copy_from_user(slot, planes[i].reserved,
				sizeof(planes[i].reserved));
		if (rc)
			return -EFAULT;
	}
	return 0;
}

/* QBUF keeps the caller's words for msm_v4l2_qbuf, DQBUF hands back what msm_v4l2_dqbuf kept */
static long msm_v4l2_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct msm_vidc_inst *inst = get_vidc_inst(filp, NULL);
	long rc;

	if (!inst->compat_reserved)
		return video_ioctl2(filp, cmd, arg);

	switch (cmd) {
	case MSM_VIDIOC_QBUF32:
	case MSM_VIDIOC_QBUF32_TIME32:
		rc = msm_v4l2_compat_words(inst, cmd, (void __user *)arg, false);
		if (rc)
			return rc;
		return video_ioctl2(filp, cmd, arg);
	case MSM_VIDIOC_DQBUF32:
	case MSM_VIDIOC_DQBUF32_TIME32:
		rc = video_ioctl2(filp, cmd, arg);
		if (rc)
			return rc;
		return msm_v4l2_compat_words(inst, cmd, (void __user *)arg, true);
	default:
		return video_ioctl2(filp, cmd, arg);
	}
}

static void msm_v4l2_compat_copy(struct msm_vidc_inst *inst,
		struct v4l2_buffer *b, bool save)
{
	u32 i, *slot;

	if (!in_compat_syscall() || !V4L2_TYPE_IS_MULTIPLANAR(b->type))
		return;
	for (i = 0; i < b->length; i++) {
		slot = msm_v4l2_compat_slot(inst, b->type, b->index, i);
		if (!slot)
			return;
		if (save)
			memcpy(slot, b->m.planes[i].reserved,
				sizeof(b->m.planes[i].reserved));
		else
			memcpy(b->m.planes[i].reserved, slot,
				sizeof(b->m.planes[i].reserved));
	}
}
#else
static inline void msm_v4l2_compat_copy(struct msm_vidc_inst *inst,
		struct v4l2_buffer *b, bool save)
{
}
#endif

static long msm_v4l2_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		return msm_v4l2_compat_ioctl(filp, cmd, arg);
#endif
	return video_ioctl2(filp, cmd, arg);
}

static int msm_v4l2_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct msm_video_device *vid_dev =
		container_of(vdev, struct msm_video_device, vdev);
	struct msm_vidc_core *core = video_drvdata(filp);
	struct msm_vidc_inst *vidc_inst;

	trace_msm_v4l2_vidc_open_start("msm v4l2_open start");
	vidc_inst = msm_vidc_open(core->id, vid_dev->type);
	if (!vidc_inst) {
		d_vpr_e("Failed to create instance, core: %d, type = %d\n",
		core->id, vid_dev->type);
		return -ENOMEM;
	}
#ifdef CONFIG_COMPAT
	if (in_compat_syscall()) {
		vidc_inst->compat_reserved = kvcalloc(2,
			sizeof(*vidc_inst->compat_reserved), GFP_KERNEL);
		if (!vidc_inst->compat_reserved) {
			msm_vidc_close(vidc_inst);
			return -ENOMEM;
		}
	}
#endif
	clear_bit(V4L2_FL_USES_V4L2_FH, &vdev->flags);
	filp->private_data = &(vidc_inst->event_handler);
	trace_msm_v4l2_vidc_open_end("msm v4l2_open end");
	return 0;
}

static int msm_v4l2_close(struct file *filp)
{
	int rc = 0;
	struct msm_vidc_inst *vidc_inst;

	trace_msm_v4l2_vidc_close_start("msm v4l2_close start");
	vidc_inst = get_vidc_inst(filp, NULL);
	kvfree(vidc_inst->compat_reserved);
	vidc_inst->compat_reserved = NULL;

	rc = msm_vidc_close(vidc_inst);
	filp->private_data = NULL;
	trace_msm_v4l2_vidc_close_end("msm v4l2_close end");
	return rc;
}

static int msm_v4l2_querycap(struct file *filp, void *fh,
			struct v4l2_capability *cap)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(filp, fh);

	return msm_vidc_querycap((void *)vidc_inst, cap);
}

int msm_v4l2_enum_fmt(struct file *file, void *fh,
					struct v4l2_fmtdesc *f)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_enum_fmt((void *)vidc_inst, f);
}

int msm_v4l2_s_fmt(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_s_fmt((void *)vidc_inst, f);
}

int msm_v4l2_g_fmt(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_g_fmt((void *)vidc_inst, f);
}

int msm_v4l2_s_ctrl(struct file *file, void *fh,
					struct v4l2_control *a)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_s_ctrl((void *)vidc_inst, a);
}

int msm_v4l2_g_ctrl(struct file *file, void *fh,
					struct v4l2_control *a)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_g_ctrl((void *)vidc_inst, a);
}

int msm_v4l2_reqbufs(struct file *file, void *fh,
				struct v4l2_requestbuffers *b)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_reqbufs((void *)vidc_inst, b);
}

int msm_v4l2_qbuf(struct file *file, void *fh,
				struct v4l2_buffer *b)
{
	struct video_device *vdev = video_devdata(file);
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	msm_v4l2_compat_copy(vidc_inst, b, false);
	return msm_vidc_qbuf(vidc_inst, vdev->v4l2_dev->mdev, b);
}

int msm_v4l2_dqbuf(struct file *file, void *fh,
				struct v4l2_buffer *b)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	int rc;

	rc = msm_vidc_dqbuf(vidc_inst, b);
	if (!rc)
		msm_v4l2_compat_copy(vidc_inst, b, true);
	return rc;
}

int msm_v4l2_streamon(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_streamon((void *)vidc_inst, i);
}

int msm_v4l2_streamoff(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_streamoff((void *)vidc_inst, i);
}

static int msm_v4l2_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	struct msm_vidc_inst *vidc_inst = container_of(fh,
			struct msm_vidc_inst, event_handler);

	return msm_vidc_subscribe_event((void *)vidc_inst, sub);
}

static int msm_v4l2_unsubscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	struct msm_vidc_inst *vidc_inst = container_of(fh,
			struct msm_vidc_inst, event_handler);

	return msm_vidc_unsubscribe_event((void *)vidc_inst, sub);
}

static int msm_v4l2_decoder_cmd(struct file *file, void *fh,
				struct v4l2_decoder_cmd *dec)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_comm_cmd((void *)vidc_inst, (union msm_v4l2_cmd *)dec);
}

static int msm_v4l2_encoder_cmd(struct file *file, void *fh,
				struct v4l2_encoder_cmd *enc)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_comm_cmd((void *)vidc_inst, (union msm_v4l2_cmd *)enc);
}

static int msm_v4l2_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_enum_framesizes((void *)vidc_inst, fsize);
}

static int msm_v4l2_queryctrl(struct file *file, void *fh,
	struct v4l2_queryctrl *ctrl)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_query_ctrl((void *)vidc_inst, ctrl);
}

static int msm_v4l2_querymenu(struct file *file, void *fh,
	struct v4l2_querymenu *qmenu)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);

	return msm_vidc_query_menu((void *)vidc_inst, qmenu);
}

const struct v4l2_ioctl_ops msm_v4l2_ioctl_ops = {
	.vidioc_querycap = msm_v4l2_querycap,
	.vidioc_enum_fmt_vid_cap = msm_v4l2_enum_fmt,
	.vidioc_enum_fmt_vid_out = msm_v4l2_enum_fmt,
	.vidioc_s_fmt_vid_cap_mplane = msm_v4l2_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = msm_v4l2_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane = msm_v4l2_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = msm_v4l2_g_fmt,
	.vidioc_reqbufs = msm_v4l2_reqbufs,
	.vidioc_qbuf = msm_v4l2_qbuf,
	.vidioc_dqbuf = msm_v4l2_dqbuf,
	.vidioc_streamon = msm_v4l2_streamon,
	.vidioc_streamoff = msm_v4l2_streamoff,
	.vidioc_s_ctrl = msm_v4l2_s_ctrl,
	.vidioc_g_ctrl = msm_v4l2_g_ctrl,
	.vidioc_queryctrl = msm_v4l2_queryctrl,
	.vidioc_querymenu = msm_v4l2_querymenu,
	.vidioc_subscribe_event = msm_v4l2_subscribe_event,
	.vidioc_unsubscribe_event = msm_v4l2_unsubscribe_event,
	.vidioc_decoder_cmd = msm_v4l2_decoder_cmd,
	.vidioc_encoder_cmd = msm_v4l2_encoder_cmd,
	.vidioc_enum_framesizes = msm_v4l2_enum_framesizes,
};

static const struct v4l2_ioctl_ops msm_v4l2_enc_ioctl_ops = { 0 };

static unsigned int msm_v4l2_poll(struct file *filp,
	struct poll_table_struct *pt)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(filp, NULL);

	return msm_vidc_poll((void *)vidc_inst, filp, pt);
}

static const struct v4l2_file_operations msm_v4l2_vidc_fops = {
	.owner = THIS_MODULE,
	.open = msm_v4l2_open,
	.release = msm_v4l2_close,
	.unlocked_ioctl = msm_v4l2_ioctl,
	.poll = msm_v4l2_poll,
};

void msm_vidc_release_video_device(struct video_device *pvdev)
{
}

static int read_platform_resources(struct msm_vidc_core *core,
		struct platform_device *pdev)
{
	int rc = 0;

	if (!core || !pdev) {
		d_vpr_e("%s: invalid params %pK %pK\n",
			__func__, core, pdev);
		return -EINVAL;
	}
	if (!pdev->dev.of_node) {
		d_vpr_e("%s: pdev node is NULL\n", __func__);
		return -EINVAL;
	}

	core->hfi_type = VIDC_HFI_VENUS;
	core->resources.pdev = pdev;
	/* Target supports DT, parse from it */
	rc = read_platform_resources_from_drv_data(core);
	if (rc) {
		d_vpr_e("%s: read platform resources from driver failed\n",
			__func__);
		return rc;
	}

	rc = read_platform_resources_from_dt(&core->resources);
	if (rc) {
		d_vpr_e("%s: read platform resources from dt failed\n",
			__func__);
		return rc;
	}
	return 0;
}

static int msm_vidc_initialize_core(struct platform_device *pdev,
				struct msm_vidc_core *core)
{
	int i = 0;
	int rc = 0;

	if (!core)
		return -EINVAL;
	rc = read_platform_resources(core, pdev);
	if (rc) {
		d_vpr_e("Failed to get platform resources\n");
		return rc;
	}

	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->lock);
	mutex_init(&core->resources.cb_lock);

	core->state = VIDC_CORE_UNINIT;
	for (i = SYS_MSG_INDEX(SYS_MSG_START);
		i <= SYS_MSG_INDEX(SYS_MSG_END); i++) {
		init_completion(&core->completions[i]);
	}

	INIT_DELAYED_WORK(&core->fw_unload_work, msm_vidc_fw_unload_handler);
	INIT_WORK(&core->ssr_work, msm_vidc_ssr_handler);

	msm_vidc_init_core_clk_ops(core);
	return rc;
}

static ssize_t link_name_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct msm_vidc_core *core = dev_get_drvdata(dev);

	if (core)
		if (dev == &core->vdev[MSM_VIDC_DECODER].vdev.dev)
			return sysfs_emit(buf, "venus_dec");
		else if (dev == &core->vdev[MSM_VIDC_ENCODER].vdev.dev)
			return sysfs_emit(buf, "venus_enc");
		else
			return 0;
	else
		return 0;
}

static DEVICE_ATTR_RO(link_name);

static ssize_t pwr_collapse_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long val = 0;
	int rc = 0;
	struct msm_vidc_core *core = NULL;

	rc = kstrtoul(buf, 0, &val);
	if (rc)
		return rc;
	else if (!val)
		return -EINVAL;

	core = get_vidc_core(MSM_VIDC_CORE_VENUS);
	if (!core)
		return -EINVAL;
	core->resources.msm_vidc_pwr_collapse_delay = val;
	return count;
}

static ssize_t pwr_collapse_delay_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct msm_vidc_core *core = NULL;

	core = get_vidc_core(MSM_VIDC_CORE_VENUS);
	if (!core)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n",
		core->resources.msm_vidc_pwr_collapse_delay);
}

static DEVICE_ATTR_RW(pwr_collapse_delay);

static ssize_t thermal_level_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "%d\n", vidc_driver->thermal_level);
}

static ssize_t thermal_level_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int rc = 0, val = 0;

	rc = kstrtoint(buf, 0, &val);
	if (rc || val < 0) {
		d_vpr_e("Invalid thermal level value: %s\n", buf);
		return -EINVAL;
	}
	d_vpr_h("Thermal level old %d new %d\n",
			vidc_driver->thermal_level, val);

	if (val == vidc_driver->thermal_level)
		return count;
	vidc_driver->thermal_level = val;

	msm_comm_handle_thermal_event();
	return count;
}

static DEVICE_ATTR_RW(thermal_level);

static ssize_t sku_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d",
			vidc_driver->sku_version);
}

static DEVICE_ATTR_RO(sku_version);

static struct attribute *msm_vidc_core_attrs[] = {
		&dev_attr_pwr_collapse_delay.attr,
		&dev_attr_thermal_level.attr,
		&dev_attr_sku_version.attr,
		NULL
};

static struct attribute_group msm_vidc_core_attr_group = {
		.attrs = msm_vidc_core_attrs,
};

static const struct of_device_id msm_vidc_dt_match[] = {
	{.compatible = "qcom,msm-vidc"},
	{.compatible = "qcom,msm-vidc,context-bank"},
	{.compatible = "qcom,msm-vidc,mem-cdsp"},
	{}
};

static int msm_vidc_register_video_device(enum session_type sess_type,
		int nr, struct msm_vidc_core *core, struct device *dev)
{
	int rc = 0;

	core->vdev[sess_type].vdev.release =
		msm_vidc_release_video_device;
	core->vdev[sess_type].vdev.fops = &msm_v4l2_vidc_fops;
	core->vdev[sess_type].vdev.ioctl_ops = &msm_v4l2_ioctl_ops;
	core->vdev[sess_type].vdev.vfl_dir = VFL_DIR_M2M;
	core->vdev[sess_type].type = sess_type;
	core->vdev[sess_type].vdev.v4l2_dev = &core->v4l2_dev;
	core->vdev[sess_type].vdev.device_caps =
		V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE |
		V4L2_CAP_STREAMING;
	rc = video_register_device(&core->vdev[sess_type].vdev,
					VFL_TYPE_VIDEO, nr);
	if (rc) {
		d_vpr_e("Failed to register the video device\n");
		return rc;
	}
	video_set_drvdata(&core->vdev[sess_type].vdev, core);
	dev = &core->vdev[sess_type].vdev.dev;
	rc = device_create_file(dev, &dev_attr_link_name);
	if (rc) {
		d_vpr_e("Failed to create video device file\n");
		video_unregister_device(&core->vdev[sess_type].vdev);
		return rc;
	}
	return 0;
}
/*
 * redwood: first stage init loads this module, so sku_version exists before
 * the SELinux policy and keeps the plain sysfs label until ueventd's
 * coldboot restorecon reaches it. init.qti.media.sh reads it at early-init,
 * while coldboot still runs; it was denied, picked the yupik v0 media
 * profiles that redwood doesn't ship, and zygote aborted. Give the node now
 * the label the QTI file_contexts give it, as restorecon would; SELinux
 * takes it from the xattr once the policy loads. First stage init has
 * mounted /sys by the time it loads modules.
 */
static void msm_vidc_label_sku_version(struct device *dev)
{
	static const char ctx[] = "u:object_r:vendor_sysfs_sku:s0";
	char *kpath, *spath;
	struct path path;
	int rc;

	kpath = kobject_get_path(&dev->kobj, GFP_KERNEL);
	if (!kpath)
		return;
	spath = kasprintf(GFP_KERNEL, "/sys%s/sku_version", kpath);
	kfree(kpath);
	if (!spath)
		return;

	rc = kern_path(spath, 0, &path);
	if (!rc) {
		rc = vfs_setxattr(&nop_mnt_idmap, path.dentry,
				  XATTR_NAME_SELINUX, ctx, sizeof(ctx), 0);
		path_put(&path);
	}
	if (rc)
		d_vpr_e("Failed to label %s: %d\n", spath, rc);
	kfree(spath);
}

static int msm_vidc_probe_vidc_device(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_vidc_core *core;
	struct device *dev = NULL;
	int nr = BASE_DEVICE_NUMBER;

	if (!vidc_driver) {
		d_vpr_e("Invalid vidc driver\n");
		return -EINVAL;
	}

	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->platform_data = vidc_get_drv_data(&pdev->dev);
	dev_set_drvdata(&pdev->dev, core);
	rc = msm_vidc_initialize_core(pdev, core);
	if (rc) {
		d_vpr_e("Failed to init core\n");
		goto err_core_init;
	}
	rc = sysfs_create_group(&pdev->dev.kobj, &msm_vidc_core_attr_group);
	if (rc) {
		d_vpr_e("Failed to create attributes\n");
		goto err_core_init;
	}
	msm_vidc_label_sku_version(&pdev->dev);

	core->id = MSM_VIDC_CORE_VENUS;

	rc = v4l2_device_register(&pdev->dev, &core->v4l2_dev);
	if (rc) {
		d_vpr_e("Failed to register v4l2 device\n");
		goto err_v4l2_register;
	}

	/* setup the decoder device */
	rc = msm_vidc_register_video_device(MSM_VIDC_DECODER,
			nr, core, dev);
	if (rc) {
		d_vpr_e("Failed to register video decoder\n");
		goto err_dec;
	}

	/* setup the encoder device */
	rc = msm_vidc_register_video_device(MSM_VIDC_ENCODER,
			nr + 1, core, dev);
	if (rc) {
		d_vpr_e("Failed to register video encoder\n");
		goto err_enc;
	}

	/* finish setting up the 'core' */
	mutex_lock(&vidc_driver->lock);
	if (vidc_driver->num_cores  + 1 > MSM_VIDC_CORES_MAX) {
		mutex_unlock(&vidc_driver->lock);
		d_vpr_e("Maximum cores already exist, core_no = %d\n",
			vidc_driver->num_cores);
		goto err_cores_exceeded;
	}
	vidc_driver->num_cores++;
	mutex_unlock(&vidc_driver->lock);

	core->device = vidc_hfi_initialize(core->hfi_type, core->id,
				&core->resources, &handle_cmd_response);
	if (IS_ERR_OR_NULL(core->device)) {
		mutex_lock(&vidc_driver->lock);
		vidc_driver->num_cores--;
		mutex_unlock(&vidc_driver->lock);

		rc = PTR_ERR(core->device) ?
			PTR_ERR(core->device) : -EBADHANDLE;
		if (rc != -EPROBE_DEFER)
			d_vpr_e("Failed to create HFI device\n");
		else
			d_vpr_h("msm_vidc: request probe defer\n");
		goto err_cores_exceeded;
	}

	core->vidc_core_workq = create_singlethread_workqueue(
			"vidc_core_workq");
	if (!core->vidc_core_workq) {
		d_vpr_e("%s: create core workq failed\n", __func__);
		goto err_core_workq;
	}
	mutex_lock(&vidc_driver->lock);
	list_add_tail(&core->list, &vidc_driver->cores);
	mutex_unlock(&vidc_driver->lock);

	core->debugfs_root = msm_vidc_debugfs_init_core(
		core, vidc_driver->debugfs_root);

	vidc_driver->sku_version = core->resources.sku_version;

	d_vpr_h("populating sub devices\n");
	/*
	 * Trigger probe for each sub-device i.e. qcom,msm-vidc,context-bank.
	 * When msm_vidc_probe is called for each sub-device, parse the
	 * context-bank details and store it in core->resources.context_banks
	 * list.
	 */
	rc = of_platform_populate(pdev->dev.of_node, msm_vidc_dt_match, NULL,
			&pdev->dev);
	if (rc) {
		d_vpr_e("Failed to trigger probe for sub-devices\n");
		goto err_fail_sub_device_probe;
	}

	return rc;

err_fail_sub_device_probe:
	if (core->vidc_core_workq)
		destroy_workqueue(core->vidc_core_workq);
err_core_workq:
	vidc_hfi_deinitialize(core->hfi_type, core->device);
err_cores_exceeded:
	device_remove_file(&core->vdev[MSM_VIDC_ENCODER].vdev.dev,
			&dev_attr_link_name);
	video_unregister_device(&core->vdev[MSM_VIDC_ENCODER].vdev);
err_enc:
	device_remove_file(&core->vdev[MSM_VIDC_DECODER].vdev.dev,
			&dev_attr_link_name);
	video_unregister_device(&core->vdev[MSM_VIDC_DECODER].vdev);
err_dec:
	v4l2_device_unregister(&core->v4l2_dev);
err_v4l2_register:
	sysfs_remove_group(&pdev->dev.kobj, &msm_vidc_core_attr_group);
err_core_init:
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(core);
	return rc;
}

static int msm_vidc_probe_mem_cdsp(struct platform_device *pdev)
{
	return read_mem_cdsp_resources_from_dt(pdev);
}

static int msm_vidc_probe_context_bank(struct platform_device *pdev)
{
	return read_context_bank_resources_from_dt(pdev);
}

static int msm_vidc_probe(struct platform_device *pdev)
{
	/*
	 * Sub devices probe will be triggered by of_platform_populate() towards
	 * the end of the probe function after msm-vidc device probe is
	 * completed. Return immediately after completing sub-device probe.
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,msm-vidc")) {
		return msm_vidc_probe_vidc_device(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-vidc,context-bank")) {
		return msm_vidc_probe_context_bank(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-vidc,mem-cdsp")) {
		return msm_vidc_probe_mem_cdsp(pdev);
	}

	/* How did we end up here? */
	MSM_VIDC_ERROR(1);
	return -EINVAL;
}

static int msm_vidc_remove(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_vidc_core *core;

	if (!pdev) {
		d_vpr_e("%s: invalid input %pK", __func__, pdev);
		return -EINVAL;
	}

	core = dev_get_drvdata(&pdev->dev);
	if (!core) {
		d_vpr_e("%s: invalid core", __func__);
		return -EINVAL;
	}

	if (core->vidc_core_workq)
		destroy_workqueue(core->vidc_core_workq);
	vidc_hfi_deinitialize(core->hfi_type, core->device);
	device_remove_file(&core->vdev[MSM_VIDC_ENCODER].vdev.dev,
				&dev_attr_link_name);
	video_unregister_device(&core->vdev[MSM_VIDC_ENCODER].vdev);
	device_remove_file(&core->vdev[MSM_VIDC_DECODER].vdev.dev,
				&dev_attr_link_name);
	video_unregister_device(&core->vdev[MSM_VIDC_DECODER].vdev);
	v4l2_device_unregister(&core->v4l2_dev);

	msm_vidc_free_platform_resources(&core->resources);
	sysfs_remove_group(&pdev->dev.kobj, &msm_vidc_core_attr_group);
	dev_set_drvdata(&pdev->dev, NULL);
	mutex_destroy(&core->resources.cb_lock);
	mutex_destroy(&core->lock);
	kfree(core);
	return rc;
}

static void msm_vidc_remove_612(struct platform_device *pdev)
{
	msm_vidc_remove(pdev);
}

static int msm_vidc_pm_suspend(struct device *dev)
{
	int rc = 0;
	struct msm_vidc_core *core;

	/*
	 * Bail out if
	 * - driver possibly not probed yet
	 * - not the main device. We don't support power management on
	 *   subdevices (e.g. context banks)
	 */
	if (!dev || !dev->driver ||
		!of_device_is_compatible(dev->of_node, "qcom,msm-vidc"))
		return 0;

	core = dev_get_drvdata(dev);
	if (!core) {
		d_vpr_e("%s: invalid core\n", __func__);
		return -EINVAL;
	}

	rc = msm_vidc_suspend(core->id);
	if (rc == -ENOTSUPP)
		rc = 0;
	else if (rc)
		d_vpr_e("Failed to suspend: %d\n", rc);


	return rc;
}

static int msm_vidc_pm_resume(struct device *dev)
{
	d_vpr_h("%s\n", __func__);
	return 0;
}

static const struct dev_pm_ops msm_vidc_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(msm_vidc_pm_suspend, msm_vidc_pm_resume)
};

MODULE_DEVICE_TABLE(of, msm_vidc_dt_match);

static struct platform_driver msm_vidc_driver = {
	.probe = msm_vidc_probe,
	.remove = msm_vidc_remove_612,
	.driver = {
		.name = "msm_vidc_v4l2",
		.of_match_table = msm_vidc_dt_match,
		.pm = pm_sleep_ptr(&msm_vidc_pm_ops),
	},
};

static int __init msm_vidc_init(void)
{
	int rc = 0;

	vidc_driver = kzalloc(sizeof(*vidc_driver),
						GFP_KERNEL);
	if (!vidc_driver) {
		d_vpr_e("Failed to allocate memroy for msm_vidc_drv\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vidc_driver->cores);
	mutex_init(&vidc_driver->lock);
	vidc_driver->debugfs_root = msm_vidc_debugfs_init_drv();
	if (!vidc_driver->debugfs_root)
		d_vpr_e("Failed to create debugfs for msm_vidc\n");

	rc = platform_driver_register(&msm_vidc_driver);
	if (rc) {
		d_vpr_e("Failed to register platform driver\n");
		debugfs_remove_recursive(vidc_driver->debugfs_root);
		kfree(vidc_driver);
		vidc_driver = NULL;
	}

	return rc;
}

static void __exit msm_vidc_exit(void)
{
	platform_driver_unregister(&msm_vidc_driver);
	debugfs_remove_recursive(vidc_driver->debugfs_root);
	mutex_destroy(&vidc_driver->lock);
	kfree(vidc_driver);
	vidc_driver = NULL;
}

module_init(msm_vidc_init);
module_exit(msm_vidc_exit);

MODULE_SOFTDEP("pre: subsys-pil-tz");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");
