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
| Display | works: panel at 1080x2400 120 Hz with the 6.12 display driver |
| GPU | works: Adreno 642L powers up with kgsl |
| Android | not yet |
| Shell over USB | yes, see bringup |

## What redwood needs on top of Yupik IDP

`qcom/redwood-overlay.dtso`:

- redwood's msm-id and board-id, so the bootloader picks it
- the memory map redwood's firmware expects (AOP, ADSP, CDSP and CVP regions
  differ from Qualcomm's 6.12 map)
- the PMR735A and the SD card controller disabled
- a fixed ramoops region at `0xb0000000`

## Vendor modules

Build Qualcomm's vendor drivers against the lahaina config, not the GKI
one: copy the kernel's modules_prepare output and put Kleaf's
`lahaina_perf_config/out_dir/.config`, `include/generated/autoconf.h` and
`include/config` in it. Against the GKI config, options like
`CONFIG_QCOM_COMMAND_DB` are off, the soc-repo headers hand out stubs that
always fail, and things like the GPU's RPMh tables quietly break.

## Display

Qualcomm's 6.12 display devicetree has no Yupik, so `qcom/display/` ports
it from the 5.4 kernel, along with redwood's two M20 panels. The driver is
`display-drivers` from `display-kernel.lnx.12.13.r1-rel`, which still knows
SDE 7.2 (Yupik), with
[patches/display-drivers](patches/display-drivers) applied. Build it
against the kernel's modules_prepare output:

```
make -C kernel_platform/common O=<modules_prepare out> M=<display-drivers> \
	ARCH=arm64 LLVM=1 LLVM_IAS=1 DISPLAY_ROOT=<display-drivers> \
	CONFIG_ARCH_LAHAINA=y CONFIG_ARCH_YUPIK=y CONFIG_DRM_MSM=m \
	KBUILD_EXTRA_SYMBOLS=<soc-repo module symvers> \
	SOCINCLUDE="-I<soc-repo>/include -I<soc-repo>/include/uapi" modules
```

`mm-drivers` from the same branch has to sit next to it for its headers,
and `gendwarfksyms` needs `LD_LIBRARY_PATH` pointing at
`prebuilts/kernel-build-tools/linux-x86/lib64`.

## GPU

`graphics-kernel` from `gfx-kernel.lnx.16.0.r14-rel` knows the Adreno 642L.
Its devicetree comes from `graphics-devicetree`
(`gfx-devicetree-oss.lnx.16.0.r8-rel`), whose `yupik-gpu.dtsi` is in
`qcom/gpu/` as is. Apply [patches/graphics-kernel](patches/graphics-kernel)
and build it like the display driver, with `KGSL_PATH` instead of
`DISPLAY_ROOT` and the `mm-drivers` `hw_fence` and `sync_fence` headers in
`SOCINCLUDE`. It needs `a660_sqe.fw`, `a660_gmu.bin` and `a660_zap.*` from
`/vendor/firmware`.

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
