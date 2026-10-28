#!/bin/bash
# Real 6.12 kernel images for the installed ROM: its own boot and vendor
# ramdisks, untouched (real fstab and /data, no testguard, no logsaver
# shims), with the 6.12 kernel, every module pack-android.sh collects and
# the ROM's command line plus what 6.12 needs. bootlog stays in, so the
# kernel log of each boot still lands on the logdump partitions.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
D=$KP/out/msm-kernel-lahaina-perf/dist
W=~/work; AN=$W/android; OUT=$AN/out; R=$AN/real
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
M=$OUT/mods/lib/modules/$REL
[ -f $M/modules.load ] || bash ~/redwood-6.12/scripts/pack-android.sh > $W/pack-real-android.log
TEST="testguard.ko logcap.ko"

rm -rf $R; mkdir -p $R
S=$R/mods/lib/modules/$REL; mkdir -p $S
for f in $M/*.ko; do
	case " $TEST " in *" $(basename $f) "*) continue ;; esac
	cp $f $S/
done
grep -vxF $(for m in $TEST; do echo "-e $m"; done) $M/modules.load > $S/modules.load
depmod -b $R/mods $REL 2>/dev/null
while read m; do grep -q "^$m:" $S/modules.dep || { echo "missing from modules.dep: $m"; exit 1; }; done < $S/modules.load

VR=$R/vr
cp -a $AN/v54 $VR
mkdir -p $VR/lib/modules
cp $S/*.ko $S/modules.dep $S/modules.load $S/modules.alias $S/modules.softdep $VR/lib/modules/

(cd $VR && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 --favor-decSpeed -q > $R/vendor_ramdisk.lz4
(cd $AN/b54 && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 --favor-decSpeed -q > $R/ramdisk.lz4
MK=$KP/tools/mkbootimg/mkbootimg.py
python3 $MK --header_version 3 --kernel $D/Image --ramdisk $R/ramdisk.lz4 \
	--os_version 17.0.0 --os_patch_level 2026-08 --output $R/boot.img
python3 $MK --header_version 3 --pagesize 0x1000 --base 0 --kernel_offset 0x8000 \
	--ramdisk_offset 0x01000000 --tags_offset 0x100 --dtb_offset 0x01f00000 \
	--vendor_cmdline "$(cat $AN/vendor_cmdline.txt) clk_ignore_unused pd_ignore_unused" \
	--dtb $D/yupik.dtb --vendor_ramdisk $R/vendor_ramdisk.lz4 --vendor_boot $R/vendor_boot.img
cp $W/dtfast/dtbo.img $R/dtbo.img
echo "release $REL, $(wc -l < $S/modules.load) modules to load, first: $(head -1 $S/modules.load)"
grep -q testguard $VR/lib/modules/modules.load && { echo "testguard in a real image"; exit 1; }
cmp -s $VR/first_stage_ramdisk/fstab.default $AN/v54/first_stage_ramdisk/fstab.default 2>/dev/null || true
ls -la $R/*.img
sha1sum $R/*.img
