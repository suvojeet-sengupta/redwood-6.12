# Build scripts

These turn a [redwood-6.12-kernel](https://github.com/suvojeet-sengupta/redwood-6.12-kernel)
tree into a kernel, modules and images for redwood. They expect:

- the kernel source at `~/redwood-6.12-kernel` (branch `main`),
- this repository at `~/redwood-6.12`,
- scratch space in `~/work`, with the 5.4 ROM's ramdisks unpacked in
  `~/work/android` for the packing scripts,
- `~/k612`, which `setup-k612.sh` fills with what out-of-tree modules build
  against.

## The usual way

```
bash ~/redwood-6.12/scripts/build-all.sh          # everything below, in order
bash ~/redwood-6.12/scripts/make-prebuilt.sh <prebuilt repo> <5.4 prebuilt>
```

`build-all.sh` stops at the first stage that fails; `FROM=<stage>` restarts
from there. `watch-build.sh` shows its progress live.

## Building

| Script | Builds |
|---|---|
| `stage1.sh` | The kernel, first stage modules and device trees (Kleaf), then the audio modules |
| `stage-ipa.sh` | Mobile data: dataipa, rmnet and rmnet_mem |
| `setup-k612.sh` | The tree out-of-tree modules build against, and the soc-repo symbol list |
| `stage2.sh` | Display, GPU, TrustZone, touch, `subsys_compat`, `usbrole`, speaker amps |
| `build-small.sh` | Speaker amps, FastRPC and the test helpers |
| `build-video.sh` | Video decoder and encoder |
| `build-btpower.sh` | Bluetooth power control |
| `build-camera54.sh` | The 5.4 camera driver, which redwood's camera userspace speaks to |
| `build-camera.sh` | Qualcomm's 6.12 camera driver (not in `build-all.sh`; redwood uses the 5.4 one) |
| `build-fingerprint.sh` | Goodix and Xiaomi fingerprint drivers |
| `build-haptics.sh` | The AW86927 vibrator |
| `build-ioncompat.sh` | `/dev/ion` for the 5.4 vendor libraries |
| `build-irspi.sh` | The IR blaster |
| `build-mithermal.sh` | Xiaomi's thermal nodes for `mi_thermald` |
| `build-xiaomitouch.sh` | Xiaomi's touch feature class |
| `build-platform.sh`, `build-qcacld.sh` | Wi-Fi: the WCN6750 platform driver and `wlan.ko` |
| `dtbo-fast.sh` | Only `dtbo.img`, in seconds, for device tree changes |

## Packing

| Script | Makes |
|---|---|
| `make-prebuilt.sh` | Fills [device_xiaomi_redwood-kernel-6.12](https://github.com/suvojeet-sengupta/device_xiaomi_redwood-kernel-6.12) for ROM builds |
| `pack-rom612.sh` | New boot and vendor_boot for a phone already on the 6.12 ROM: its own images with the new kernel and modules swapped in. `DROP=` and `KEEP=` leave modules out or keep test ones |
| `pack-real.sh` | 6.12 boot images for an installed ROM, with its own ramdisks and real `/data` |
| `pack-android.sh` | Test images that boot the 5.4 ROM on 6.12 with `/data` in RAM, so nothing on the phone is touched |
| `pack.sh` | The USB shell images from the early bring-up |

`pack-android.sh` needs `v54` (vendor ramdisk), `b54` (boot ramdisk) and
`vendor_cmdline.txt` in `~/work/android`, plus `~/work/aw88261_acf.bin`
from the vendor firmware.
