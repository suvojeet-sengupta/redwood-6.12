# redwood 6.12 status

Where the Qualcomm 6.12 kernel platform port stands on redwood. Tests run on
slot b in the bring-up USB shell ([bringup](bringup/README.md)); slot a keeps
the daily 5.4 system.

Last updated 2026-10-22.

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
| Sound card | lahaina-yupikidp-snd-card registers, MultiMedia1 plays to RX_CDC_DMA_RX_0 |

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

## Audio

- [x] APR, the ADSP loader and the audio notifier find the ADSP through
      `qcom,rproc-handle` (5.4 used the subsystem name).
- [x] The in-kernel pd-mapper serves yupik (sc7280 domains) and binds to
      Qualcomm's `rproc_qcom_common.pd-mapper` aux device, so PDR sees the
      ADSP's `avs/audio` domain come up
      ([patches/soc-repo](patches/soc-repo) 0003, 0004).
- [x] APR adds q6core and the audio ION devices on the first ADSP up
      ([patches/audio-kernel](patches/audio-kernel)).
- [x] SoundWire masters get their bus numbers from the swr0..2 aliases; rx
      and tx both register with both WCD938x slaves.
- [x] WCD938x probes with `qcom,swr-tx-port-params` (lahaina defaults).
- [x] The card registers: AFE PCM RX1 (`msm-dai-q6-dev.226`) added, AUXPCM
      links dropped.
- [x] A tone on MultiMedia1 through the headphone path runs end to end on
      the DSP (AFE port 0xb030 starts). No ACDB calibration in the shell.
- [ ] Hear it on wired headphones (`HPH_L/R Switch` from the 5.4 mixer
      paths don't exist on 6.12; find their names).
- [x] Speaker amps: two AW88261 (chip id 0x2113) on i2c@98c000 (0x34,
      0x35) with the mainline aw88261 driver ([modules/speaker](modules/speaker)),
      on PRI_TDM_RX_0 next to the stub codec
      ([patches/audio-kernel](patches/audio-kernel) 0002), PRI TDM at 2
      slots like 5.4. Both report "start success" on a tone through
      MultiMedia1 (PRI_TDM_RX_0: Two, S32_LE, KHZ_48).
- [ ] Hear the speaker tone, and the earpiece (5.4 switches the 0x35 amp
      with spk-sw-gpio 101).
- [ ] Earpiece and the mics (TX port params untested).
- [ ] `msm-audio-ion` still uses `qcom,iommu-dma-addr-pool` (works,
      deprecated).

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
- [ ] Battery / UCSI: `BATTERY_CHG` prop 27 answers rc=514, UCSI never sees a
      cable partner.
- [ ] Video driver (`sku_version` missing, `media.sh` workaround).
- [ ] Display still uses `qcom,iommu-dma-addr-pool` (works, but deprecated).
