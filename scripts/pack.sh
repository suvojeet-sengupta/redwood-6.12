#!/bin/bash
# Pack the bring-up USB shell boot, vendor_boot and dtbo images for redwood 6.12.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
D=$KP/out/msm-kernel-lahaina-perf/dist
A=$KP/out/target/product/lahaina/dlkm/lib/modules
W=~/work
B=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
OUT=$W/out; R=$OUT/ramdisk; M=$R/lib/modules/$REL
rm -rf $OUT; mkdir -p $M $R/bin $R/etc $OUT/sysdlkm

cp $W/bin/init $R/init
cp $W/bin/toybox $W/bin/rb $R/bin/
for t in $($W/toybox/generated/instlist 2>/dev/null || (cd $W/toybox && ./scripts/install.sh --help >/dev/null 2>&1; cat generated/newtoys.h | sed -n "s/^USE_[A-Z0-9_]*(NEWTOY(\([a-z0-9_.\[-]*\),.*/\1/p")); do
	[ "$t" = toybox ] || ln -sf toybox $R/bin/$t
done
echo "PS1=\"redwood612# \"" > $R/etc/shrc

cp $D/*.ko $M/
tar -xzf $D/system_dlkm_staging_archive.tar.gz -C $OUT/sysdlkm
find $OUT/sysdlkm -name "*.ko" | while read f; do
	[ -e $M/$(basename $f) ] && echo "dup $(basename $f)" || cp $f $M/
done
cp $A/*.ko $M/
rm -f $M/hdmi_dlkm.ko
cp $W/usbrole/usbrole.ko $M/
for f in $M/*.ko; do $B/llvm-strip --strip-debug $f; done

xargs -n1 basename < $D/vendor_boot.modules.load > $M/modules.load
{
	xargs -n1 basename < $D/vendor_dlkm.modules.load
	for m in snd_event q6_notifier apr adsp_loader q6_pdr q6 platform native stub pinctrl_lpi pinctrl_wcd \
		swr swr_ctrl wcd_core wcd9xxx mbhc bolero_cdc va_macro tx_macro rx_macro wsa_macro \
		swr_dmic_legacy swr_haptics wsa883x wcd937x_slave wcd937x wcd938x_slave wcd938x machine; do
		echo ${m}_dlkm.ko
	done
	echo usbrole.ko
} > $M/vendor_dlkm.modules.load
depmod -b $R $REL
for l in modules.load vendor_dlkm.modules.load; do
	while read m; do grep -q "^$m:" $M/modules.dep || echo "missing from modules.dep: $m"; done < $M/$l
done

(cd $R && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 --favor-decSpeed -q > $OUT/ramdisk.lz4
mkdir -p $OUT/vr && (cd $OUT/vr && echo . | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -q > $OUT/vendor_ramdisk.lz4

MK=$KP/tools/mkbootimg/mkbootimg.py
python3 $MK --header_version 3 --kernel $D/Image --ramdisk $OUT/ramdisk.lz4 \
	--os_version 17.0.0 --os_patch_level 2026-08 --output $OUT/boot.img
python3 $MK --header_version 3 --pagesize 0x1000 --base 0 --kernel_offset 0x8000 \
	--ramdisk_offset 0x01000000 --tags_offset 0x100 --dtb_offset 0x01f00000 \
	--vendor_cmdline "androidboot.hardware=qcom androidboot.usbcontroller=a600000.dwc3 cgroup.memory=nokmem,nosocket loop.max_part=7 msm_rtb.filter=0x237 service_locator.enable=1 swiotlb=0 pcie_ports=compat clk_ignore_unused pd_ignore_unused" \
	--dtb $D/yupik.dtb --vendor_ramdisk $OUT/vendor_ramdisk.lz4 --vendor_boot $OUT/vendor_boot.img
python3 $KP/external/libufdt/utils/src/mkdtboimg.py create $OUT/dtbo.img $D/redwood-overlay.dtbo

echo "release $REL, $(ls $M/*.ko | wc -l) modules"
ls -la $OUT/*.img $OUT/*.lz4
sha1sum $OUT/*.img
