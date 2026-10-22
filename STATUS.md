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
| Modem | boots, both SIMs ready, registers on LTE |
| Battery | charging and fuel gauge readings |
| Sound card | lahaina-yupikidp-snd-card registers |
| Speaker | both AW88261 amps play (tone from the bring-up shell is loud and clear) |

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
      MultiMedia1 (PRI_TDM_RX_0: Two, S32_LE, KHZ_48), and the tone is
      heard on the speakers.
- [ ] Earpiece (5.4 switches the 0x35 amp with spk-sw-gpio 101).
- [ ] Earpiece and the mics (TX port params untested).
- [ ] `msm-audio-ion` still uses `qcom,iommu-dma-addr-pool` (works,
      deprecated).

## Modem

- [x] MSS boots and stays up: in-kernel pd-mapper and qrtr-ns, linux-msm
      rmtfs serving EFS read-only from RAM copies of modemst1, modemst2,
      fsg and fsc, and tqftpserv ([bringup](bringup/README.md)). The modem
      registers its QMI services (NAS, WDS, UIM, DMS, voice, SMS, IPA, ...).
- [x] The modem's mcfg configs load through tqftpserv (path fix in
      [patches/tqftpserv](bringup/patches/tqftpserv)); its readwrite files
      (mcfg.tmp, server_check.txt) go to tmpfs.
- [x] Both SIMs detected (USIM ready, slot 2 also ISIM) with
      [bringup/qmitool.c](bringup/qmitool.c).
- [x] IPA: dataipa (gsim, ipam, ipanetm, rmnet_mem) built for lahaina,
      `yupik_ipa_fws` loads once the DT gives IPA its context bank count and
      IOVA windows. The modem's IPA handshake completes.
- [x] Online and registered on LTE (405-52 Airtel) with
      `qmitool online`. Without IPA the modem crashed in enl2_dl when it
      went online, and with remoteproc recovery disabled that took the
      phone down: set `recovery` to `enabled` before starting MSS.
- [x] Mobile data call: [bringup/datacall.c](bringup/datacall.c) (DPM
      open port, WDA, WDS bind/start) gets an address on `rmnet_data0`, and
      ping over LTE works once WAN RX uses plain skbs.
- [ ] TCP over mobile data: downlink TCP packets come back from IPA with a
      deaggregation exception (destination pipe 0) and get dropped, so
      connections open but no data arrives. ICMP is fine. Probably the
      DL QMAP/checksum format the modem and IPA agree on.
- [ ] DNS in the shell (static bionic resolves through netd).
- [ ] DMS MAC for Wi-Fi (DMS is up now; icnss2 still has to ask it).

## Other gaps

- [ ] Boot image: `qcom_ice.ko` built with the HWKM patch needs `hwkm.ko` and a
      matching `modules.dep` in the ramdisk, or boot stalls before USB.
- [x] Battery: status, capacity, voltage, current, temperature, cycle count,
      capacities and model read right with Xiaomi's property layout
      ([patches/soc-repo](patches/soc-repo) 0005, `qcom,xiaomi-battery-props`),
      charging works, thermal levels from 10A down.
- [ ] UCSI never sees a cable partner (USB role still forced).
- [ ] Video driver (`sku_version` missing, `media.sh` workaround).
- [ ] Display still uses `qcom,iommu-dma-addr-pool` (works, but deprecated).
