# Out of tree modules

Built like the display driver, against the lahaina modules_prepare output
(see the top level README), with `make -C kernel_platform/common O=<out> M=<dir>`.

- `usbrole`: keeps the USB controller in device mode. UCSI on the ADSP
  reports nothing connected on 6.12, so without it adb never comes up, and
  once the ADSP runs it drops the role again whenever it looks.
- `touch`: the mainline Goodix Berlin driver, which GKI does not build.
- `speaker`: the mainline AW88261 smart amp driver and its aw88395 ACF
  library, which GKI does not build, plus lib/crc8.c because GKI doesn't
  export it. The amps load redwood's vendor `aw882xx_acf.bin` as
  `aw88261_acf.bin`; the mainline parser reads it as is.
- `bootlog`: keeps the kernel log, which also carries init's messages, on
  `logdump` while testing Android, with a heartbeat every two seconds and a
  process list and the stacks of blocked tasks at 30, 90 and 150 seconds. It writes with bios
  from a kernel thread, so SELinux doesn't stop it, and only writes to a
  partition that starts with zeros. Load it first in `modules.load`.
- `testguard`: load it before anything else whenever 6.12 boots Android.
  It holds `userdata` exclusively and read-only and makes `misc` read-only,
  so 6.12 can neither mount nor write the 5.4 data, nor leave a wipe request
  for recovery; it turns reboots into recovery into reboots to the
  bootloader, sends a recovery boot of 6.12 straight back to the
  bootloader, and reboots to the bootloader if it can't guard both
  partitions within six seconds. A userspace fstab swap can't do this:
  /vendor is only up for about 100 ms before SELinux goes enforcing.
