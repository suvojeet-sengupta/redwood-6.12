/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Builds redwood's 5.4 lahaina camera driver (LineageOS sm8350
 * techpack/camera) on 6.12: the kernel API that changed under it.
 */
#ifndef CAM_COMPAT_612_H
#define CAM_COMPAT_612_H

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/dma-map-ops.h>
#include <linux/io.h>

/* strlcpy() is gone; callers here never use its return value */
#define strlcpy(d, s, n) strscpy(d, s, n)

/* timestamps in the driver's own structs */
#define timeval __kernel_old_timeval

#define VFL_TYPE_GRABBER VFL_TYPE_VIDEO

/* the msm _no_log register accessors were never upstream */
#define writel_relaxed_no_log(v, a) writel_relaxed(v, a)
#define readl_relaxed_no_log(a) readl_relaxed(a)
#define __raw_writel_no_log(v, a) __raw_writel(v, a)
#define __raw_readl_no_log(a) __raw_readl(a)


#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interconnect.h>

/* of_get_gpio() and gpio_free_array() were removed */
#define of_get_gpio(np, index) of_get_named_gpio(np, "gpios", index)
static inline void cam_gpio_free_array(const struct gpio *array, size_t num)
{
	while (num--)
		gpio_free((array++)->gpio);
}
#define gpio_free_array(a, n) cam_gpio_free_array(a, n)

/* redwood has no WLED flash; the qpnp wled flash hook doesn't exist */
#define wled_flash_led_prepare(trigger, options, max_current) (-ENODEV)

/*
 * 5.4's icc_get(dev, src_id, dst_id) is gone. The ids come from the
 * "interconnects" pair of a known node, so get the path from that node.
 */
static inline struct icc_path *cam_icc_get_np(struct device_node *np)
{
	struct device *dev;
	struct icc_path *path;

	if (!np)
		return ERR_PTR(-ENODEV);
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	device_initialize(dev);
	dev->of_node = np;
	dev_set_name(dev, "cam-icc-%pOFn", np);
	path = of_icc_get(dev, NULL);	/* index 0; of_icc_get_by_index() isn't exported */
	put_device(dev);
	return path;
}

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/iosys-map.h>
#include <linux/list_sort.h>
#include <linux/qcom-iommu-util.h>
#include <soc/qcom/of_common.h>
#include <linux/msm_ion.h>
#ifndef ION_FLAG_CACHED
#define ION_FLAG_CACHED 1
#endif
#define of_gpio_count(np) of_count_phandle_with_args(np, "gpios", "#gpio-cells")

#define kzfree(p) kfree_sensitive(p)
#define V4L2_SUBDEV_NAME_SIZE 32

/* the qcom IOMMU fault ids are prefixed on 6.12 */
#define iommu_fault_ids qcom_iommu_fault_ids
#define iommu_get_fault_ids(domain, ids) qcom_iommu_get_fault_ids(domain, ids)

/*
 * 6.12 has no ION: allocate camera buffers from the DMA-BUF heap the msm
 * ION heap mask and flags stand for, as modules/ioncompat does.
 */
static inline struct dma_buf *cam_heap_alloc(const char *name, size_t len)
{
	struct dma_heap *heap = dma_heap_find(name);
	struct dma_buf *buf;

	if (!heap)
		return ERR_PTR(-ENODEV);
	buf = dma_heap_buffer_alloc(heap, len, O_RDWR | O_CLOEXEC, 0);
	dma_heap_put(heap);
	return buf;
}

static inline struct dma_buf *cam_ion_alloc(size_t len, unsigned int mask,
					    unsigned int flags)
{
	struct dma_buf *buf;

	if (flags & ION_FLAG_SECURE)
		return cam_heap_alloc("qcom,secure-pixel", len);
	buf = cam_heap_alloc((flags & ION_FLAG_CACHED) ? "qcom,system" :
			     "qcom,system-uncached", len);
	if (IS_ERR(buf))
		buf = cam_heap_alloc("system", len);
	return buf;
}
#define ion_alloc(len, mask, flags) cam_ion_alloc(len, mask, flags)

/* dma_buf_vmap() takes an iosys_map and wants the reservation lock now */
static inline void *cam_dma_buf_vmap(struct dma_buf *buf)
{
	struct iosys_map map;

	if (dma_buf_vmap_unlocked(buf, &map))
		return NULL;
	return map.vaddr;
}

static inline void cam_dma_buf_vunmap(struct dma_buf *buf, void *vaddr)
{
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(vaddr);

	dma_buf_vunmap_unlocked(buf, &map);
}

#endif
