// Open the Adreno GPU through kgsl, which powers it up (SQE and GMU
// firmware, zap shader), and print what the driver reports about it.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/msm_kgsl.h>

int main(void)
{
	struct kgsl_devinfo info = { 0 };
	struct kgsl_device_getproperty prop = {
		.type = KGSL_PROP_DEVICE_INFO,
		.value = &info,
		.sizebytes = sizeof(info),
	};
	int fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);

	if (fd < 0) {
		printf("open /dev/kgsl-3d0: %s\n", strerror(errno));
		return 1;
	}
	if (ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &prop)) {
		printf("getproperty: %s\n", strerror(errno));
		return 1;
	}
	printf("device_id %u chip_id 0x%08x gpu_id %u mmu %u gmem %u bytes\n",
	       info.device_id, info.chip_id, info.gpu_id, info.mmu_enabled,
	       (unsigned int)info.gmem_sizebytes);
	sleep(2);
	close(fd);
	return 0;
}
