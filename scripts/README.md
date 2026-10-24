# Build scripts

The scripts that turn a [redwood-6.12-kernel](https://github.com/suvojeet-sengupta/redwood-6.12-kernel)
tree into redwood's test images. They expect the kernel tree at
`~/redwood-6.12-kernel`, this repo at `~/redwood-6.12` and run from `~/work`
(copy them there).

| Script | Builds |
|---|---|
| `stage1.sh` | Kernel, first stage modules and DTBs (`build_with_bazel.py`), then the audio modules |
| `stage-ipa.sh` | dataipa, rmnet and rmnet_mem |
| `setup-k612.sh` | `~/k612/mp-lahaina`, the tree out-of-tree modules build against, and the soc-repo symbol list |
| `stage2.sh` | Display, GPU, TrustZone, touch, `subsys_compat`, `usbrole` |
| `build-small.sh` | Speaker amps, FastRPC, `bootlog`, `testguard`, `logcap` |
| `build-video.sh`, `build-btpower.sh`, `build-camera.sh` | Video, Bluetooth power, camera |
| `dtbo-fast.sh` | `redwood-overlay.dtbo` and `dtbo.img`, without Kleaf |
| `build-all.sh` | All of the above in order; `FROM=<script>` starts later |
| `pack-android.sh` | boot, vendor_boot and dtbo for Android on slot b |
| `pack.sh` | Images for the bring-up shell |

Wi-Fi builds with [../modules/wlan](../modules/wlan).

`pack-android.sh` needs the ROM's 5.4 ramdisks unpacked in `~/work/android`:
`v54` (vendor ramdisk), `b54` (boot ramdisk) and `vendor_cmdline.txt`, plus
`~/work/aw88261_acf.bin` from the vendor firmware.
