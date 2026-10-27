# redwood 6.12 status

Where the Qualcomm 6.12 kernel platform port stands on redwood. Android test
boots (`scripts/pack-android.sh`) run the 5.4 ROM's userspace on 6.12 with
`/data` on tmpfs, so the real userdata is never touched; the earlier work was
done in the bring-up USB shell ([bringup](bringup/README.md)). The kernel log
of each test boot lands on the `logdump` partitions (`modules/bootlog`, two
newest boots kept by `seq`).

Last updated 2026-10-28.

## Works

| | |
|---|---|
| Kernel + 91 first stage modules | boot, UFS works |
| USB | gadget (ACM + NCM) with the role forced to device |
| Display | 1080x2400 at 120 Hz with display-drivers |
| GPU | Adreno 642L with kgsl |
| Touch | Goodix Berlin-D with mainline goodix_berlin |
| Android 17 | boots to the UI with adb, apps run (`/data` on tmpfs) |
| ADSP, CDSP | boot |
| Wi-Fi | WPSS boots from `wpss.mdt`, `wlan.ko` loads, scanning works |
| Modem | both SIMs in service on LTE, qcrild up (signed TIPC) |
| Mobile data | validated internet on `rmnet_data3` (datarmnet built) |
| Camera | ICP firmware boots, all four sensors probe, preview frames arrive |
| /dev/ion | `modules/ioncompat` on top of the DMA-BUF heaps (fingerprint TA, rpcmem) |
| Battery | charging and fuel gauge readings |
| Sound card | lahaina-yupikidp-snd-card registers |
| Speaker | both AW88261 amps play (tone from the bring-up shell is loud and clear) |

## Android test boots

- [x] Shims (`fstab.test`, `build.prop` with adb, `init.usb.configfs.rc`
      starting adbd) live on `/metadata/redwood612` and get their SELinux
      labels as xattrs before the policy loads; tmpfs labels set after
      that never stuck and the boot HAL couldn't read the fstab.
- [x] `CONFIG_TMPFS_POSIX_ACL`: vold's default ACL on `/data/media/0`
      failed on tmpfs, user 0 got destroyed and apps crash-looped.
- [x] `modules/subsys_compat` puts subsystem devices at the 5.4 PIL sysfs
      paths, so `vendor_sysfs_ssr` applies and libmdmdetect finds the modem.
- [x] No `qcom_edac`: its LLCC ECC IRQ storm got the phone bitten by the
      hypervisor watchdog.
- [ ] Slot b sometimes needs a second boot.

## Camera

- [x] redwood's 5.4 camera driver ported as `modules/camera54` (built from
      the LineageOS sm8350 techpack with `port.py` and `cam_compat_612.h`),
      replacing the 6.12 camera-kernel. No UBSAN for the module (one-element
      trailing arrays); `iommu_map_sg` gets `orig_nents` so the ICP secheap
      maps whole.
- [x] LRME back in the device tree: without it CamX found no LRME MMU
      handle and every ZSL preview pipeline failed with `ENoMemory`.
- [x] MIUI camera opens, the pipelines build and the first preview frame
      arrives.
- [ ] The provider then crashes: Mi's scene detector asks SNPE for the CDSP
      runtime, the DSP rejects the `ADSP_MMAP_ADD_PAGES` heap grow (no
      kernel error, so the refusal is DSP side), SNPE throws and dies in
      the unwind. Same remote heap failure as `audioadsprpcd`.
- [ ] FORTIFY memcpy warnings on the HFI trailing arrays
      (`cam_icp_process_stream_settings`, `cam_icp_mgr_destroy_handle`);
      harmless, but printing them stalls the display.

## Not working yet on Android

- [ ] Bluetooth: stuck at `BLE_TURNING_ON`; the HAL goes for
      `/bt_firmware/image/msbtfw11.mbn` after `PatchVerReq rsp=1`. btpower
      and the DT match LineageOS; compare with the 5.4 HAL init next.
- [ ] Untested on Android: fingerprint (`goodix_fp`, `xiaomi_fp`), vibrator
      (aw8697), IR (`ir-spi`), mic, earpiece, `mi_thermal`, `xiaomi_touch`.
- [ ] Battery: Xiaomi's `qcom-battery` sysfs nodes (`real_type`,
      `authentic`, ...).

## Wi-Fi (WCN6750)

- [x] icnss2 probes. 6.12's qcom IOMMU code refuses
      `qcom,iommu-dma-addr-pool`, so the device never got an IOMMU domain and
      icnss2 deferred forever. `qcom/redwood-wlan.dtsi` now describes the
      0xa0000000 + 256M window with `iommu-addresses`.
- [x] WPSS boots from `modem_a` (`qca6750/wpss.*` linked under
      `/lib/firmware`) and the firmware reports ready.
- [x] No BUG() when the modem's DMS has no MAC
      ([patches/wlan-platform/0002](patches/wlan-platform)).
- [x] icnss2 gets the soc wake states and the right IOVA window; qcacld
      builds with its real feature flags and the 5.4 ini path.
- [x] In Android the vendor keeps the image at `wpss.mdt` (no `qca6750/`),
      set with `firmware-name`. `wlan0` comes up and scans.
- [ ] Connect and pass traffic (not tried yet).

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
- [x] In Android: qcrild needs TIPC (the signed GKI modules), and data
      calls need `rmnet_core`/`rmnet_ctl` from datarmnet
      (`OEM_DCFAILCAUSE_4` without them). Data validates on `rmnet_data3`.
- [ ] DMS MAC for Wi-Fi (DMS is up now; icnss2 still has to ask it).

## Other gaps

- [ ] Boot image: `qcom_ice.ko` built with the HWKM patch needs `hwkm.ko` and a
      matching `modules.dep` in the ramdisk, or boot stalls before USB.
- [x] Battery: status, capacity, voltage, current, temperature, cycle count,
      capacities and model read right with Xiaomi's property layout
      ([patches/soc-repo](patches/soc-repo) 0005, `qcom,xiaomi-battery-props`),
      charging works, thermal levels from 10A down.
- [ ] UCSI never sees a cable partner (USB role still forced).
- [x] Video driver: the lahaina msm_vidc ported as `modules/video`. venus
  boots from `/vendor/firmware/vpu20_1v` into the 5.4 carveout at
  0x8b200000, `sku_version` reads 1, `/dev/video32` decodes MPEG-2, H.264,
  HEVC and VP9 to NV12/UBWC, `/dev/video33` encodes H.264 and HEVC, and an
  H.264 1080p session gets its buffers (`bringup/vidctest.c`). Power
  collapse works. GKI's UBSAN bounds traps are off for the module: the HFI
  structs end in one-element arrays used as flexible ones.
- [ ] Decode a real stream (through Codec2 once Android boots).
- [x] FastRPC: redwood's 5.4 adsprpc ported as `modules/adsprpc`; the ADSP
  channel opens and the audio and sensor PDs come up. CDSP user PDs load
  skels (mialgo).
- [ ] FastRPC: the DSPs refuse `ADD_PAGES`/remote heap maps (camera SNPE,
  `audioadsprpcd`).
- [ ] FastRPC: 32-bit ioctls, ADSP recovery instead of a panic.
- [ ] Display still uses `qcom,iommu-dma-addr-pool` (works, but deprecated).
