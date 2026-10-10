# Out of tree modules

Built like the display driver, against the lahaina modules_prepare output
(see the top level README), with `make -C kernel_platform/common O=<out> M=<dir>`.

- `usbrole`: keeps the USB controller in device mode. UCSI on the ADSP
  reports nothing connected on 6.12, so without it adb never comes up, and
  once the ADSP runs it drops the role again whenever it looks.
- `touch`: the mainline Goodix Berlin driver, which GKI does not build.
