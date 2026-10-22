// libion for an ION based vendor on a kernel without ION: every ION
// allocation goes to the matching DMA-BUF heap instead.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct dma_heap_allocation_data {
	uint64_t len;
	uint32_t fd;
	uint32_t fd_flags;
	uint64_t heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_allocation_data)

struct ion_heap_data {
	char name[32];
	uint32_t type;
	uint32_t heap_id;
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
};

typedef int ion_user_handle_t;

#define BIT(n)			(1U << (n))
#define ION_FLAG_CACHED		1U
#define ION_FLAG_CP_PIXEL	BIT(19)
#define ION_FLAG_CP_CDSP	BIT(29)
#define ION_FLAG_CP_SEC_DISPLAY	BIT(25)
#define ION_FLAG_SECURE		BIT(31)
#define ION_FLAGS_CP_MASK	0x6FFE8000U

/* heap ids of the 5.4 msm ION driver */
static const struct {
	const char *name;
	uint32_t id;
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
	{ "secure_heap", 10 },
	{ "secure_display", 11 },
	{ "spss", 14 },
	{ "secure_carveout", 15 },
	{ "system", 25 },
};
#define NR_HEAPS (sizeof(msm_heaps) / sizeof(msm_heaps[0]))

static int logged;

static void klog(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int fd, n;

	if (logged > 200)
		return;
	logged++;
	n = snprintf(buf, sizeof(buf), "<6>ionshim[%d]: ", getpid());
	va_start(ap, fmt);
	n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
	va_end(ap);
	fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		write(fd, buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
		close(fd);
	}
}

static const char *pick_heap(unsigned int mask, unsigned int flags)
{
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

static int heap_alloc(const char *name, size_t len, int *out)
{
	struct dma_heap_allocation_data d = {
		.len = len,
		.fd_flags = O_RDWR | O_CLOEXEC,
	};
	char path[64];
	int fd, ret;

	snprintf(path, sizeof(path), "/dev/dma_heap/%s", name);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &d);
	ret = ret < 0 ? -errno : 0;
	close(fd);
	if (!ret)
		*out = d.fd;
	return ret;
}

int ion_open(void)
{
	int fd = open("/dev/dma_heap", O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (fd < 0) {
		klog("no /dev/dma_heap: %d\n", errno);
		return -errno;
	}
	return fd;
}

int ion_close(int fd)
{
	return close(fd) ? -errno : 0;
}

int ion_is_legacy(int fd)
{
	(void)fd;
	return 0;
}

int ion_is_using_modular_heaps(int fd)
{
	(void)fd;
	return 1;
}

int ion_alloc_fd(int fd, size_t len, size_t align, unsigned int heap_mask,
		 unsigned int flags, int *handle_fd)
{
	const char *name = pick_heap(heap_mask, flags);
	int ret;

	(void)fd;
	(void)align;
	if (!handle_fd)
		return -EINVAL;
	ret = heap_alloc(name, len, handle_fd);
	if (ret) {
		klog("alloc len %zu mask 0x%x flags 0x%x from %s failed: %d\n",
		     len, heap_mask, flags, name, ret);
		name = "qcom,system";
		ret = heap_alloc(name, len, handle_fd);
	}
	if (ret) {
		name = "system";
		ret = heap_alloc(name, len, handle_fd);
	}
	klog("alloc len %zu mask 0x%x flags 0x%x -> %s: %d\n", len, heap_mask,
	     flags, name, ret);
	return ret;
}

int ion_query_heap_cnt(int fd, int *cnt)
{
	(void)fd;
	if (!cnt)
		return -EINVAL;
	*cnt = NR_HEAPS;
	return 0;
}

int ion_query_get_heaps(int fd, int cnt, void *buffers)
{
	struct ion_heap_data *h = buffers;
	int i;

	(void)fd;
	if (!h || cnt <= 0)
		return -EINVAL;
	for (i = 0; i < cnt && i < (int)NR_HEAPS; i++) {
		memset(&h[i], 0, sizeof(h[i]));
		strncpy(h[i].name, msm_heaps[i].name, sizeof(h[i].name) - 1);
		h[i].heap_id = msm_heaps[i].id;
	}
	return 0;
}

int ion_sync_fd(int fd, int handle_fd)
{
	(void)fd;
	(void)handle_fd;
	return 0;
}

/* the handle based API only exists on legacy ION */
int ion_alloc(int fd, size_t len, size_t align, unsigned int heap_mask,
	      unsigned int flags, ion_user_handle_t *handle)
{
	(void)fd; (void)len; (void)align; (void)heap_mask; (void)flags; (void)handle;
	klog("legacy ion_alloc called\n");
	return -ENOTTY;
}

int ion_free(int fd, ion_user_handle_t handle)
{
	(void)fd; (void)handle;
	return -ENOTTY;
}

int ion_map(int fd, ion_user_handle_t handle, size_t length, int prot,
	    int flags, off_t offset, unsigned char **ptr, int *map_fd)
{
	(void)fd; (void)handle; (void)length; (void)prot; (void)flags;
	(void)offset; (void)ptr; (void)map_fd;
	return -ENOTTY;
}

int ion_share(int fd, ion_user_handle_t handle, int *share_fd)
{
	(void)fd; (void)handle; (void)share_fd;
	return -ENOTTY;
}

int ion_import(int fd, int share_fd, ion_user_handle_t *handle)
{
	(void)fd; (void)share_fd; (void)handle;
	return -ENOTTY;
}
