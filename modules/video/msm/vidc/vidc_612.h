/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * What the lahaina msm_vidc needs on 6.12 that 5.4 had elsewhere: venus
 * booted with the MDT loader and PAS instead of PIL, the DDR type from the
 * device tree, and the msm SCM calls through their mainline versions.
 * Buffers come straight from the qcom DMA-BUF heaps (msm_smem.c).
 */
#ifndef VIDC_612_H
#define VIDC_612_H

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <soc/qcom/of_common.h>

/* LPDDR4X (7) when the device tree doesn't say */
static inline int vidc_ddr_type(void)
{
	int type = of_fdt_get_ddrtype();

	return type > 0 ? type : 0x7;
}

/*
 * DMA_ATTR_DELAYED_UNMAP keeps one IOVA per buffer for the buffer's life,
 * as 5.4's lazy mapping did. The driver relies on it: venus names reference
 * frames by device address, and the release-buffer-reference handling finds
 * the buffer by that address. Without it every queue maps the buffer at a
 * new address, references are never released and playback starves.
 */
#include <linux/qcom-dma-mapping.h>
#ifndef DMA_ATTR_IOMMU_USE_UPSTREAM_HINT
#define DMA_ATTR_IOMMU_USE_UPSTREAM_HINT 0
#endif

/* the venus PAS id */
#define VIDC_PAS_ID	9

void *vidc_fw_load(struct device *dev, const char *fw_name);
void vidc_fw_unload(void *cookie);

static inline int qcom_scm_mem_protect_video(u32 cp_start, u32 cp_size,
					     u32 cp_nonpixel_start, u32 cp_nonpixel_size)
{
	return qcom_scm_mem_protect_video_var(cp_start, cp_size,
					      cp_nonpixel_start, cp_nonpixel_size);
}

#endif
