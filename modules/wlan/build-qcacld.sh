K=~/k612/platform/kernel_platform; V=~/k612/vendor-tree; M=~/k612/mp-lahaina; SOC=$K/soc-repo
export PATH=$K/prebuilts/clang/host/linux-x86/clang-r536225/bin:$K/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$K/prebuilts/kernel-build-tools/linux-x86/lib64
cd $V/wlan/qcacld-3.0
ls -la cmn | head -2
[ -e cmn/qdf ] || { rm -rf cmn; ln -s ../qca-wifi-host-cmn cmn; }
cat $V/soc-modules.symvers $V/wlan/platform/Module.symvers > /tmp/wlan-extra.symvers
make -C $K/common O=$M M=$PWD ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  WLAN_ROOT=$PWD WLAN_PROFILE=lahaina_gki_qca6750 MODNAME=wlan BUILD_DIAG_VERSION=y \
  CONFIG_QCA_CLD_WLAN=m CONFIG_QCA_WIFI_ISOC=0 CONFIG_QCA_WIFI_2_0=1 QCA_WIFI_FTM_NL80211=y \
  KBUILD_EXTRA_SYMBOLS=/tmp/wlan-extra.symvers \
  KCFLAGS="-I$SOC/include -I$V/wlan/platform/inc -I$V/wlan/inc-net -include $HOME/k612/vendor-tree/wlan/qcacld-3.0/configs/kbuild_defines.h -include $HOME/k612/vendor-tree/wlan/qcacld-3.0/configs/config_to_feature.h -include $HOME/k612/vendor-tree/wlan/qcacld-3.0/configs/grep_defines_lahaina.h -Wno-macro-redefined -DWLAN_OPEN_SOURCE -Wno-error=missing-prototypes -Wno-error=unused-variable -Wno-error=unused-but-set-variable" -j96 modules > ~/k612/qcacld.log 2>&1; echo rc=$?
grep -E " error:|ERROR:|undefined" ~/k612/qcacld.log | sort -u | head -20
ls -la wlan.ko 2>&1
