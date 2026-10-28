// SPDX-License-Identifier: GPL-2.0-only
/*
 * /dev/ion for redwood's 5.4 vendor on 6.12, which has DMA-BUF heaps and no
 * ION. The vendor's SELinux policy lets every domain open /dev/ion
 * (ion_device), but only a few of them open the qcom DMA-BUF heap nodes, so
 * TA loads through libQSEEComAPI (fingerprint, mlipay, ...) failed with
 * -EACCES. This takes the modern ION allocation ioctl and allocates from
 * the DMA-BUF heap the 5.4 msm ION heap id or flags stand for.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct ion_allocation_data {
	__u64 len;
	__u32 heap_id_mask;
	__u32 flags;
	__u32 fd;
	__u32 unused;
};

struct ion_heap_data {
	char name[32];
	__u32 type;
	__u32 heap_id;
	__u32 reserved0;
	__u32 reserved1;
	__u32 reserved2;
};

struct ion_heap_query {
	__u32 cnt;
	__u32 reserved0;
	__u64 heaps;
	__u32 reserved1;
	__u32 reserved2;
};

#define ION_IOC_MAGIC		'I'
#define ION_IOC_ALLOC		_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_HEAP_QUERY	_IOWR(ION_IOC_MAGIC, 8, struct ion_heap_query)
#define ION_IOC_ABI_VERSION	_IOR(ION_IOC_MAGIC, 9, __u32)

#define ION_FLAG_CACHED		1U
#define ION_FLAG_CP_PIXEL	BIT(19)
#define ION_FLAG_CP_SEC_DISPLAY	BIT(25)
#define ION_FLAG_CP_CDSP	BIT(29)
#define ION_FLAG_SECURE		BIT(31)
#define ION_FLAGS_CP_MASK	0x6FFE8000U

/* heap ids of the 5.4 msm ION driver, for ION_IOC_HEAP_QUERY */
static const struct {
	const char *name;
	u32 id;
} msm_heaps[] = {
	{ "qsecom_ta", 1 },
	{ "camera", 2 },
	{ "display", 3 },
	{ "adsp", 4 },
	{ "audio_ml", 5 },
	{ "user_contig", 6 },
	{ "qsecom", 7 },
	{ "audio", 8 },
	{ "secure_heap", 9 },
	{ "secure_display", 11 },
	{ "spss", 14 },
	{ "secure_carveout", 15 },
	{ "system", 25 },
	/* the names libdmabufheap asks for when it falls back to ION */
	{ "ion_system_heap", 25 },
	{ "ion_system_contig_heap", 25 },
};

/* the same choice as bringup/ionshim.c */
/* the system heap's id in the ION masks the vendor userspace passes */
#define ION_SYSTEM_HEAP_MASK	BIT(25)

static const char *pick_heap(u32 mask, u32 flags)
{
	/*
	 * Secure requests first, then the system heap whenever the mask has
	 * it: ION tried heaps by priority and the system heap came first, so
	 * Codec2's "any heap" mask (~0) got system memory. Checking the small
	 * carve-outs first gave it qseecom, and every MediaCodec failed to
	 * allocate its buffers (no video playback).
	 */
	if (flags & (ION_FLAG_SECURE | ION_FLAGS_CP_MASK) || (mask & BIT(11)))
		goto special;
	if (mask & ION_SYSTEM_HEAP_MASK)
		return (flags & ION_FLAG_CACHED) ? "qcom,system" : "qcom,system-uncached";
special:
	if (mask & BIT(7))
		return "qcom,qseecom";
	if (mask & BIT(1))
		return "qcom,qseecom-ta";
	if ((mask & BIT(11)) || (flags & ION_FLAG_CP_SEC_DISPLAY))
		return "qcom,secure-display";
	if (flags & (ION_FLAG_SECURE | ION_FLAGS_CP_MASK)) {
		if (flags & ION_FLAG_CP_PIXEL)
			return "qcom,secure-pixel";
		if (flags & ION_FLAG_CP_CDSP)
			return "qcom,secure-cdsp";
		return "qcom,secure-non-pixel";
	}
	if (mask & BIT(4))
		return "qcom,adsp";
	if (mask & BIT(3))
		return "qcom,display";
	if (mask & BIT(5))
		return "qcom,audio-ml";
	if (mask & BIT(6))
		return "qcom,user-contig";
	return (flags & ION_FLAG_CACHED) ? "qcom,system" : "qcom,system-uncached";
}

