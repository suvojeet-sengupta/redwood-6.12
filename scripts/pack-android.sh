#!/bin/bash
# Android test images for redwood 6.12 on slot b: the ROM's 5.4 ramdisks with
# every 6.12 module, testguard keeping userdata and misc out of reach,
# bootlog keeping the kernel log on logdump, logcap adding Android's logs to
# it and logsaver as /init for the shims.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
D=$KP/out/msm-kernel-lahaina-perf/dist
A=$KP/out/target/product/lahaina/dlkm/lib/modules
W=~/work; AN=$W/android; BU=~/redwood-6.12/bringup
NB=$KP/prebuilts/ndk-r26/toolchains/llvm/prebuilt/linux-x86_64/bin
B=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
[ -n "$REL" ] || { echo "no kernel release"; exit 1; }
[ -d $AN/v54 ] && [ -d $AN/b54 ] || { echo "no 5.4 ramdisks in $AN"; exit 1; }
OUT=$AN/out
rm -rf $OUT
mkdir -p $OUT/mods $OUT/sysdlkm
M=$OUT/mods/lib/modules/$REL
mkdir -p $M

cp $D/*.ko $M/
tar -xzf $D/system_dlkm_staging_archive.tar.gz -C $OUT/sysdlkm
find $OUT/sysdlkm -name "*.ko" | while read f; do [ -e $M/$(basename $f) ] || cp $f $M/; done
cp $A/*.ko $M/
rm -f $M/hdmi_dlkm.ko
# The LLCC ECC interrupt fires nonstop on redwood under qcom_edac (close to
# a million times in 11 s), the CPUs stop petting the watchdog and it bites.
rm -f $M/qcom_edac.ko
cp $W/speaker/*.ko $W/testguard/testguard.ko $W/bootlog/bootlog.ko $W/logcap/logcap.ko $W/adsprpc/frpc-adsprpc.ko $W/video/msm-vidc.ko $W/btpower/btpower.ko $W/camera/camera.ko $W/fingerprint/goodix_fp.ko $W/fingerprint/xiaomi_fp.ko $W/haptics/aw8697-haptic.ko $W/ioncompat/ioncompat.ko $W/irspi/ir-spi.ko $W/mithermal/mi_thermal.ko $W/xiaomitouch/xiaomi_touch.ko $M/
WL=$KP/../vendor/wlan
cp $WL/qcacld-3.0/wlan.ko $(find $WL/platform -name "*.ko") $M/
cp $(ls $KP/bazel-cache/bazel-cache/*/execroot/_main/bazel-out/k8-fastbuild/bin/vendor/qcom/opensource/datarmnet-ext/mem/lahaina_perf_rmnet_mem/staging/lib/modules/*/extra/vendor/qcom/opensource/datarmnet-ext/mem/rmnet_mem.ko) $M/
for f in $W/vb/out/*.ko; do
	case $(basename $f) in
	tz_log_dlkm.ko|subsys_compat.ko) ;;
	*) cp $f $M/ ;;
	esac
done
for f in $M/*.ko; do $B/llvm-strip --strip-debug $f; done

# GKI only lets modules that export protected symbols load when they carry
# the build's signature, and stripping drops it, so put the signed copies of
# the ones Wi-Fi and Bluetooth need back after stripping. First stage init
# gives up on the first module that fails, so they load last.
SIGNED="rfkill.ko libarc4.ko bluetooth.ko btbcm.ko btqca.ko hci_uart.ko pwrseq-core.ko"
for m in $SIGNED; do
	f=$(find $OUT/sysdlkm -name $m | head -1)
	[ -n "$f" ] && cp $f $M/$m
done

# Leave out the rest of what needs a protected module that isn't signed here.
depmod -b $OUT/mods $REL
bad="tipc.ko"
while :; do
	more=$(awk -v bad=" $bad " -F: '{ n = split($2, d, " "); for (i = 1; i <= n; i++) if (index(bad, " " d[i] " ") && !index(bad, " " $1 " ")) { print $1; break } }' $M/modules.dep | sort -u | tr "\n" " ")
	[ -n "$more" ] || break
	bad="$bad $more"
done
echo "left out: $bad"
for m in $bad; do rm -f $M/$m; done

{
	echo testguard.ko
	echo bootlog.ko
	echo logcap.ko
	echo ioncompat.ko
	xargs -n1 basename < $D/vendor_boot.modules.load
	xargs -n1 basename < $D/vendor_dlkm.modules.load
	for m in snd_event q6_notifier apr adsp_loader q6_pdr q6 platform native stub pinctrl_lpi pinctrl_wcd \
		swr swr_ctrl wcd_core wcd9xxx mbhc bolero_cdc va_macro tx_macro rx_macro wsa_macro \
		swr_dmic_legacy swr_haptics wsa883x wcd937x_slave wcd937x wcd938x_slave wcd938x; do
		echo ${m}_dlkm.ko
	done
	for m in snd-soc-aw88395-lib snd-soc-aw88261 machine_dlkm \
		rmnet_mem gsim ipam ipanetm rmnet_ctl rmnet_core \
		qseecom_dlkm smcinvoke_dlkm qcrypto-msm_dlkm qce50_dlkm qcedev-mod_dlkm qrng_dlkm \
		hdcp_qseecom_dlkm smmu_proxy_dlkm msm_drm msm_kgsl goodix_berlin_core goodix_berlin_spi usbrole frpc-adsprpc msm-vidc camera goodix_fp xiaomi_fp aw8697-haptic ir-spi mi_thermal xiaomi_touch \
		rfkill cfg80211 cnss_prealloc cnss_utils wlan_firmware_service cnss_nl icnss2 wlan \
		bluetooth hci_uart btpower; do
		echo $m.ko
	done
} | awk '!seen[$0]++' | while read m; do [ -e $M/$m ] && echo $m; done > $M/modules.load.all
# Wi-Fi and Bluetooth hang off the signed rfkill: load them last, so a
# failure there can't stop anything else. qcacld doesn't use mac80211.
LATE="rfkill.ko cfg80211.ko cnss_prealloc.ko cnss_utils.ko wlan_firmware_service.ko cnss_nl.ko icnss2.ko wlan.ko bluetooth.ko hci_uart.ko btpower.ko"
grep -vxF -e mac80211.ko $(for m in $LATE; do echo "-e $m"; done) $M/modules.load.all > $M/modules.load
for m in $LATE; do [ -e $M/$m ] && echo $m; done >> $M/modules.load
rm $M/modules.load.all
rm -f $M/modules.dep* $M/modules.alias* $M/modules.softdep $M/modules.symbols* $M/modules.devname
depmod -b $OUT/mods $REL
while read m; do grep -q "^$m:" $M/modules.dep || echo "missing from modules.dep: $m"; done < $M/modules.load

VR=$OUT/vr
cp -a $AN/v54 $VR
# First stage init mounts /metadata (shared with slot a) from the vendor
# ramdisk's fstab, so it must not be formattable there either.
find $VR -name fstab.default | while read f; do cp $BU/fstab.test $f; done
mkdir -p $VR/lib/modules $VR/lib/firmware
cp $M/*.ko $M/modules.dep $M/modules.load $M/modules.alias $M/modules.softdep $VR/lib/modules/
cp $W/aw88261_acf.bin $VR/lib/firmware/

BR=$OUT/br
cp -a $AN/b54 $BR
mv $BR/init $BR/init.android
$NB/aarch64-linux-android34-clang -static -O2 -o $BR/init $BU/logsaver.c
$NB/aarch64-linux-android30-clang -shared -fPIC -O2 -Wl,-soname,libion.so -o $BR/ionshim64.so $BU/ionshim.c
$NB/armv7a-linux-androideabi30-clang -shared -fPIC -O2 -Wl,-soname,libion.so -o $BR/ionshim32.so $BU/ionshim.c
$NB/llvm-strip $BR/init $BR/ionshim64.so $BR/ionshim32.so
cp $BU/fstab.test $BU/media.sh $BR/
chmod 755 $BR/init $BR/media.sh

(cd $VR && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 --favor-decSpeed -q > $OUT/vendor_ramdisk.lz4
(cd $BR && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 --favor-decSpeed -q > $OUT/ramdisk.lz4
MK=$KP/tools/mkbootimg/mkbootimg.py
python3 $MK --header_version 3 --kernel $D/Image --ramdisk $OUT/ramdisk.lz4 \
	--os_version 17.0.0 --os_patch_level 2026-08 --output $OUT/boot.img
python3 $MK --header_version 3 --pagesize 0x1000 --base 0 --kernel_offset 0x8000 \
	--ramdisk_offset 0x01000000 --tags_offset 0x100 --dtb_offset 0x01f00000 \
	--vendor_cmdline "$(sed s/init_fatal_reboot_target=recovery/init_fatal_reboot_target=bootloader/ $AN/vendor_cmdline.txt) clk_ignore_unused pd_ignore_unused printk.devkmsg=on" \
	--dtb $D/yupik.dtb --vendor_ramdisk $OUT/vendor_ramdisk.lz4 --vendor_boot $OUT/vendor_boot.img
cp $W/dtfast/dtbo.img $OUT/dtbo.img
echo "release $REL, $(wc -l < $M/modules.load) modules to load"
head -2 $M/modules.load | tr "\n" " " | grep -q "^testguard.ko bootlog.ko " || { echo "testguard must load first"; exit 1; }
ls -la $OUT/*.img
sha1sum $OUT/*.img
