// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-heap.h>
#include <linux/qcom_dma_heap.h>
#include <linux/dma-buf.h>
#include "vidc_612.h"
#include "msm_vidc.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_resources.h"

static int msm_dma_get_device_address(struct dma_buf *dbuf, unsigned long align,
	dma_addr_t *iova, unsigned long *buffer_size,
	unsigned long flags, enum hal_buffer buffer_type,
	unsigned long session_type, struct msm_vidc_platform_resources *res,
	struct dma_mapping_info *mapping_info, u32 sid)
{
	int rc = 0;
	struct dma_buf_attachment *attach;
	struct sg_table *table = NULL;
	struct context_bank_info *cb = NULL;

	if (!dbuf || !iova || !buffer_size || !mapping_info) {
		s_vpr_e(sid, "%s: invalid params: %pK, %pK, %pK, %pK\n",
			__func__, dbuf, iova, buffer_size, mapping_info);
		return -EINVAL;
	}

	if (is_iommu_present(res)) {
		cb = msm_smem_get_context_bank(
				session_type, (flags & SMEM_SECURE),
				res, buffer_type, sid);
		if (!cb) {
			s_vpr_e(sid, "%s: Failed to get context bank device\n",
				 __func__);
			rc = -EIO;
			goto mem_map_failed;
		}

		/* Check if the dmabuf size matches expected size */
		if (dbuf->size < *buffer_size) {
			rc = -EINVAL;
			s_vpr_e(sid,
				"Size mismatch: Dmabuf size: %zu Expected Size: %lu",
				dbuf->size, *buffer_size);
			msm_vidc_res_handle_fatal_hw_error(res,
					true);
			goto mem_buf_size_mismatch;
		}

		/* Prepare a dma buf for dma on the given device */
		attach = dma_buf_attach(dbuf, cb->dev);
		if (IS_ERR_OR_NULL(attach)) {
			rc = PTR_ERR(attach) ? PTR_ERR(attach) : -ENOMEM;
			s_vpr_e(sid, "Failed to attach dmabuf\n");
			goto mem_buf_attach_failed;
		}

		/*
		 * Get the scatterlist for the given attachment
		 * Mapping of sg is taken care by map attachment
		 */
		attach->dma_map_attrs = DMA_ATTR_DELAYED_UNMAP;
		/*
		 * We do not need dma_map function to perform cache operations
		 * on the whole buffer size and hence pass skip sync flag.
		 * We do the required cache operations separately for the
		 * required buffer size
		 */
		attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
		if (res->sys_cache_present)
			attach->dma_map_attrs |=
				DMA_ATTR_IOMMU_USE_UPSTREAM_HINT;

		table = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(table)) {
			rc = PTR_ERR(table) ? PTR_ERR(table) : -ENOMEM;
			s_vpr_e(sid, "Failed to map table\n");
			goto mem_map_table_failed;
		}

		/* debug trace's need to be updated later */
		trace_msm_smem_buffer_iommu_op_start("MAP", 0, 0,
			align, *iova, *buffer_size);

		if (table->sgl) {
			*iova = table->sgl->dma_address;
			*buffer_size = table->sgl->dma_length;
		} else {
			s_vpr_e(sid, "sgl is NULL\n");
			rc = -ENOMEM;
			goto mem_map_sg_failed;
		}

		mapping_info->dev = cb->dev;
		mapping_info->domain = cb->domain;
		mapping_info->table = table;
		mapping_info->attach = attach;
		mapping_info->buf = dbuf;
		mapping_info->cb_info = (void *)cb;

		trace_msm_smem_buffer_iommu_op_end("MAP", 0, 0,
			align, *iova, *buffer_size);
	} else {
		s_vpr_h(sid, "iommu not present, use phys mem addr\n");
	}

	return 0;
mem_map_sg_failed:
	dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
mem_map_table_failed:
	dma_buf_detach(dbuf, attach);
mem_buf_size_mismatch:
mem_buf_attach_failed:
mem_map_failed:
	return rc;
}

static int msm_dma_put_device_address(u32 flags,
	struct dma_mapping_info *mapping_info,
	enum hal_buffer buffer_type, u32 sid)
{
	int rc = 0;
	struct sg_table *table = NULL;
	dma_addr_t iova;
	unsigned long buffer_size;

	if (!mapping_info) {
		s_vpr_e(sid, "Invalid mapping_info\n");
		return -EINVAL;
	}

