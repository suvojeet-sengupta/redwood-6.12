# redwood-6.12

Build scripts, extra drivers and bring-up tools for running **Linux 6.12** on
the **Redmi Note 12 Pro 5G / POCO X5 Pro 5G** (`redwood`, SM7325).

Three repositories make up the 6.12 kernel:

| Repository | What it holds |
|---|---|
| [redwood-6.12-kernel](https://github.com/suvojeet-sengupta/redwood-6.12-kernel) (`r48`) | The kernel source, redwood's device tree included |
| **redwood-6.12** (this one) | The scripts that build and pack it, drivers that live outside the kernel tree, test tools and notes |
| [device_xiaomi_redwood-kernel-6.12](https://github.com/suvojeet-sengupta/device_xiaomi_redwood-kernel-6.12) | The finished kernel, modules and `dtbo.img` a ROM build uses |

If you just want to build a ROM, you only need the last one, and the
redwood device tree's `clo-6.12` branch already uses it.

## Where it stands

The 6.12 ROM (AOSPA) boots and is usable as a daily phone: display at 120 Hz,
touch, GPU, Wi-Fi, Bluetooth, SIMs and mobile data, speakers, microphones,
video, charging, battery readings and vibration all work. Camera,
fingerprint and the IR blaster are ported but need more testing.

Still open:

- Widevine L1 and `feature_enabler_client`: the 6.12 `smcinvoke` driver
  changed its ioctl layout, which the 5.4 vendor libraries don't speak yet.
- `memlat` doesn't load (redwood's CPUCP firmware lacks the SCMI protocols).
- Wi-Fi runs without runtime power management: with it on, a WCN6750
  wake-up could hard reset the phone.

[STATUS.md](STATUS.md) is the step-by-step bring-up log.

## Building the kernel

The scripts expect this layout on the build machine:

| Path | What |
|---|---|
| `~/redwood-6.12-kernel` | The kernel source (`r48`), set up as its README says |
| `~/redwood-6.12` | This repository |
| `~/work` | Scratch space; the 5.4 ROM ramdisks go in `~/work/android` |

Then:

```
bash ~/redwood-6.12/scripts/build-all.sh        # kernel + every module
bash ~/redwood-6.12/scripts/make-prebuilt.sh \
	~/redwood-kernel-612 ~/redwood-kernel-54     # refresh the prebuilt repo
```

A full build takes a while. For a device tree change alone,
`scripts/dtbo-fast.sh` rebuilds just `dtbo.img` in seconds. The other
scripts are described in [scripts/README.md](scripts/README.md).

## What's in here

| Path | What |
|---|---|
| `scripts/` | Build and packing scripts |
| `modules/` | Drivers built outside the kernel tree: touch, speaker amps, haptics, fingerprint, IR, `/dev/ion`, Xiaomi's thermal and touch classes, the 5.4 camera driver, and helpers for testing ([modules/README.md](modules/README.md)) |
| `patches/` | Patches applied to Qualcomm's display, GPU, TrustZone, Wi-Fi and SoC driver sources |
| `bringup/` | Small tools from the early bring-up: a USB shell, a log saver, a `libion` shim and more ([bringup/README.md](bringup/README.md)) |
| `qcom/`, `bindings/`, `Documentation/devicetree` | Qualcomm's device tree repository with redwood, as it was during bring-up. The live copy is now in the kernel repository |
| [Documentation/porting-notes.md](Documentation/porting-notes.md) | Detailed notes from the port: what each part needed and why |

## Testing a kernel on the phone

- Test on slot b and keep slot a on a working system, so there is always
  something to boot and read logs from.
- `/data` is shared between the slots. Back it up first, and never format
  data from recovery while testing.
- `scripts/pack-rom612.sh` takes the boot and vendor_boot of a phone that
  already runs the 6.12 ROM and swaps in a new kernel and modules, so a
  kernel fix can be tried without building a ROM.

## Credits

Qualcomm and CodeLinaro for the kernel platform, the LineageOS redwood/sm8350
kernel for driver and device tree references.

Written and maintained by Suvojeet Sengupta.
