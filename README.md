# redwood on Linux 6.12

Work on running Qualcomm's 6.12 kernel platform (CodeLinaro
`KERNEL.PLATFORM.5.0.r35-02100-kernel.0`) on the Redmi Note 12 Pro 5G /
POCO X5 Pro 5G (redwood, SM7325).

This repository is Qualcomm's devicetree (`kernel.lnx.6.12.r45-rel`) with
redwood added on top. It goes in the kernel platform tree as
`kernel_platform/qcom/opensource/devicetree`.

## Status

| | |
|---|---|
| Kernel boots | yes |
| First stage modules (91) | load |
| UFS storage | works |
| USB | works with the role forced to device |
| Display | not yet |
| Android | not yet |

## What redwood needs on top of Yupik IDP

`qcom/redwood-overlay.dtso`:

- redwood's msm-id and board-id, so the bootloader picks it
- the memory map redwood's firmware expects (AOP, ADSP, CDSP and CVP regions
  differ from Qualcomm's 6.12 map)
- the PMR735A and the SD card controller disabled
- a fixed ramoops region at `0xb0000000`

## Building

```
cd kernel_platform
python3 build_with_bazel.py -t lahaina perf --skip abl
```

The modules Kleaf puts in `out/msm-kernel-lahaina-perf/dist` are not
stripped. Strip them with `llvm-strip --strip-debug` before packing a vendor
ramdisk, or the bootloader refuses it.

## Testing

See [bringup](bringup/README.md). Tests go on slot b only, so slot a keeps a
working system to read the logs from.

Written and maintained by Suvojeet Sengupta.
