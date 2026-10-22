// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA mixer and tone test for the bring-up shell, without tinyalsa.
 *
 *   sndtest ctl [card]                     list the mixer controls
 *   sndtest get <name> [card]              show one control
 *   sndtest set <name> <value...> [-c card] set a control (numbers or enum names)
 *   sndtest play <card> <device> [seconds] [rate] [channels] [hz]
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>

static int open_ctl(int card)
{
	char path[64];
	int fd;

	snprintf(path, sizeof(path), "/dev/snd/controlC%d", card);
	fd = open(path, O_RDWR);
	if (fd < 0)
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
	return fd;
}

static const char *type_name(int t)
{
	switch (t) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN: return "BOOL";
	case SNDRV_CTL_ELEM_TYPE_INTEGER: return "INT";
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "ENUM";
	case SNDRV_CTL_ELEM_TYPE_BYTES: return "BYTE";
	case SNDRV_CTL_ELEM_TYPE_INTEGER64: return "INT64";
	default: return "?";
	}
}

static int enum_item(int fd, struct snd_ctl_elem_info *info, unsigned int i, char *name, size_t len)
{
	struct snd_ctl_elem_info ei = *info;

	ei.value.enumerated.item = i;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &ei) < 0)
		return -1;
	snprintf(name, len, "%s", ei.value.enumerated.name);
	return 0;
}

static void show(int fd, struct snd_ctl_elem_id *id)
{
	struct snd_ctl_elem_info info = { .id = *id };
	struct snd_ctl_elem_value val = { .id = *id };
	unsigned int i, n;
	char item[64];

	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0)
		return;
	printf("%u\t%s\t%u\t%s\t", info.id.numid, type_name(info.type), info.count, info.id.name);
	if (!(info.access & SNDRV_CTL_ELEM_ACCESS_READ) ||
	    ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &val) < 0) {
		printf("(unreadable)\n");
		return;
	}
	n = info.count > 16 ? 16 : info.count;
	for (i = 0; i < n; i++) {
		switch (info.type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			printf("%ld ", val.value.integer.value[i]);
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			printf("%lld ", (long long)val.value.integer64.value[i]);
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			if (enum_item(fd, &info, val.value.enumerated.item[i], item, sizeof(item)) == 0)
				printf("%s ", item);
			break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			printf("%02x", val.value.bytes.data[i]);
			break;
		}
	}
	if (info.type == SNDRV_CTL_ELEM_TYPE_INTEGER)
		printf("(%ld..%ld)", info.value.integer.min, info.value.integer.max);
	if (info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
		printf("{");
		for (i = 0; i < info.value.enumerated.items; i++)
			if (enum_item(fd, &info, i, item, sizeof(item)) == 0)
				printf("%s%s", i ? "|" : "", item);
		printf("}");
	}
	printf("\n");
}

static struct snd_ctl_elem_id *list(int fd, unsigned int *count)
{
	struct snd_ctl_elem_list l = { 0 };
	struct snd_ctl_elem_id *ids;

	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) < 0)
		return NULL;
	ids = calloc(l.count, sizeof(*ids));
	l.space = l.count;
	l.pids = ids;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &l) < 0)
		return NULL;
	*count = l.used;
	return ids;
}

static int find(int fd, const char *name, struct snd_ctl_elem_id *out)
{
	struct snd_ctl_elem_id *ids;
	unsigned int i, n;

	ids = list(fd, &n);
	if (!ids)
		return -1;
	for (i = 0; i < n; i++)
		if (!strcmp((char *)ids[i].name, name)) {
			*out = ids[i];
			free(ids);
			return 0;
		}
	free(ids);
	fprintf(stderr, "no control \"%s\"\n", name);
	return -1;
}

static int set(int fd, const char *name, char **vals, int nvals)
{
	struct snd_ctl_elem_id id;
	struct snd_ctl_elem_info info;
	struct snd_ctl_elem_value val;
	char item[64];
	unsigned int i, j;

	if (find(fd, name, &id))
		return 1;
	memset(&info, 0, sizeof(info));
	info.id = id;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0)
		return 1;
	memset(&val, 0, sizeof(val));
	val.id = id;
	ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &val);
	for (i = 0; i < info.count; i++) {
		const char *v = vals[i < (unsigned int)nvals ? i : nvals - 1];

		switch (info.type) {
		case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		case SNDRV_CTL_ELEM_TYPE_INTEGER:
			val.value.integer.value[i] = strtol(v, NULL, 0);
			break;
		case SNDRV_CTL_ELEM_TYPE_INTEGER64:
			val.value.integer64.value[i] = strtoll(v, NULL, 0);
			break;
		case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
			for (j = 0; j < info.value.enumerated.items; j++)
				if (enum_item(fd, &info, j, item, sizeof(item)) == 0 && !strcmp(item, v))
					break;
			if (j == info.value.enumerated.items)
				j = strtoul(v, NULL, 0);
			val.value.enumerated.item[i] = j;
			break;
		case SNDRV_CTL_ELEM_TYPE_BYTES:
			val.value.bytes.data[i] = strtoul(v, NULL, 0);
			break;
		}
	}
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &val) < 0) {
		fprintf(stderr, "write \"%s\": %s\n", name, strerror(errno));
		return 1;
	}
	show(fd, &id);
	return 0;
}

