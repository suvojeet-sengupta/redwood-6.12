// SPDX-License-Identifier: GPL-2.0
/*
 * Poke the msm_vidc decoder or encoder from the bring-up shell:
 * vidctest [/dev/video32]
 * QUERYCAP, the formats on both queues, then an H.264 decode session up to
 * buffer allocation, which makes the driver open a session on venus.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>

static void fourcc(unsigned int f, char *s)
{
	s[0] = f & 0xff; s[1] = (f >> 8) & 0xff; s[2] = (f >> 16) & 0xff; s[3] = f >> 24; s[4] = 0;
}

static void enum_fmt(int fd, enum v4l2_buf_type type, const char *name)
{
	struct v4l2_fmtdesc d;
	char s[5];
	int i;

	for (i = 0;; i++) {
		memset(&d, 0, sizeof(d));
		d.index = i;
		d.type = type;
		if (ioctl(fd, VIDIOC_ENUM_FMT, &d))
			break;
		fourcc(d.pixelformat, s);
		printf("  %s %d: %s (%s)\n", name, i, s, d.description);
	}
	if (!i)
		printf("  %s: no formats (%s)\n", name, strerror(errno));
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/video32";
	struct v4l2_capability cap = { 0 };
	struct v4l2_format f;
	struct v4l2_requestbuffers rb;
	int fd = open(path, O_RDWR);

	if (fd < 0) {
		printf("open %s: %s\n", path, strerror(errno));
		return 1;
	}
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		printf("QUERYCAP: %s\n", strerror(errno));
		return 1;
	}
	printf("%s: driver %s, card %s, caps 0x%x\n", path, cap.driver, cap.card, cap.device_caps);
	enum_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, "output");
	enum_fmt(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "capture");
	if (strstr((char *)cap.card, "enc"))
		return 0;

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f.fmt.pix_mp.width = 1920;
	f.fmt.pix_mp.height = 1080;
	f.fmt.pix_mp.num_planes = 1;
	printf("S_FMT output H264 1920x1080: %s\n", ioctl(fd, VIDIOC_S_FMT, &f) ? strerror(errno) : "ok");

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	f.fmt.pix_mp.width = 1920;
	f.fmt.pix_mp.height = 1080;
	printf("S_FMT capture NV12: %s\n", ioctl(fd, VIDIOC_S_FMT, &f) ? strerror(errno) : "ok");

	memset(&rb, 0, sizeof(rb));
	rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	rb.memory = V4L2_MEMORY_USERPTR;
	rb.count = 4;
	if (ioctl(fd, VIDIOC_REQBUFS, &rb))
		printf("REQBUFS output: %s\n", strerror(errno));
	else
		printf("REQBUFS output: %u buffers\n", rb.count);
	close(fd);
	return 0;
}
