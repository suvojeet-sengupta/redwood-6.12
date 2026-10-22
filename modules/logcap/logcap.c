// SPDX-License-Identifier: GPL-2.0
/*
 * Copy Android log messages into the kernel log while testing Android on
 * redwood 6.12, so bootlog keeps them too. On a user build nothing that may
 * read logd may also write /dev/kmsg, and liblog leaves pmsg alone, so this
 * looks at what processes send to /dev/socket/logdw instead. It only reads.
 */
#include <linux/hash.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/percpu.h>
#include <linux/socket.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <net/af_unix.h>
#include <net/sock.h>

#define HDR_LEN		11	/* android_log_header_t: id, tid, sec, nsec */
#define MSG_MAX		1024
#define LINE_BUDGET	150000
/* so a daemon retrying in a loop can't use up the budget on its own */
#define PROC_BUDGET	2000
#define PROC_SLOTS	512

enum { LOG_MAIN, LOG_RADIO, LOG_EVENTS, LOG_SYSTEM, LOG_CRASH };

struct capbuf {
	char raw[MSG_MAX];
	char line[MSG_MAX];
};

static DEFINE_PER_CPU(struct capbuf, bufs);
static atomic_t budget = ATOMIC_INIT(LINE_BUDGET);
static DEFINE_SPINLOCK(procs_lock);
static struct {
	pid_t tgid;
	unsigned int lines;
} procs[PROC_SLOTS];

static bool proc_may_log(pid_t tgid)
{
	unsigned int i, slot = hash_32(tgid, ilog2(PROC_SLOTS));
	unsigned long flags;
	bool ok = false;

	spin_lock_irqsave(&procs_lock, flags);
	for (i = 0; i < PROC_SLOTS; i++, slot = (slot + 1) % PROC_SLOTS) {
		if (procs[slot].tgid == tgid || !procs[slot].tgid) {
			procs[slot].tgid = tgid;
			ok = procs[slot].lines++ < PROC_BUDGET;
			if (procs[slot].lines == PROC_BUDGET + 1)
				printk(KERN_INFO "logcat: %d hit its line budget\n", tgid);
			break;
		}
	}
	spin_unlock_irqrestore(&procs_lock, flags);
	return ok;
}

static const char *const tags[] = {
	"zygote", "Zygote", "AndroidRuntime", "libc", "DEBUG", "crash_dump",
};

static bool to_logd(struct sock *sk)
{
	struct sock *peer = unix_peer(sk);
	struct unix_address *addr;

	if (!peer)
		return false;
	addr = READ_ONCE(unix_sk(peer)->addr);
	return addr && addr->len > offsetof(struct sockaddr_un, sun_path) &&
	       !strncmp(addr->name->sun_path, "/dev/socket/logdw", sizeof("/dev/socket/logdw"));
}

static size_t copy_payload(struct iov_iter *it, char *dst, size_t max)
{
	size_t got = 0, skip = it->iov_offset;
	unsigned long i;

	if (iter_is_ubuf(it)) {
		size_t n = min(it->count, max);

		return copy_from_user_nofault(dst, it->ubuf + skip, n) ? 0 : n;
	}
	if (!iter_is_iovec(it))
		return 0;
	for (i = 0; i < it->nr_segs && got < max; i++) {
		const struct iovec *v = iter_iov(it) + i;
		size_t n = v->iov_len - skip;

		n = min(n, max - got);
		if (n && copy_from_user_nofault(dst + got, v->iov_base + skip, n))
			break;
		got += n;
		skip = 0;
	}
	return got;
}

static bool wanted(int id, int prio, const char *tag)
{
	int i;

	if (id == LOG_CRASH || prio >= 5)
		return true;
	for (i = 0; i < ARRAY_SIZE(tags); i++)
		if (!strncmp(tag, tags[i], strlen(tags[i])))
			return true;
	return false;
}

static int pre_sendmsg(struct kprobe *p, struct pt_regs *regs)
{
	struct socket *sock = (struct socket *)regs->regs[0];
	struct msghdr *msg = (struct msghdr *)regs->regs[1];
	struct capbuf *b;
	const char *tag, *text;
	size_t n, tlen, i, o;
	int id, prio;

	if (!sock || !sock->sk || msg->msg_name || atomic_read(&budget) <= 0 || !to_logd(sock->sk))
		return 0;
	b = this_cpu_ptr(&bufs);
	n = copy_payload(&msg->msg_iter, b->raw, MSG_MAX - 1);
	if (n < HDR_LEN + 3)
		return 0;
	b->raw[n] = 0;
	id = b->raw[0];
	if (id != LOG_MAIN && id != LOG_RADIO && id != LOG_SYSTEM && id != LOG_CRASH)
		return 0;
	prio = b->raw[HDR_LEN];
	tag = b->raw + HDR_LEN + 1;
	tlen = strnlen(tag, n - HDR_LEN - 1);
	if (HDR_LEN + 1 + tlen + 1 >= n || !wanted(id, prio, tag))
		return 0;
	if (id != LOG_CRASH && !proc_may_log(current->tgid))
		return 0;
	text = tag + tlen + 1;
	for (i = 0, o = 0; text[i] && o < MSG_MAX - 4; i++) {
		if (text[i] == '\n') {
			b->line[o++] = ' ';
			b->line[o++] = '|';
			b->line[o++] = ' ';
		} else {
			b->line[o++] = text[i];
		}
	}
	b->line[o] = 0;
	atomic_dec(&budget);
	printk(KERN_INFO "logcat: %c %d %s: %s\n", "??VDIWEFS"[clamp(prio, 0, 8)],
	       current->tgid, tag, b->line);
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "unix_dgram_sendmsg",
	.pre_handler = pre_sendmsg,
};

static int __init logcap_init(void)
{
	int ret = register_kprobe(&kp);

	if (ret)
		pr_err("logcap: can't probe unix_dgram_sendmsg: %d\n", ret);
	else
		pr_info("logcap: copying Android logs into the kernel log\n");
	return ret;
}

static void __exit logcap_exit(void)
{
	unregister_kprobe(&kp);
}

module_init(logcap_init);
module_exit(logcap_exit);
MODULE_DESCRIPTION("Android log messages in the kernel log for redwood 6.12 bring-up");
MODULE_LICENSE("GPL");