static struct dma_buf *heap_alloc(const char *name, size_t len)
{
	struct dma_heap *heap = dma_heap_find(name);
	struct dma_buf *buf;

	if (!heap)
		return ERR_PTR(-ENODEV);
	buf = dma_heap_buffer_alloc(heap, len, O_RDWR | O_CLOEXEC, 0);
	dma_heap_put(heap);
	return buf;
}

static long ion_alloc(struct ion_allocation_data __user *arg)
{
	struct ion_allocation_data d;
	const char *name;
	struct dma_buf *buf;
	int fd;

	if (copy_from_user(&d, arg, sizeof(d)))
		return -EFAULT;
	if (!d.len)
		return -EINVAL;
	name = pick_heap(d.heap_id_mask, d.flags);
	buf = heap_alloc(name, d.len);
	/* like 5.4, an ordinary buffer may come from the system heap instead */
	if (IS_ERR(buf) && !(d.flags & (ION_FLAG_SECURE | ION_FLAGS_CP_MASK)) &&
	    !(d.heap_id_mask & (BIT(1) | BIT(7) | BIT(11)))) {
		name = "qcom,system";
		buf = heap_alloc(name, d.len);
	}
	if (IS_ERR(buf)) {
		pr_warn_ratelimited("ioncompat: %s: %llu bytes, mask 0x%x flags 0x%x from %s failed: %ld\n",
				    current->comm, d.len, d.heap_id_mask, d.flags,
				    name, PTR_ERR(buf));
		return PTR_ERR(buf);
	}
	fd = dma_buf_fd(buf, O_CLOEXEC);
	if (fd < 0) {
		dma_buf_put(buf);
		return fd;
	}
	d.fd = fd;
	if (copy_to_user(arg, &d, sizeof(d))) {
		/* the fd is installed; userspace can't learn it, but it goes with the process */
		return -EFAULT;
	}
	return 0;
}

static long ion_heap_query(struct ion_heap_query __user *arg)
{
	struct ion_heap_query q;
	struct ion_heap_data h;
	u32 i, n = ARRAY_SIZE(msm_heaps);

	if (copy_from_user(&q, arg, sizeof(q)))
		return -EFAULT;
	if (q.heaps) {
		for (i = 0; i < min(q.cnt, n); i++) {
			memset(&h, 0, sizeof(h));
			strscpy(h.name, msm_heaps[i].name, sizeof(h.name));
			h.heap_id = msm_heaps[i].id;
			if (copy_to_user(u64_to_user_ptr(q.heaps) + i * sizeof(h), &h, sizeof(h)))
				return -EFAULT;
		}
		n = min(q.cnt, n);
	}
	q.cnt = n;
	return copy_to_user(arg, &q, sizeof(q)) ? -EFAULT : 0;
}

static long ion_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ION_IOC_ALLOC:
		return ion_alloc((void __user *)arg);
	case ION_IOC_HEAP_QUERY:
		return ion_heap_query((void __user *)arg);
	case ION_IOC_ABI_VERSION:
		return put_user(3U, (__u32 __user *)arg);
	}
	return -ENOTTY;
}

static const struct file_operations ion_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ion_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice ion_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &ion_fops,
};

module_misc_device(ion_misc);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_DESCRIPTION("/dev/ion on DMA-BUF heaps for redwood's 5.4 vendor");
MODULE_LICENSE("GPL");
