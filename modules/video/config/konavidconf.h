/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#define CONFIG_MSM_VIDC_V4L2 1

/* 6.12 only keeps these aliases for userspace */
#define V4L2_CTRL_CLASS_MPEG		V4L2_CTRL_CLASS_CODEC
#define V4L2_CID_MPEG_CLASS		V4L2_CID_CODEC_CLASS
#define V4L2_CID_MPEG_BASE		V4L2_CID_CODEC_BASE
#define V4L2_CID_MPEG_CX2341X_BASE	V4L2_CID_CODEC_CX2341X_BASE
#define V4L2_CID_MPEG_MFC51_BASE	V4L2_CID_CODEC_MFC51_BASE
