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
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>

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

/*
 * Every log starts with "REDWOOD612ANDROID seq=NNNNNNNN". Write to a cleared
 * logdump if there is one, else over the one with the older log, so the two
 * newest boots are always kept and nothing has to be cleared by hand.
 * Returns the log's sequence number, -1 for a cleared partition, or -2 if
 * it can't be read. A log from before the sequence numbers counts as 0.
 */
#define HEADER		"REDWOOD612ANDROID seq="
#define HEADER_LEN	(sizeof(HEADER) - 1)
#define SEQ_DIGITS	8

static long log_seq(struct block_device *bdev)
{
	struct page *p = alloc_page(GFP_KERNEL | __GFP_ZERO);
	struct bio *bio;
	long seq = -2;
	char digits[SEQ_DIGITS + 1];
	u8 *head;
	int i, err;

	if (!p)
		return -2;
	bio = bio_alloc(bdev, 1, REQ_OP_READ, GFP_KERNEL);
	bio->bi_iter.bi_sector = 0;
	__bio_add_page(bio, p, PAGE_SIZE, 0);
	err = submit_bio_wait(bio);
	bio_put(bio);
	if (err)
		pr_info("bootlog: reading %pg failed: %d\n", bdev, err);
	if (!err) {
		head = page_address(p);
		for (i = 0; i < 64 && !head[i]; i++)
			;
		if (i == 64) {
			seq = -1;
		} else if (!memcmp(head, HEADER, HEADER_LEN)) {
			memcpy(digits, head + HEADER_LEN, SEQ_DIGITS);
			digits[SEQ_DIGITS] = 0;
			if (kstrtol(digits, 10, &seq) || seq < 0)
				seq = 0;
		} else {
			seq = 0;
		}
	}
	__free_page(p);
	return seq;
}

static long my_seq;

static struct file *find_logdump(void)
{
	static const unsigned int majors[] = { 8, 259 };
	dev_t best = 0;
	long best_seq = LONG_MAX, max_seq = 0;
	struct file *f;
	int i, m;

	for (i = 0; i < ARRAY_SIZE(majors); i++) {
		for (m = 0; m < SCAN_MINORS; m++) {
			f = bdev_file_open_by_dev(MKDEV(majors[i], m), BLK_OPEN_READ, NULL, NULL);
			if (IS_ERR(f))
				continue;
			/* redwood has two */
			if (is_logdump(file_bdev(f)) &&
			    bdev_nr_bytes(file_bdev(f)) >= (loff_t)LOG_PAGES * PAGE_SIZE) {
				long seq = log_seq(file_bdev(f));

				pr_info("bootlog: %pg holds log %ld\n", file_bdev(f), seq);
				if (seq > max_seq)
					max_seq = seq;
				if (seq >= -1 && seq < best_seq) {
					best_seq = seq;
					best = file_bdev(f)->bd_dev;
				}
			}
			fput(f);
		}
	}
	if (!best)
		return NULL;
	my_seq = max_seq + 1;
	f = bdev_file_open_by_dev(best, BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);
	if (!IS_ERR(f))
		pr_info("bootlog: writing log %ld to %pg\n", my_seq, file_bdev(f));
	return f;
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

/* sysrq is off on a user build, so list the processes and the stacks of
 * blocked tasks directly */
static void show_tasks(void)
{
	struct task_struct *p, *t;

	rcu_read_lock();
	for_each_process(p)
		if (p->mm)
			pr_info("bootlog: ps %d %d %c %s\n", task_pid_nr(p), task_ppid_nr(p),
				task_state_to_char(p), p->comm);
	for_each_process_thread(p, t) {
		unsigned int state = READ_ONCE(t->__state);

		if ((state & TASK_UNINTERRUPTIBLE) && !(state & TASK_NOLOAD))
			sched_show_task(t);
	}
	rcu_read_unlock();
}

static int bootlog_thread(void *unused)
{
	static char line[1024];
	struct kmsg_dump_iter iter;
	unsigned long start = jiffies, beat = jiffies;
	int tick = 0;
	size_t len;

	kmsg_dump_rewind(&iter);
	/* the sequence number goes in once the partition is picked */
	append(HEADER "00000000\n", HEADER_LEN + SEQ_DIGITS + 1);
	while (!kthread_should_stop() && tick < 600) {
		if (time_after_eq(jiffies, beat)) {
			beat = jiffies + 2 * HZ;
			pr_info("bootlog: tick %d\n", tick);
			if (tick == 15 || tick == 45 || tick == 75)
				show_tasks();
			tick++;
		}
		while (kmsg_dump_get_line(&iter, false, line, sizeof(line), &len))
			append(line, len);
		if (!dev) {
			struct file *f = find_logdump();

			if (IS_ERR(f))
				break;
			if (f) {
				char seq[SEQ_DIGITS + 1];

				snprintf(seq, sizeof(seq), "%0*ld", SEQ_DIGITS, my_seq % 100000000);
				memcpy(page_address(pages[0]) + HEADER_LEN, seq, SEQ_DIGITS);
			}
			dev = f;
		}
		if (dev && used > done)
			flush();
		msleep_interruptible(dev ? 100 : 500);
	}
	pr_info("bootlog: stopped after %u ms\n", jiffies_to_msecs(jiffies - start));
	while (kmsg_dump_get_line(&iter, false, line, sizeof(line), &len))
		append(line, len);
	if (dev && used > done)
		flush();
	while (!kthread_should_stop())
		msleep_interruptible(1000);
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
