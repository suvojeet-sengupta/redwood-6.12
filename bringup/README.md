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

## kmsbars.c

Shows colour bars on the panel with plain KMS ioctls. With `msm_drm.ko`
loaded and `/dev/dri/card0` made (`mknod /dev/dri/card0 c 226 0`), it
takes the DSI connector's first mode and puts the bars on screen.

## kgslinfo.c

Opens `/dev/kgsl-3d0`, which powers the GPU up, and prints what kgsl
reports about it. Make the node from `/sys/class/kgsl/kgsl-3d0/dev` first.

## splashcon

A module that draws the kernel log on the framebuffer the bootloader left on
screen. It finds the scanout buffer from the display pipe registers, but the
picture doesn't change. Not needed any more now that the display driver works.
