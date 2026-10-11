# redwood on Linux 6.12

Work on running Qualcomm's 6.12 kernel platform (CodeLinaro
`KERNEL.PLATFORM.5.0.r35-02100-kernel.0`) on the Redmi Note 12 Pro 5G /
POCO X5 Pro 5G (redwood, SM7325).

This repository is Qualcomm's devicetree (`kernel.lnx.6.12.r45-rel`) with
redwood added on top. It goes in the kernel platform tree as
`kernel_platform/qcom/opensource/devicetree`.

## Status

The detailed checklist is in [STATUS.md](STATUS.md).

| | |
|---|---|
| Kernel boots | yes |
| First stage modules (91) | load |
| UFS storage | works |
| USB | works with the role forced to device |
| Display | works: panel at 1080x2400 120 Hz with the 6.12 display driver |
| GPU | works: Adreno 642L powers up with kgsl |
| Touch | works: Goodix Berlin-D with the mainline goodix_berlin driver |
| Android | boots to the setup wizard (with the test workarounds below) |
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

The 5.4 era `libgsl` gives the size of the whole command list as the
entry size in `IOCTL_KGSL_GPU_COMMAND` (64 for two 32 byte objects). The
6.12 driver's `copy_struct_from_user()` takes the second entry for
trailing garbage and fails with `-EFAULT`, so apps hang in RenderThread
and ANR. `0002` in [patches/graphics-kernel](patches/graphics-kernel)
copies like the 5.4 driver did.

## Touch

redwood's Goodix Berlin-D works with the mainline `goodix_berlin` driver
from `kernel_platform/common/drivers/input/touchscreen`. GKI doesn't build
it, so build `goodix_berlin_core.c` and `goodix_berlin_spi.c` out of tree
with `obj-m += goodix_berlin_core.o goodix_berlin_spi.o`. The device tree
is in `qcom/redwood-touch.dtsi`.

## TrustZone

Keymaster, gatekeeper and vold reach TrustZone through `qseecom` and
`smcinvoke`, which on 6.12 live in `securemsm-kernel`
(`LA.VENDOR.16.2.8.r1-06100-LAHAINA.0`). Apply
[patches/securemsm-kernel](patches/securemsm-kernel) and build it like the
display driver, with `SSG_MODULE_ROOT` and `CONFIG_DISABLE_TEST_MODULES=y`
plus `CONFIG_QSEECOM=m` (lahaina leaves it off, but redwood's vendor still
opens `/dev/qseecom`). Its tracepoints include headers through
`vendor/qcom/opensource/securemsm-kernel` next to `kernel_platform`, so link
the tree there.

## Android

With the 5.4 vendor ramdisk plus every 6.12 module in it, Android's own
init loads all modules, mounts system, system_ext, product and vendor and
gets through most of second stage. Things that matter:

- Pass `clk_ignore_unused pd_ignore_unused`. The bootloader passes
  `msm_drm.dsi_display0`, so the display driver takes over the splash
  screen, and without these the clock and power domain sync_state turns
  the display hardware off underneath it.
- Without `qseecom`/`smcinvoke` keymaster never comes up and vold waits
  forever to mount /data (see TrustZone above).
- Leave `tz_log` out: probing it resets the phone on the spot.
- redwood's 5.4 vendor allocates TrustZone buffers through ION
  (`libQSEEComAPI` uses `libion`), and 6.12 has no ION.
  [bringup/ionshim.c](bringup/ionshim.c) is a `libion` that hands every ION
  allocation to the matching DMA-BUF heap (the qseecom heap to
  `qcom,qseecom` and so on). The vendor's `ueventd.rc` only opens up the
  AOSP heaps, so the `qcom,*` heap nodes also need `0444 system system`, or
  the allocations quietly land in the system heap and TrustZone turns the
  listener buffers down with `-EINVAL`. For testing, logsaver bind mounts
  both over `/vendor` in RAM.
- With that, keymaster and the listeners work and vold sets up the
  metadata encryption.
- redwood's fstab uses hardware wrapped keys, and lahaina has the v1
  hardware key manager. Qualcomm's lahaina config leaves
  `CONFIG_QTI_HW_KEY_MANAGER_V1` off, so the ICE driver asks TrustZone to
  program the key the v2 way and TrustZone answers `-EINVAL`
  (`qcom_ice_program_wrapped_key: SCM call Error: 0xffffffea`). With
  [patches/soc-repo](patches/soc-repo) the kernel drives the HWKM itself
  like 5.4 does, and /data decrypts.
- While testing, keep /data read-only: logsaver bind mounts
  [bringup/fstab.test](bringup/fstab.test), the vendor fstab with
  `ro,norecovery` on /data and without `check` and `formattable`, and
  reboots to the bootloader if it can't. If init lands in recovery, never
  format data from there: /data is shared with the working slot.

- The vendor composer needs the DMA pipes. The 6.12 display driver only
  sets them up when every `dgm` sub-block in `qcom,sde-sspp-dma-blocks` has
  a `cell-index`; without it there are two planes and SDM fails with
  `DisplayBase::Init: Display 59 comp manager registration failed!`.
- The media profiles are picked from the video driver's `sku_version`,
  which doesn't exist yet on 6.12, so `init.qti.media.sh` chooses
  `_yupik_v0`, which has no XML, and zygote aborts in `MediaProfiles`.
  For now logsaver bind mounts [bringup/media.sh](bringup/media.sh), which
  picks `_yupik_v1`, until the video driver is ported.
- Booting 6.12 with /data writable left the shared /data unusable for the
  5.4 system on slot a. Only test that way with nothing to lose on /data.

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
