/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The 5.4 interfaces the lahaina msm_vidc uses, on 6.12: ION allocations
 * from the qcom DMA-BUF heaps, venus booted with the MDT loader and PAS
 * instead of PIL, and the msm SCM calls through their mainline versions.
 */
#ifndef VIDC_612_H
#define VIDC_612_H

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/firmware/qcom/qcom_scm.h>

#define ION_BIT(nr)			(1U << (nr))
#define ION_HEAP(bit)			(bit)
#define ION_ADSP_HEAP_ID		ION_BIT(4)
#define ION_CP_MM_HEAP_ID		ION_BIT(9)
#define ION_SECURE_HEAP_ID		ION_BIT(10)
#define ION_SYSTEM_HEAP_ID		ION_BIT(25)

#define ION_FLAG_CACHED			1
#define ION_FLAG_CP_BITSTREAM		ION_BIT(18)
#define ION_FLAG_CP_PIXEL		ION_BIT(19)
#define ION_FLAG_CP_NON_PIXEL		ION_BIT(20)
#define ION_FLAG_SECURE			ION_BIT(31)

/* redwood has LPDDR4X; 6.12 has no msm DDR type lookup */
#define vidc_ddr_type()	0x7

struct ion_prefetch_region {
	u64 size;
	u32 vmid;
};

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

struct dma_buf *ion_alloc(size_t len, unsigned int heap_mask, unsigned int flags);

static inline int msm_ion_heap_prefetch(unsigned int heap_id, void *regions, int nr)
{
	return 0;
}

static inline int msm_ion_heap_drain(unsigned int heap_id, void *regions, int nr)
{
	return 0;
}

static inline int subsystem_crashed(const char *name)
{
	pr_err("msm_vidc: %s crashed\n", name);
	return 0;
}

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
