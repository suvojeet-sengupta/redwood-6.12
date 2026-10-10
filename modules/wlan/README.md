# Wi-Fi (WCN6750 on the WPSS)

From `LA.VENDOR.16.2.8.r1-06100-LAHAINA.0`: `wlan/platform`, `wlan/qcacld-3.0`,
`wlan/qca-wifi-host-cmn` (linked as `qcacld-3.0/cmn`) and `wlan/fw-api`, side
by side under `vendor-tree/wlan`.

- Platform (`icnss2`, `cnss_utils`, `wlan_firmware_service`, `cnss_nl`,
  `cnss_prealloc`): apply [../../patches/wlan-platform](../../patches/wlan-platform)
  and run `build-platform.sh`.
- qcacld (`wlan.ko`, profile `lahaina_gki_qca6750`): `build-qcacld.sh`. Kleaf
  passes feature flags the plain Kbuild build does not get, so the script
  adds `configs/config_to_feature.h` and `grep_defines_lahaina.h` (the
  cfg80211 features Kleaf greps out of the kernel headers, plus
  `CFG80211_SINGLE_NETDEV_MULTI_LINK_SUPPORT`, which 6.12 needs since
  `wdev->chandef` moved into `wdev->links[]`), `WLAN_OPEN_SOURCE` and
  `QCA_WIFI_FTM_NL80211=y`. It needs a `net/` and `soc/qcom/` view of the
  platform headers (`inc-net`).
- Device tree: `qcom/redwood-wlan.dtsi`, the 5.4 `qcom,wcn6750` node plus
  `qcom,rproc-handle` for the WPSS.
