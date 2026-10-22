// SPDX-License-Identifier: GPL-2.0
/*
 * Keep the kernel log on the logdump partition while testing Android on
 * redwood 6.12. It runs in the kernel and writes with bios, so it keeps
 * going once Android's init has loaded its SELinux policy, which cuts off a
 * userspace logger reading /dev/kmsg.
 */
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kmsg_dump.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sysrq.h>

#define LOG_PAGES	4096
#define SCAN_MINORS	1024

static struct page *pages[LOG_PAGES];
static size_t used, done;
static struct file *dev;
static struct task_struct *task;

static bool is_logdump(struct block_device *bdev)
{
	return bdev->bd_meta_info &&
	       !strcmp(bdev->bd_meta_info->volname, "logdump");
}

/* only write to a cleared partition, so an unread log from an earlier boot
 * survives the boot used to read it */
static bool is_cleared(struct block_device *bdev)
{
	struct page *p = alloc_page(GFP_KERNEL | __GFP_ZERO);
	struct bio *bio;
	bool ok = false;
	u8 *head;
	int i, err;

	if (!p)
		return false;
	bio = bio_alloc(bdev, 1, REQ_OP_READ, GFP_KERNEL);
	bio->bi_iter.bi_sector = 0;
	__bio_add_page(bio, p, PAGE_SIZE, 0);
	err = submit_bio_wait(bio);
	bio_put(bio);
	if (!err) {
		head = page_address(p);
		for (i = 0; i < 64 && !head[i]; i++)
			;
		ok = i == 64;
	}
	__free_page(p);
	return ok;
}

static struct file *find_logdump(void)
{
	static const unsigned int majors[] = { 8, 259 };
	struct file *f;
	int i, m;

	for (i = 0; i < ARRAY_SIZE(majors); i++) {
		for (m = 0; m < SCAN_MINORS; m++) {
			f = bdev_file_open_by_dev(MKDEV(majors[i], m), BLK_OPEN_READ, NULL, NULL);
			if (IS_ERR(f))
				continue;
			if (is_logdump(file_bdev(f)) &&
			    bdev_nr_bytes(file_bdev(f)) >= (loff_t)LOG_PAGES * PAGE_SIZE) {
				dev_t devt = file_bdev(f)->bd_dev;
				bool cleared = is_cleared(file_bdev(f));

				fput(f);
				if (!cleared) {
					pr_info("bootlog: logdump holds an unread log, not writing\n");
					return ERR_PTR(-EEXIST);
				}
				f = bdev_file_open_by_dev(devt, BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);
				if (!IS_ERR(f))
					pr_info("bootlog: writing to %pg\n", file_bdev(f));
				return f;
			}
			fput(f);
		}
	}
	return NULL;
}

static void append(const char *s, size_t n)
{
	while (n && used < (size_t)LOG_PAGES * PAGE_SIZE) {
		size_t pg = used / PAGE_SIZE, off = used % PAGE_SIZE;
		size_t k = min(n, PAGE_SIZE - off);

		if (!pages[pg]) {
			pages[pg] = alloc_page(GFP_KERNEL | __GFP_ZERO);
			if (!pages[pg])
				return;
		}
		memcpy(page_address(pages[pg]) + off, s, k);
		used += k;
		s += k;
		n -= k;
	}
}

static void flush(void)
{
	size_t first = done / PAGE_SIZE, last = DIV_ROUND_UP(used, PAGE_SIZE);
	size_t pg = first;

	while (pg < last) {
		unsigned int nr = min_t(size_t, last - pg, BIO_MAX_VECS);
		struct bio *bio = bio_alloc(file_bdev(dev), nr,
					    REQ_OP_WRITE | REQ_SYNC | REQ_FUA, GFP_KERNEL);
		unsigned int i;

		bio->bi_iter.bi_sector = (sector_t)pg << (PAGE_SHIFT - SECTOR_SHIFT);
		for (i = 0; i < nr; i++)
			__bio_add_page(bio, pages[pg + i], PAGE_SIZE, 0);
		if (submit_bio_wait(bio)) {
			bio_put(bio);
			return;
		}
		bio_put(bio);
		pg += nr;
	}
	done = used;
}

static int bootlog_thread(void *unused)
{
	static char line[1024];
	struct kmsg_dump_iter iter;
	unsigned long start = jiffies, beat = jiffies;
	int tick = 0;
	size_t len;

	kmsg_dump_rewind(&iter);
	append("REDWOOD612ANDROID\n", 18);
	while (!kthread_should_stop() && tick < 600) {
		if (time_after_eq(jiffies, beat)) {
			beat = jiffies + 2 * HZ;
			pr_info("bootlog: tick %d\n", tick);
			if (tick == 15 || tick == 45) {
				handle_sysrq('w');
				handle_sysrq('l');
			}
			tick++;
		}
		while (kmsg_dump_get_line(&iter, false, line, sizeof(line), &len))
			append(line, len);
		if (!dev) {
			struct file *f = find_logdump();

			if (IS_ERR(f))
				break;
			dev = f;
		}
		if (dev && used > done)
			flush();
		msleep(dev ? 100 : 500);
	}
	pr_info("bootlog: stopped after %u ms\n", jiffies_to_msecs(jiffies - start));
	while (kmsg_dump_get_line(&iter, false, line, sizeof(line), &len))
		append(line, len);
	if (dev && used > done)
		flush();
	while (!kthread_should_stop())
		msleep(1000);
	return 0;
}

static int __init bootlog_init(void)
{
	task = kthread_run(bootlog_thread, NULL, "bootlog");
	return PTR_ERR_OR_ZERO(task);
}

static void __exit bootlog_exit(void)
{
	int i;

	kthread_stop(task);
	if (dev)
		fput(dev);
	for (i = 0; i < LOG_PAGES; i++)
		if (pages[i])
			__free_page(pages[i]);
}

module_init(bootlog_init);
module_exit(bootlog_exit);
MODULE_DESCRIPTION("Kernel log on the logdump partition for redwood 6.12 bring-up");
MODULE_LICENSE("GPL");
