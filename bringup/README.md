# Bring-up tools

Used while the Android userspace doesn't run on 6.12 yet.

## modload.c

A static `/init` for the boot ramdisk. It loads the first stage modules the
way Android's init does, dependencies first from `modules.dep`, writes the
kernel log to the `logdump` partition and reboots to the bootloader. If it
can't save the log it crashes on purpose, and the bootloader then keeps the
first seconds of the boot log in the `minidump` partition.

```
aarch64-linux-android34-clang -static -O2 -o init modload.c
```

Put the modules in `lib/modules/<release>/`, run `depmod -b <ramdisk> <release>`
and pack it with the init as the boot ramdisk.

Read the log back from a system on slot a:

```
dd if=/dev/block/by-name/logdump bs=1M count=8 | tr -d "\0"
```

Things to know about redwood:

- The bootloader clears RAM on every reboot, so pstore/ramoops never keeps
  anything, not even on 5.4.
- The GKI kernel has no devtmpfs. The init has to `mknod` `/dev/kmsg` and the
  block devices it needs.

## logsaver.c

An `/init` wrapper for booting the real Android ramdisk: rename Android's
`init` to `init.android` and put this in its place. It forks a child that
writes the kernel log, which also carries init's messages, to `logdump`
as it comes in, with a heartbeat every two seconds and blocked task and
CPU backtraces at 30 and 90 seconds, then execs Android's init.

The child mounts sysfs and proc in its own mount namespace. First stage
init moves every mount it can see into the new root and kills the boot if
one fails, so a plain mount there ends in "Attempted to kill init".

It also bind mounts [ionshim.c](ionshim.c), built with the NDK as
`libion.so` for arm64 and arm, over `/vendor/lib64/libion.so` and
`/vendor/lib/libion.so`, and a copy of the vendor's `ueventd.rc` with a
rule for the `qcom,*` DMA-BUF heaps over the original, from inside init's
mount namespace once `/vendor` is up. Put the shims at `/ionshim64.so` and
`/ionshim32.so` in the boot ramdisk.

```
aarch64-linux-android30-clang -shared -fPIC -O2 -Wl,-soname,libion.so -o ionshim64.so ionshim.c
armv7a-linux-androideabi30-clang -shared -fPIC -O2 -Wl,-soname,libion.so -o ionshim32.so ionshim.c
```

redwood has two partitions named `logdump` (`sda29` and `sde56`). Which one
shows up first changes from boot to boot, so read both back.

## shell.c, rb.c, rsh.py

The init used for most work now: everything usbtest.c does, then a toybox
shell on TCP port 2323 over the USB network. The phone is `192.168.90.1`
and `fe80::ff:fe61:2001`. It keeps running; `rb` in the shell reboots to
the bootloader.

toybox comes from `kernel_platform/external/toybox`, built static with the
NDK compiler, `sh` turned on and `su`, `login`, `passwd` and `mkpasswd` turned
off (bionic has no `crypt()`). `make install_flat PREFIX=<ramdisk>/bin` puts
the links in place.

From the computer:

```
python3 rsh.py <usb interface> "dmesg | tail"
```

## usbtest.c

Loads the first stage and vendor_dlkm modules, mounts `modem_a` for the
firmware, starts the ADSP and sets up a USB ACM + NCM gadget. The ADSP boots
and UCSI answers, but it never registers the cable partner, so the USB role
stays at none: `GET_CONNECTOR_STATUS` from the ADSP says nothing is
connected. Writing `device` to
`/sys/class/usb_role/a600000.ssusb-role-switch/role` makes the phone show up
on the computer as an ACM serial port and an NCM network adapter.

## usbcatcher.c

Sets up a USB NCM and ACM gadget and serves the kernel log on TCP port 2323.
Superseded by usbtest.c.

## sndtest.c

Lists and sets mixer controls and plays a sine on a PCM device, for
testing audio from the shell without tinyalsa.

```
aarch64-linux-android34-clang -static -O2 -o sndtest sndtest.c -lm
sndtest ctl
sndtest set "RX_CDC_DMA_RX_0 Audio Mixer MultiMedia1" 1
sndtest play 0 0 3 48000 2 1000
```

## Modem: rmtfs, tqftpserv, qrtr-lookup

The modem needs a remote storage server for its EFS and a TFTP server for
its config files; the pd-mapper and qrtr name service are in the kernel.
The shell uses the linux-msm tools, built static with the NDK:

- [qrtr](https://github.com/linux-msm/qrtr) for libqrtr and `qrtr-lookup`
- [rmtfs](https://github.com/linux-msm/rmtfs), with
  [udev-shim](udev-shim) standing in for libudev (it only reads two sysfs
  attributes of the `rmtfs` UIO device)
- [tqftpserv](https://github.com/linux-msm/tqftpserv) with `-DANDROID` and
  [patches/tqftpserv](patches/tqftpserv)

```
CC="aarch64-linux-android34-clang -static -O2"
Q="-Iqrtr/include -Iqrtr/lib qrtr/lib/qrtr.c qrtr/lib/qmi.c qrtr/lib/logging.c"
$CC -include endian.h -Iudev-shim -Irmtfs $Q udev-shim/udev.c rmtfs/{qmi_rmtfs,rmtfs,rproc,sharedmem,storage,util}.c -o rmtfs
$CC -DANDROID -Itqftpserv $Q tqftpserv/{tqftpserv,translate}.c -o tqftpserv
$CC -Iqrtr/src $Q qrtr/src/lookup.c -o qrtr-lookup
```

modemst1, modemst2, fsg and fsc are shared with the daily system, so never
let the modem on 6.12 write them. Back them up from the daily system, copy
them into RAM in the shell as `modem_fs1`, `modem_fs2`, `modem_fsg` and
`modem_fsc` and run rmtfs read-only from there:

```
mknod /dev/qcom_rmtfs_uio1 c <major:minor of the uio named rmtfs>
mkdir -p /data/vendor/tmp/tqftpserv
rmtfs -o /tmp/efs -r -v &
tqftpserv &
echo start > /sys/class/remoteproc/<4080000.remoteproc-mss>/state
```

## qmitool.c

A small QMI client over QRTR for checking the modem from the shell: SIM
card status, NAS registration and signal, the DMS operating mode, or any
raw request.

```
$CC $Q qmitool.c -o qmitool
qmitool sim
qmitool nas
qmitool mode
qmitool raw 3 0x4f
```

## kmsbars.c

Shows colour bars on the panel with plain KMS ioctls. With `msm_drm.ko`
loaded and `/dev/dri/card0` made (`mknod /dev/dri/card0 c 226 0`), it
takes the DSI connector's first mode and puts the bars on screen.

## kgslinfo.c

Opens `/dev/kgsl-3d0`, which powers the GPU up, and prints what kgsl
reports about it. Make the node from `/sys/class/kgsl/kgsl-3d0/dev` first.

## evdump.c

Prints finger down/up and X/Y from an evdev node, to check the
touchscreen.

## splashcon

A module that draws the kernel log on the framebuffer the bootloader left on
screen. It finds the scanout buffer from the display pipe registers, but the
picture doesn't change. Not needed any more now that the display driver works.
