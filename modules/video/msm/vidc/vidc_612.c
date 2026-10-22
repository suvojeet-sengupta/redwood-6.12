// SPDX-License-Identifier: GPL-2.0-only
/*
 * See vidc_612.h.
 */
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "vidc_612.h"

static const char *heap_name(unsigned int heap_mask, unsigned int flags)
{
	if ((heap_mask & (ION_SECURE_HEAP_ID | ION_CP_MM_HEAP_ID)) || (flags & ION_FLAG_SECURE))
		return (flags & ION_FLAG_CP_PIXEL) ? "qcom,secure-pixel" : "qcom,secure-non-pixel";
	if (heap_mask & ION_ADSP_HEAP_ID)
		return "qcom,adsp";
	return (flags & ION_FLAG_CACHED) ? "qcom,system" : "qcom,system-uncached";
}

struct dma_buf *ion_alloc(size_t len, unsigned int heap_mask, unsigned int flags)
{
	const char *name = heap_name(heap_mask, flags);
	struct dma_heap *heap = dma_heap_find(name);
	struct dma_buf *buf;

	if (!heap)
		return ERR_PTR(-ENODEV);
	buf = dma_heap_buffer_alloc(heap, len, O_RDWR, 0);
	dma_heap_put(heap);
	if (IS_ERR(buf))
		pr_err("msm_vidc: %zu bytes from %s failed: %ld\n", len, name, PTR_ERR(buf));
	return buf;
}

struct vidc_fw {
	struct device *dev;
	void *va;
	size_t size;
};

void *vidc_fw_load(struct device *dev, const char *fw_name)
{
	struct device_node *np;
	struct reserved_mem *rmem;
	const struct firmware *fw;
	struct vidc_fw *vfw;
	char path[64];
	phys_addr_t reloc;
	int ret;

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	rmem = np ? of_reserved_mem_lookup(np) : NULL;
	of_node_put(np);
	if (!rmem) {
		dev_err(dev, "no memory-region for the venus firmware\n");
		return ERR_PTR(-EINVAL);
	}

	vfw = kzalloc(sizeof(*vfw), GFP_KERNEL);
	if (!vfw)
		return ERR_PTR(-ENOMEM);
	vfw->dev = dev;
	vfw->size = rmem->size;
	vfw->va = memremap(rmem->base, rmem->size, MEMREMAP_WC);
	if (!vfw->va) {
		ret = -ENOMEM;
		goto free;
	}

	snprintf(path, sizeof(path), "%s.mdt", fw_name);
	ret = request_firmware(&fw, path, dev);
	if (ret)
		goto unmap;
	ret = qcom_mdt_load(dev, fw, path, VIDC_PAS_ID, vfw->va, rmem->base,
			    rmem->size, &reloc);
	release_firmware(fw);
	if (ret) {
		dev_err(dev, "loading %s failed: %d\n", path, ret);
		goto unmap;
	}
	ret = qcom_scm_pas_auth_and_reset(VIDC_PAS_ID);
	if (ret) {
		dev_err(dev, "booting venus failed: %d\n", ret);
		goto unmap;
	}
	dev_info(dev, "venus up from %s\n", path);
	return vfw;

unmap:
	memunmap(vfw->va);
free:
	kfree(vfw);
	return ERR_PTR(ret);
}

void vidc_fw_unload(void *cookie)
{
	struct vidc_fw *vfw = cookie;

	if (IS_ERR_OR_NULL(vfw))
		return;
	qcom_scm_pas_shutdown(VIDC_PAS_ID);
	memunmap(vfw->va);
	kfree(vfw);
}
