# redwood 6.12 status

Where the Qualcomm 6.12 kernel platform port stands on redwood. Tests run on
slot a in the bring-up USB shell ([bringup](bringup/README.md)); slot b keeps
the daily system.

Last updated 2026-10-12.

## Works

| | |
|---|---|
| Kernel + 91 first stage modules | boot, UFS works |
| USB | gadget (ACM + NCM) with the role forced to device |
| Display | 1080x2400 at 120 Hz with display-drivers |
| GPU | Adreno 642L with kgsl |
| Touch | Goodix Berlin-D with mainline goodix_berlin |
| Android 17 | reached the setup wizard (with the workarounds in the README) |
| ADSP, CDSP | boot |
| WPSS | boots, icnss2 QMI connects, firmware reports ready |

## Wi-Fi (WCN6750)

- [x] icnss2 probes. 6.12's qcom IOMMU code refuses
      `qcom,iommu-dma-addr-pool`, so the device never got an IOMMU domain and
      icnss2 deferred forever. `qcom/redwood-wlan.dtsi` now describes the
      0xa0000000 + 256M window with `iommu-addresses`.
- [x] WPSS boots from `modem_a` (`qca6750/wpss.*` linked under
      `/lib/firmware`) and the firmware reports ready.
- [x] No BUG() when the modem's DMS has no MAC
      ([patches/wlan-platform/0002](patches/wlan-platform)).
- [ ] `wlan.ko` still resets the phone about a second after the firmware is
      ready, with nothing in the kernel log (looks like a secure-side
      SMMU/XPU fault). Next: read `minidump` after the reset.
- [ ] `wlan0` up and scanning.
- [ ] Add the `soc-wake` and `ep-powersave` smp2p states (icnss2 asks for
      them; 5.4 never had them).

## Modem

- [ ] MSS starts but trips "DOG detects stalled initialization": the modem
      needs `qrtr-ns`, `pd-mapper`, `tftp_server` and `rmt_storage`. Plan: run
      just those vendor daemons in the bring-up shell, after backing up
      `modemst1`, `modemst2`, `fsg` and `fsc`, with the firmware from the
      slot the daily system uses.
- [ ] DMS up, which also gives Wi-Fi its real MAC.

## Other gaps

- [ ] Boot image: `qcom_ice.ko` built with the HWKM patch needs `hwkm.ko` and a
      matching `modules.dep` in the ramdisk, or boot stalls before USB.
- [ ] Audio: all modules load, no sound card yet.
- [ ] Battery / UCSI: `BATTERY_CHG` prop 27 answers rc=514, UCSI never sees a
      cable partner.
- [ ] Video driver (`sku_version` missing, `media.sh` workaround).
- [ ] Display still uses `qcom,iommu-dma-addr-pool` (works, but deprecated).