	if (!mapping_info->dev || !mapping_info->table ||
		!mapping_info->buf || !mapping_info->attach ||
		!mapping_info->cb_info) {
		s_vpr_e(sid, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	table = mapping_info->table;
	iova = table->sgl->dma_address;
	buffer_size = table->sgl->dma_length;
	trace_msm_smem_buffer_iommu_op_start("UNMAP", 0, 0,
			0, iova, buffer_size);
	dma_buf_unmap_attachment(mapping_info->attach,
		mapping_info->table, DMA_BIDIRECTIONAL);
	dma_buf_detach(mapping_info->buf, mapping_info->attach);
	trace_msm_smem_buffer_iommu_op_end("UNMAP", 0, 0, 0, 0, 0);

	mapping_info->dev = NULL;
	mapping_info->domain = NULL;
	mapping_info->table = NULL;
	mapping_info->attach = NULL;
	mapping_info->buf = NULL;
	mapping_info->cb_info = NULL;

	return rc;
}

/*
 * 6.12's DMA-BUF heaps have no get_flags, so every buffer read as uncached
 * and non-secure: the CPU cache was never cleaned before the video core
 * read a bitstream the codec had just written, and decoded frames came out
 * as blocks of garbage. Work the qcom heap flags out from the heap the
 * buffer came from.
 */
static int vidc_dma_buf_flags(struct dma_buf *dbuf, unsigned long *flags)
{
	int rc = dma_buf_get_flags(dbuf, flags);

	if (rc || *flags || !dbuf->exp_name)
		return rc;
	if (!strcmp(dbuf->exp_name, "qcom,system") ||
	    !strcmp(dbuf->exp_name, "system"))
		*flags = QCOM_DMA_HEAP_FLAG_CACHED;
	else if (!strncmp(dbuf->exp_name, "qcom,secure-", 12))
		*flags = QCOM_DMA_HEAP_FLAG_SECURE;
	return 0;
}

struct dma_buf *msm_smem_get_dma_buf(int fd, u32 sid)
{
	struct dma_buf *dma_buf;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dma_buf)) {
		s_vpr_e(sid, "Failed to get dma_buf for %d, error %ld\n",
				fd, PTR_ERR(dma_buf));
		dma_buf = NULL;
	}

	return dma_buf;
}

void msm_smem_put_dma_buf(void *dma_buf, u32 sid)
{
	if (!dma_buf) {
		s_vpr_e(sid, "%s: NULL dma_buf\n", __func__);
		return;
	}

	dma_buf_put((struct dma_buf *)dma_buf);
}

int msm_smem_map_dma_buf(struct msm_vidc_inst *inst, struct msm_smem *smem)
{
	int rc = 0;

	dma_addr_t iova = 0;
	u32 temp = 0;
	unsigned long buffer_size = 0;
	unsigned long align = SZ_4K;
	struct dma_buf *dbuf;
	unsigned long heap_flags = 0;
	u32 b_type = HAL_BUFFER_INPUT | HAL_BUFFER_OUTPUT | HAL_BUFFER_OUTPUT2;

	if (!inst || !smem) {
		d_vpr_e("%s: invalid params: %pK %pK\n",
				__func__, inst, smem);
		rc = -EINVAL;
		goto exit;
	}

	if (smem->refcount) {
		smem->refcount++;
		goto exit;
	}

	dbuf = msm_smem_get_dma_buf(smem->fd, inst->sid);
	if (!dbuf) {
		rc = -EINVAL;
		goto exit;
	}

	smem->dma_buf = dbuf;

	rc = vidc_dma_buf_flags(dbuf, &heap_flags);
	if (rc) {
		s_vpr_e(inst->sid, "Failed to get dma buf flags: %d\n", rc);
		goto fail_map_dma_buf;
	}
	if (heap_flags & QCOM_DMA_HEAP_FLAG_CACHED)
		smem->flags |= SMEM_CACHED;

	if (heap_flags & QCOM_DMA_HEAP_FLAG_SECURE)
		smem->flags |= SMEM_SECURE;

	if ((smem->buffer_type & b_type) &&
		!!(smem->flags & SMEM_SECURE) ^ !!(inst->flags & VIDC_SECURE)) {
		s_vpr_e(inst->sid, "Failed to map %s buffer with %s session\n",
			smem->flags & SMEM_SECURE ? "secure" : "non-secure",
			inst->flags & VIDC_SECURE ? "secure" : "non-secure");
		rc = -EINVAL;
		goto fail_map_dma_buf;
	}
	buffer_size = smem->size;

	rc = msm_dma_get_device_address(dbuf, align, &iova, &buffer_size,
			smem->flags, smem->buffer_type,	inst->session_type,
			&(inst->core->resources), &smem->mapping_info,
			inst->sid);
	if (rc) {
		s_vpr_e(inst->sid, "Failed to get device address: %d\n", rc);
		goto fail_map_dma_buf;
	}
	temp = (u32)iova;
	if ((dma_addr_t)temp != iova) {
		s_vpr_e(inst->sid, "iova(%pa) truncated to %#x", &iova, temp);
		rc = -EINVAL;
		goto fail_iova_truncation;
	}

	smem->device_addr = (u32)iova + smem->offset;

	smem->refcount++;
	return 0;

fail_iova_truncation:
	msm_dma_put_device_address(smem->flags, &smem->mapping_info,
			smem->buffer_type, inst->sid);
fail_map_dma_buf:
	msm_smem_put_dma_buf(dbuf, inst->sid);
exit:
	return rc;
}