static void mask_set(struct snd_pcm_hw_params *p, int param, unsigned int bit)
{
	struct snd_mask *m = &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];

	memset(m, 0, sizeof(*m));
	m->bits[bit >> 5] |= 1u << (bit & 31);
}

static void interval_set(struct snd_pcm_hw_params *p, int param, unsigned int min, unsigned int max)
{
	struct snd_interval *i = &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];

	memset(i, 0, sizeof(*i));
	i->min = min;
	i->max = max;
	i->integer = 1;
}

static int play(int card, int dev, int secs, unsigned int rate, unsigned int ch, double hz)
{
	struct snd_pcm_hw_params hw;
	struct snd_xferi x;
	char path[64];
	short *buf;
	unsigned int i, c, period;
	int fd, n, total, done = 0;
	double ph = 0;

	snprintf(path, sizeof(path), "/dev/snd/pcmC%dD%dp", card, dev);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
		return 1;
	}
	memset(&hw, 0, sizeof(hw));
	for (n = SNDRV_PCM_HW_PARAM_FIRST_MASK; n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++)
		memset(&hw.masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK], 0xff, sizeof(struct snd_mask));
	for (n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++)
		interval_set(&hw, n, 0, ~0u);
	mask_set(&hw, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	mask_set(&hw, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
	mask_set(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
	interval_set(&hw, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16, 16);
	interval_set(&hw, SNDRV_PCM_HW_PARAM_FRAME_BITS, 16 * ch, 16 * ch);
	interval_set(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, ch, ch);
	interval_set(&hw, SNDRV_PCM_HW_PARAM_RATE, rate, rate);
	interval_set(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, rate / 50, rate / 50);
	interval_set(&hw, SNDRV_PCM_HW_PARAM_PERIODS, 4, 4);
	hw.rmask = ~0u;
	if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0) {
		fprintf(stderr, "hw_params: %s\n", strerror(errno));
		return 1;
	}
	if (ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) {
		fprintf(stderr, "prepare: %s\n", strerror(errno));
		return 1;
	}
	period = rate / 50;
	buf = calloc(period * ch, sizeof(*buf));
	total = secs * rate;
	printf("playing %g Hz on %s, %u Hz %u ch, %d s\n", hz, path, rate, ch, secs);
	while (done < total) {
		for (i = 0; i < period; i++) {
			short s = (short)(8000 * sin(ph));

			ph += 2 * M_PI * hz / rate;
			for (c = 0; c < ch; c++)
				buf[i * ch + c] = s;
		}
		x.buf = buf;
		x.frames = period;
		x.result = 0;
		if (ioctl(fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
			fprintf(stderr, "write: %s\n", strerror(errno));
			if (errno == EPIPE && ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) == 0)
				continue;
			return 1;
		}
		done += x.result;
	}
	ioctl(fd, SNDRV_PCM_IOCTL_DRAIN);
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	struct snd_ctl_elem_id *ids, id;
	unsigned int i, n;
	int fd, card = 0;

	if (argc < 2)
		goto usage;
	if (!strcmp(argv[1], "ctl")) {
		fd = open_ctl(argc > 2 ? atoi(argv[2]) : 0);
		if (fd < 0 || !(ids = list(fd, &n)))
			return 1;
		for (i = 0; i < n; i++)
			show(fd, &ids[i]);
		return 0;
	}
	if (!strcmp(argv[1], "get") && argc > 2) {
		fd = open_ctl(argc > 3 ? atoi(argv[3]) : 0);
		if (fd < 0 || find(fd, argv[2], &id))
			return 1;
		show(fd, &id);
		return 0;
	}
	if (!strcmp(argv[1], "set") && argc > 3) {
		if (argc > 5 && !strcmp(argv[argc - 2], "-c")) {
			card = atoi(argv[argc - 1]);
			argc -= 2;
		}
		fd = open_ctl(card);
		return fd < 0 ? 1 : set(fd, argv[2], &argv[3], argc - 3);
	}
	if (!strcmp(argv[1], "play") && argc > 3)
		return play(atoi(argv[2]), atoi(argv[3]),
			    argc > 4 ? atoi(argv[4]) : 3,
			    argc > 5 ? atoi(argv[5]) : 48000,
			    argc > 6 ? atoi(argv[6]) : 2,
			    argc > 7 ? atof(argv[7]) : 1000);
usage:
	fprintf(stderr, "sndtest ctl [card] | get <name> [card] | set <name> <value...> [-c card] |"
		" play <card> <device> [seconds] [rate] [channels] [hz]\n");
	return 1;
}
