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

## usbcatcher.c

Sets up a USB NCM and ACM gadget and serves the kernel log on TCP port 2323.
It needs the USB role from the charger firmware on the ADSP, so it does not
work yet.

## splashcon

A module that draws the kernel log on the framebuffer the bootloader left on
screen. It finds the scanout buffer from the display pipe registers, but the
picture doesn't change yet.
