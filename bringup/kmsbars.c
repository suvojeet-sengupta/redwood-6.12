// Show colour bars on the first connected DRM connector, with plain KMS
// ioctls, so the 6.12 display can be checked without any userspace stack.
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int fd;

static int xioctl(unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

int main(int argc, char **argv)
{
	struct drm_mode_card_res res = { 0 };
	uint32_t conn_ids[16], crtc_ids[16], enc_ids[16], fb_ids[16];
	unsigned int i;
	int secs = argc > 1 ? atoi(argv[1]) : 30;

	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open card0");
		return 1;
	}
	xioctl(DRM_IOCTL_SET_MASTER, NULL);

	if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res)) {
		perror("getresources");
		return 1;
	}
	res.connector_id_ptr = (uintptr_t)conn_ids;
	res.crtc_id_ptr = (uintptr_t)crtc_ids;
	res.encoder_id_ptr = (uintptr_t)enc_ids;
	res.fb_id_ptr = (uintptr_t)fb_ids;
	res.count_connectors = res.count_connectors > 16 ? 16 : res.count_connectors;
	res.count_crtcs = res.count_crtcs > 16 ? 16 : res.count_crtcs;
	res.count_encoders = res.count_encoders > 16 ? 16 : res.count_encoders;
	res.count_fbs = res.count_fbs > 16 ? 16 : res.count_fbs;
	if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res)) {
		perror("getresources 2");
		return 1;
	}
	printf("%u connectors, %u crtcs, %u encoders\n",
	       res.count_connectors, res.count_crtcs, res.count_encoders);

	for (i = 0; i < res.count_connectors; i++) {
		struct drm_mode_get_connector c = { .connector_id = conn_ids[i] };
		struct drm_mode_modeinfo modes[64];
		uint32_t encs[8];

		if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c))
			continue;
		printf("connector %u type %u status %u modes %u\n",
		       c.connector_id, c.connector_type, c.connection, c.count_modes);
		/* skip the writeback (virtual) connector, we want the panel */
		if (c.connection != 1 || !c.count_modes || c.connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
			continue;
		c.count_modes = c.count_modes > 64 ? 64 : c.count_modes;
		c.count_encoders = c.count_encoders > 8 ? 8 : c.count_encoders;
		c.modes_ptr = (uintptr_t)modes;
		c.encoders_ptr = (uintptr_t)encs;
		c.count_props = 0;
		if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c))
			continue;
		struct drm_mode_modeinfo *m = &modes[0];
		printf("mode %s %ux%u@%u\n", m->name, m->hdisplay, m->vdisplay, m->vrefresh);

		uint32_t crtc = 0;
		struct drm_mode_get_encoder e = { .encoder_id = c.encoder_id ? c.encoder_id : encs[0] };
		if (!xioctl(DRM_IOCTL_MODE_GETENCODER, &e))
			crtc = e.crtc_id;
		if (!crtc) {
			for (unsigned int k = 0; k < res.count_crtcs; k++)
				if (e.possible_crtcs & (1u << k)) {
					crtc = crtc_ids[k];
					break;
				}
		}
		printf("encoder %u crtc %u\n", e.encoder_id, crtc);

		struct drm_mode_create_dumb cd = { .width = m->hdisplay,
						    .height = m->vdisplay, .bpp = 32 };
		if (xioctl(DRM_IOCTL_MODE_CREATE_DUMB, &cd)) {
			perror("create_dumb");
			return 1;
		}
		struct drm_mode_fb_cmd fbc = { .width = cd.width, .height = cd.height,
					       .pitch = cd.pitch, .bpp = 32, .depth = 24,
					       .handle = cd.handle };
		if (xioctl(DRM_IOCTL_MODE_ADDFB, &fbc)) {
			perror("addfb");
			return 1;
		}
		struct drm_mode_map_dumb md = { .handle = cd.handle };
		if (xioctl(DRM_IOCTL_MODE_MAP_DUMB, &md)) {
			perror("map_dumb");
			return 1;
		}
		uint8_t *px = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
		if (px == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		static const uint32_t bars[] = { 0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00,
						 0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000 };
		for (uint32_t y = 0; y < cd.height; y++) {
			uint32_t *row = (uint32_t *)(px + y * cd.pitch);
			for (uint32_t x = 0; x < cd.width; x++)
				row[x] = bars[x * 8 / cd.width];
		}

		struct drm_mode_crtc sc = { .crtc_id = crtc, .fb_id = fbc.fb_id,
					    .set_connectors_ptr = (uintptr_t)&c.connector_id,
					    .count_connectors = 1, .mode = *m, .mode_valid = 1 };
		if (xioctl(DRM_IOCTL_MODE_SETCRTC, &sc)) {
			perror("setcrtc");
			return 1;
		}
		printf("colour bars up for %ds\n", secs);
		fflush(stdout);
		sleep(secs);
		return 0;
	}
	printf("no connected connector\n");
	return 1;
}