int msm_smem_unmap_dma_buf(struct msm_vidc_inst *inst, struct msm_smem *smem)
{
	int rc = 0;

	if (!inst || !smem) {
		d_vpr_e("%s: invalid params: %pK %pK\n",
				__func__, inst, smem);
		rc = -EINVAL;
		goto exit;
	}

	if (smem->refcount) {
		smem->refcount--;
	} else {
		s_vpr_e(inst->sid,
			"unmap called while refcount is zero already\n");
		return -EINVAL;
	}

	if (smem->refcount)
		goto exit;

	rc = msm_dma_put_device_address(smem->flags, &smem->mapping_info,
		smem->buffer_type, inst->sid);
	if (rc) {
		s_vpr_e(inst->sid, "Failed to put device address: %d\n", rc);
		goto exit;
	}

	msm_smem_put_dma_buf(smem->dma_buf, inst->sid);

	smem->device_addr = 0x0;
	smem->dma_buf = NULL;

exit:
	return rc;
}

/*
 * The secure heap a buffer type comes from: pixel buffers from the pixel
 * heap, bitstream and everything else from the non-pixel one.
 */
static const char *secure_heap_for_buffer_type(
	u32 session_type, enum hal_buffer buffer_type)
{
	switch (buffer_type) {
	case HAL_BUFFER_INPUT:
		return session_type == MSM_VIDC_ENCODER ?
			"qcom,secure-pixel" : "qcom,secure-non-pixel";
	case HAL_BUFFER_OUTPUT:
	case HAL_BUFFER_OUTPUT2:
		return session_type == MSM_VIDC_ENCODER ?
			"qcom,secure-non-pixel" : "qcom,secure-pixel";
	case HAL_BUFFER_INTERNAL_SCRATCH_2:
		return "qcom,secure-pixel";
	case HAL_BUFFER_INTERNAL_SCRATCH:
	case HAL_BUFFER_INTERNAL_SCRATCH_1:
	case HAL_BUFFER_INTERNAL_PERSIST:
	case HAL_BUFFER_INTERNAL_PERSIST_1:
		return "qcom,secure-non-pixel";
	default:
		WARN(1, "No matching secure heap for buffer type : %x\n",
				buffer_type);
		return NULL;
	}
}

static int alloc_dma_mem(size_t size, u32 align, u32 flags,
	enum hal_buffer buffer_type, int map_kernel,
	struct msm_vidc_platform_resources *res, u32 session_type,
	struct msm_smem *mem, u32 sid)
{
	dma_addr_t iova = 0;
	unsigned long buffer_size = 0;
	const char *heap_name;
	struct dma_heap *heap;
	int rc = 0;
	struct dma_buf *dbuf = NULL;

	if (!res) {
		s_vpr_e(sid, "%s: NULL res\n", __func__);
		return -EINVAL;
	}

	align = ALIGN(align, SZ_4K);
	size = ALIGN(size, SZ_4K);

	if (is_iommu_present(res)) {
		if (flags & SMEM_ADSP) {
			s_vpr_h(sid, "Allocating from ADSP heap\n");
			heap_name = "qcom,adsp";
		} else {
			heap_name = flags & SMEM_CACHED ?
				"qcom,system" : "qcom,system-uncached";
		}
	} else {
		s_vpr_h(sid,
			"allocate shared memory from adsp heap size %zx align %d\n",
			size, align);
		heap_name = "qcom,adsp";
	}

	if ((flags & SMEM_SECURE) ||
		(buffer_type == HAL_BUFFER_INTERNAL_PERSIST &&
		 session_type == MSM_VIDC_ENCODER)) {
		heap_name = secure_heap_for_buffer_type(session_type,
							buffer_type);
		if (!heap_name) {
			rc = -EINVAL;
			goto fail_shared_mem_alloc;
		}

		if (res->slave_side_cp) {
			size = ALIGN(size, SZ_1M);
			align = ALIGN(size, SZ_1M);
		}
		flags |= SMEM_SECURE;
	}

