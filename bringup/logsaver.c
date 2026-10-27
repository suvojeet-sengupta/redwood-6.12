// /init wrapper for testing Android on 6.12: fork a child that puts the
// test shims over /vendor once first stage init has mounted it, then exec
// Android's first stage init as PID 1. modules/bootlog keeps the log.
#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/*
 * The 5.4 vendor allocates through ION, which 6.12 does not have. Once
 * Android's init has /vendor mounted, bind mount a libion that uses the
 * DMA-BUF heaps over the vendor's, in init's mount namespace. It only
 * lives in RAM; the vendor partition stays as it is. Since
 * modules/ioncompat gives a real /dev/ion, the ramdisk no longer carries
 * the libion shims and the vendor's own libion is used: a shim that misses
 * its label in the race below breaks everything that allocates.
 */
static int shim_fd[4] = { -1, -1, -1, -1 };
/* fstab.test mounts /data read-only, so a broken key setup can't write to it */
/* media.sh picks the yupik v1 media profiles, which the missing video driver would report */
static const char *shim_src[4] = { "/ionshim64.so", "/ionshim32.so", "/fstab.test", "/media.sh" };
static const char *shim_tmp[4] = { "/dev/.shims/ionshim64.so", "/dev/.shims/ionshim32.so", "/dev/.shims/fstab.test", "/dev/.shims/media.sh" };
static const char *shim_dst[4] = { "/vendor/lib64/libion.so", "/vendor/lib/libion.so", "/vendor/etc/fstab.default", "/vendor/bin/init.qti.media.sh" };
static const char *shim_label[6] = {
	"u:object_r:same_process_hal_file:s0", "u:object_r:same_process_hal_file:s0",
	"u:object_r:vendor_configs_file:s0", "u:object_r:vendor_qti_media_exec:s0",
	"u:object_r:vendor_configs_file:s0", "u:object_r:system_file:s0",
};
#define NLABELS 6
static int se_dir = -1;

/*
 * The copies live on init's /dev tmpfs, which gets labelled by transition
 * when the policy loads, whatever xattr they had. Give them the labels of
 * the files they cover in the short gap between the policy load and init
 * turning enforcing on. selinuxfs shows the new policy before it is
 * committed, so keep at it until the label reads back.
 */
static void label_shims(int wfd)
{
	static const char *tmp[NLABELS] = { "/dev/.shims/ionshim64.so", "/dev/.shims/ionshim32.so", "/dev/.shims/fstab.test",
					    "/dev/.shims/media.sh", "/dev/.shims/ueventd.rc", "/dev/.shims/build.prop" };
	char cur[128], enf[2];
	int i, n, ok = 0, err = 0;

	if (se_dir < 0)
		return;
	for (n = 0; n < 20000; n++) {
		if (!faccessat(se_dir, "class/file", F_OK, 0))
			break;
		usleep(500);
	}
	int efd = openat(se_dir, "enforce", O_RDONLY | O_CLOEXEC);

	for (n = 0; n < 4000 && ok < NLABELS; n++) {
		if (efd >= 0 && pread(efd, enf, 1, 0) == 1 && enf[0] == '1')
			break;
		for (i = 0, ok = 0; i < NLABELS; i++) {
			ssize_t len;

			/* shims this ramdisk doesn't carry */
			if (access(tmp[i], F_OK)) {
				ok++;
				continue;
			}

			if (setxattr(tmp[i], "security.selinux", shim_label[i], strlen(shim_label[i]) + 1, 0))
				err = errno;
			len = getxattr(tmp[i], "security.selinux", cur, sizeof(cur) - 1);
			cur[len > 0 ? len : 0] = 0;
			if (!strcmp(cur, shim_label[i]))
				ok++;
		}
		if (ok < NLABELS)
			usleep(250);
	}
	dprintf(wfd, "<6>logsaver: labelled %d of %d shims after %d tries (last error %d)\n", ok, NLABELS, n, err);
}

