# Out of tree modules

Built like the display driver, against the lahaina modules_prepare output
(see the top level README), with `make -C kernel_platform/common O=<out> M=<dir>`.

- `usbrole`: keeps the USB controller in device mode. UCSI on the ADSP
  reports nothing connected on 6.12, so without it adb never comes up, and
  once the ADSP runs it drops the role again whenever it looks.
- `touch`: the mainline Goodix Berlin driver, which GKI does not build, plus
  double tap to wake (`dt2w.py`, applied by `stage2.sh`).
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
- `logcap`: copies Android log messages into the kernel log, so bootlog keeps
  them: everything from the crash buffer, W and above from everyone (2000 lines a process), and all
  of zygote, AndroidRuntime, libc, DEBUG and crash_dump. It reads what
  processes send to `/dev/socket/logdw` through a kprobe on
  `unix_dgram_sendmsg`; on a user build no domain that can read logd may
  write `/dev/kmsg`, and liblog only puts security events in pmsg. Load it
  right after bootlog.
- `adsprpc`: the legacy FastRPC driver from redwood's 5.4 kernel, which the
  HyperOS vendor's libadsprpc needs (`/dev/adsprpc-smd` and its ioctls; the
  6.12 dsp-kernel drivers only speak the upstream fastrpc ABI). SSR comes
  from the qcom remoteproc notifiers, PDR from the in-kernel PDR helpers,
  memory assignment from SCM; no ramdumps and no 32-bit ioctls yet. Its DT
  is [qcom/redwood-fastrpc.dtsi](../qcom/redwood-fastrpc.dtsi).
- `video`: the lahaina msm_vidc from video-kernel.lahaina.lnx.1.0 (5.4), the
  V4L2 ABI the vendor's Codec2 components use. ION allocations come from
  the qcom DMA-BUF heaps and venus boots through the MDT loader and PAS
  instead of PIL; the msm bitrate mode CQ keeps its 5.4 value (6.12 gives
  the name to another one). Its DT is
  [qcom/redwood-video.dtsi](../qcom/redwood-video.dtsi).
- `btpower`: the BT power driver from redwood's 5.4 kernel, which the
  vendor BT HAL drives through `/dev/btpower` (BT_EN, SW_CTRL and the
  WCN6750 rails); HCI goes over `/dev/ttyHS0`, which 6.12 already has.
  `bringup/bttest.c` powers the chip and sends an HCI Reset. Its DT is
  [qcom/redwood-bt.dtsi](../qcom/redwood-bt.dtsi).
- `camera` (in the kernel tree, `vendor/camera-kernel`): the 6.12
  camera-kernel built for Titan 570 from its `camera_modules.bzl` source list
  (`Kbuild.redwood`; the stock Kbuild names files this tree doesn't have).
  The sensor and request manager UAPI is put back to the 5.4 layout redwood's
  CamX uses: no `frequency` in `i2c_rdwr_header`, the 5.4 continuous read,
  no `ir_led_slot_id` in the sensor query, 256 link handles. LRME isn't
  built (the bzl has no sources for it). Its DT is
  [qcom/redwood-camera.dtsi](../qcom/redwood-camera.dtsi).