	trace_msm_smem_buffer_dma_op_start("ALLOC", (u32)buffer_type,
		0, size, align, flags, map_kernel);
	heap = dma_heap_find(heap_name);
	if (!heap) {
		s_vpr_e(sid, "No %s heap\n", heap_name);
		rc = -ENODEV;
		goto fail_shared_mem_alloc;
	}
	dbuf = dma_heap_buffer_alloc(heap, size, O_RDWR, 0);
	dma_heap_put(heap);
	if (IS_ERR_OR_NULL(dbuf)) {
		s_vpr_e(sid, "Failed to allocate shared memory = %zx from %s, %#x\n",
		size, heap_name, flags);
		rc = -ENOMEM;
		goto fail_shared_mem_alloc;
	}
	trace_msm_smem_buffer_dma_op_end("ALLOC", (u32)buffer_type,
		0, size, align, flags, map_kernel);

	mem->flags = flags;
	mem->buffer_type = buffer_type;
	mem->offset = 0;
	mem->size = size;
	mem->dma_buf = dbuf;
	mem->kvaddr = NULL;

	rc = msm_dma_get_device_address(dbuf, align, &iova,
			&buffer_size, flags, buffer_type,
			session_type, res, &mem->mapping_info, sid);
	if (rc) {
		s_vpr_e(sid, "Failed to get device address: %d\n",
			rc);
		goto fail_device_address;
	}
	mem->device_addr = (u32)iova;
	if ((dma_addr_t)mem->device_addr != iova) {
		s_vpr_e(sid, "iova(%pa) truncated to %#x",
			&iova, mem->device_addr);
		goto fail_device_address;
	}

	if (map_kernel) {
		dma_buf_begin_cpu_access(dbuf, DMA_BIDIRECTIONAL);
		struct iosys_map map;

		mem->kvaddr = dma_buf_vmap(dbuf, &map) ? NULL : map.vaddr;
		if (!mem->kvaddr) {
			s_vpr_e(sid, "Failed to map shared mem in kernel\n");
			rc = -EIO;
			goto fail_map;
		}
	}

	s_vpr_h(sid,
		"%s: dma_buf = %pK, inode = %lu, ref = %ld, device_addr = %x, size = %d, kvaddr = %pK, buffer_type = %#x, flags = %#lx\n",
		__func__, mem->dma_buf, (dbuf ? file_inode(dbuf->file)->i_ino : -1),
		(dbuf ? file_count(dbuf->file) : -1), mem->device_addr, mem->size,
		mem->kvaddr, mem->buffer_type, mem->flags);
	return rc;

fail_map:
	if (map_kernel)
		dma_buf_end_cpu_access(dbuf, DMA_BIDIRECTIONAL);
fail_device_address:
	dma_buf_put(dbuf);
fail_shared_mem_alloc:
	return rc;
}

static int free_dma_mem(struct msm_smem *mem, u32 sid)
{
	struct dma_buf *dbuf = NULL;

	dbuf = (struct dma_buf *)mem->dma_buf;
	s_vpr_h(sid,
		"%s: dma_buf = %pK, inode = %lu, ref = %ld, device_addr = %x, size = %d, kvaddr = %pK, buffer_type = %#x\n",
		__func__, dbuf, (dbuf ? file_inode(dbuf->file)->i_ino : -1),
		(dbuf ? file_count(dbuf->file) : -1), mem->device_addr,
		mem->size, mem->kvaddr, mem->buffer_type);

	if (mem->device_addr) {
		msm_dma_put_device_address(mem->flags,
			&mem->mapping_info, mem->buffer_type, sid);
		mem->device_addr = 0x0;
	}

	if (mem->kvaddr) {
		struct iosys_map map = IOSYS_MAP_INIT_VADDR(mem->kvaddr);

		dma_buf_vunmap(dbuf, &map);
		mem->kvaddr = NULL;
		dma_buf_end_cpu_access(dbuf, DMA_BIDIRECTIONAL);
	}

	if (dbuf) {
		trace_msm_smem_buffer_dma_op_start("FREE",
				(u32)mem->buffer_type, -1, mem->size, -1,
				mem->flags, -1);
		dma_buf_put(dbuf);
		mem->dma_buf = NULL;
		trace_msm_smem_buffer_dma_op_end("FREE", (u32)mem->buffer_type,
			-1, mem->size, -1, mem->flags, -1);
	}

	return 0;
}