static int place_shims(int wfd)
{
	pid_t pid;
	int st;

	if (access("/ls/proc/1/root/vendor/lib64/libion.so", F_OK))
		return 0;
	pid = fork();
	if (pid == 0) {
		int ns = open("/ls/proc/1/ns/mnt", O_RDONLY | O_CLOEXEC);
		int root = open("/ls/proc/1/root", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		int i;

		if (ns < 0 || root < 0 || setns(ns, CLONE_NEWNS) || fchdir(root) || chroot(".")) {
			dprintf(wfd, "<3>logsaver: can't enter init's mounts: %d\n", errno);
			_exit(1);
		}
		/* not on /dev itself: it is nosuid, which rules out the domain
		 * transition init.qti.media.sh needs */
		mkdir("/dev/.shims", 0755);
		if (mount("tmpfs", "/dev/.shims", "tmpfs", MS_NODEV, "mode=0755"))
			dprintf(wfd, "<3>logsaver: no tmpfs for the shims: %d\n", errno);
		for (i = 0; i < 4; i++) {
			char b[65536];
			ssize_t n;
			off_t off = 0;
			int out = open(shim_tmp[i], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);

			if (out < 0 || shim_fd[i] < 0) {
				if (i == 2)
					goto unsafe;
				continue;
			}
			while ((n = pread(shim_fd[i], b, sizeof(b), off)) > 0) {
				if (write(out, b, n) != n)
					break;
				off += n;
			}
			close(out);
			if (mount(shim_tmp[i], shim_dst[i], NULL, MS_BIND, NULL)) {
				dprintf(wfd, "<3>logsaver: bind %s failed: %d\n", shim_dst[i], errno);
				if (i == 2)
					goto unsafe;
			} else
				dprintf(wfd, "<6>logsaver: %s on %s\n", shim_src[i], shim_dst[i]);
		}
		/*
		 * Test boots only: adb without a key. /data is in RAM, so every
		 * boot is a first boot and adb is off on this user build; this
		 * copy of the ROM's build.prop turns adb on and drops the key
		 * check, in RAM, for 6.12 test boots only.
		 */
		{
			static const char secure[] = "ro.adb.secure=1";
			static const char extra[] = "\npersist.sys.usb.config=mtp,adb\n";
			static char bp[65536];
			int in = open("/system/build.prop", O_RDONLY | O_CLOEXEC);
			ssize_t n = in >= 0 ? read(in, bp, sizeof(bp) - sizeof(extra)) : -1;
			char *p;
			int out;

			if (in >= 0)
				close(in);
			if (n > 0) {
				bp[n] = 0;
				p = strstr(bp, secure);
				if (p)
					p[sizeof(secure) - 2] = '0';
				memcpy(bp + n, extra, sizeof(extra) - 1);
				n += sizeof(extra) - 1;
			}
			out = n > 0 ? open("/dev/.shims/build.prop", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
			if (out >= 0) {
				if (write(out, bp, n) == n &&
				    !mount("/dev/.shims/build.prop", "/system/build.prop", NULL, MS_BIND, NULL))
					dprintf(wfd, "<6>logsaver: build.prop with adb on\n");
				else
					dprintf(wfd, "<3>logsaver: build.prop bind failed: %d\n", errno);
				close(out);
			} else {
				dprintf(wfd, "<3>logsaver: no /system/build.prop to copy: %d\n", errno);
			}
		}
		label_shims(wfd);
		_exit(0);
unsafe:
		/* never let /data get mounted writable during these tests */
		dprintf(wfd, "<3>logsaver: no read-only fstab, rebooting to the bootloader\n");
		sync();
		syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
			LINUX_REBOOT_CMD_RESTART2, "bootloader");
		_exit(1);
	}
	if (pid < 0)
		return 0;
	waitpid(pid, &st, 0);
	return 1;
}

static void saver(void)
{
	int wfd, i;

	/* keep these mounts out of init's way: first stage init moves every
	 * mount it sees into the new root and dies if one of them fails */
	unshare(CLONE_NEWNS);
	mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
	mkdir("/ls", 0700);
	mkdir("/ls/proc", 0700);
	mount("proc", "/ls/proc", "proc", 0, NULL);
	mkdir("/ls/se", 0700);
	if (!mount("selinuxfs", "/ls/se", "selinuxfs", 0, NULL))
		se_dir = open("/ls/se", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	mknod("/ls/kmsg", S_IFCHR | 0600, makedev(1, 11));
	wfd = open("/ls/kmsg", O_WRONLY | O_CLOEXEC);
	for (i = 0; i < 4; i++)
		shim_fd[i] = open(shim_src[i], O_RDONLY | O_CLOEXEC);

	dprintf(wfd, "<6>logsaver: waiting for /vendor\n");
	/* there is only about 100 ms between first stage init mounting /vendor
	 * and second stage init turning SELinux enforcing on, after which this
	 * process, still in the kernel domain, can't reach init's mounts */
	sched_setscheduler(0, SCHED_FIFO, &(struct sched_param){ .sched_priority = 99 });
	for (i = 0; i < 60000; i++) {
		if (place_shims(wfd))
			return;
		usleep(1000);
	}
	dprintf(wfd, "<3>logsaver: /vendor never showed up, no shims\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	if (fork() == 0) {
		setsid();
		saver();
		_exit(0);
	}
	execv("/init.android", argv);
	return 127;
}
