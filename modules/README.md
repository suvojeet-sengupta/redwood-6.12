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