int msm_smem_alloc(size_t size, u32 align, u32 flags,
	enum hal_buffer buffer_type, int map_kernel,
	void *res, u32 session_type, struct msm_smem *smem, u32 sid)
{
	int rc = 0;

	if (!smem || !size) {
		s_vpr_e(sid, "%s: NULL smem or %d size\n",
			__func__, (u32)size);
		return -EINVAL;
	}

	rc = alloc_dma_mem(size, align, flags, buffer_type, map_kernel,
				(struct msm_vidc_platform_resources *)res,
				session_type, smem, sid);

	return rc;
}

int msm_smem_free(struct msm_smem *smem, u32 sid)
{
	int rc = 0;

	if (!smem) {
		s_vpr_e(sid, "NULL smem passed\n");
		return -EINVAL;
	}
	rc = free_dma_mem(smem, sid);

	return rc;
};

int msm_smem_cache_operations(struct dma_buf *dbuf,
	enum smem_cache_ops cache_op, unsigned long offset,
	unsigned long size, u32 sid)
{
	int rc = 0;
	unsigned long flags = 0;

	if (!dbuf) {
		s_vpr_e(sid, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	/* Return if buffer doesn't support caching */
	rc = vidc_dma_buf_flags(dbuf, &flags);
	if (rc) {
		s_vpr_e(sid, "%s: dma_buf_get_flags failed, err %d\n",
			__func__, rc);
		return rc;
	} else if (!(flags & QCOM_DMA_HEAP_FLAG_CACHED)) {
		return rc;
	}

	switch (cache_op) {
	case SMEM_CACHE_CLEAN:
	case SMEM_CACHE_CLEAN_INVALIDATE:
		rc = dma_buf_begin_cpu_access_partial(dbuf, DMA_TO_DEVICE,
				offset, size);
		if (rc)
			break;
		rc = dma_buf_end_cpu_access_partial(dbuf, DMA_TO_DEVICE,
				offset, size);
		break;
	case SMEM_CACHE_INVALIDATE:
		rc = dma_buf_begin_cpu_access_partial(dbuf, DMA_TO_DEVICE,
				offset, size);
		if (rc)
			break;
		rc = dma_buf_end_cpu_access_partial(dbuf, DMA_FROM_DEVICE,
				offset, size);
		break;
	default:
		s_vpr_e(sid, "%s: cache (%d) operation not supported\n",
			__func__, cache_op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

struct context_bank_info *msm_smem_get_context_bank(u32 session_type,
	bool is_secure, struct msm_vidc_platform_resources *res,
	enum hal_buffer buffer_type, u32 sid)
{
	struct context_bank_info *cb = NULL, *match = NULL;

	/*
	 * HAL_BUFFER_INPUT is directly mapped to bitstream CB in DT
	 * as the buffer type structure was initially designed
	 * just for decoder. For Encoder, input should be mapped to
	 * yuvpixel CB. Persist is mapped to nonpixel CB.
	 * So swap the buffer types just in this local scope.
	 */
	if (is_secure && session_type == MSM_VIDC_ENCODER) {
		if (buffer_type == HAL_BUFFER_INPUT)
			buffer_type = HAL_BUFFER_OUTPUT;
		else if (buffer_type == HAL_BUFFER_OUTPUT)
			buffer_type = HAL_BUFFER_INPUT;
		else if (buffer_type == HAL_BUFFER_INTERNAL_PERSIST)
			buffer_type = HAL_BUFFER_INTERNAL_PERSIST_1;
	}

	mutex_lock(&res->cb_lock);
	list_for_each_entry(cb, &res->context_banks, list) {
		if (cb->is_secure == is_secure &&
				cb->buffer_type & buffer_type) {
			match = cb;
			break;
		}
	}
	mutex_unlock(&res->cb_lock);
	if (!match)
		s_vpr_e(sid,
			"%s: cb not found for buffer_type %x, is_secure %d\n",
			__func__, buffer_type, is_secure);

	return match;
}

/*
 * Secure buffer prefetch came with 5.4's ION; 6.12's secure DMA-BUF heaps
 * allocate on demand and have nothing to prefetch.
 */
int msm_smem_memory_prefetch(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid parameters\n", __func__);
		return -EINVAL;
	}

	return 0;
}

/*
 * Secure buffer drain came with 5.4's ION; 6.12's secure DMA-BUF heaps
 * allocate on demand and have nothing to drain.
 */
int msm_smem_memory_drain(struct msm_vidc_inst *inst)
{
	if (!inst) {
		d_vpr_e("%s: invalid parameters\n", __func__);
		return -EINVAL;
	}

	return 0;
}
