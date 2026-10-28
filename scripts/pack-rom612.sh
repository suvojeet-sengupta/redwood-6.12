#!/bin/bash
# New kernel images for a phone already running the 6.12 ROM: take that ROM's
# own boot and vendor_boot, swap in the current kernel and every module (the
# signed GKI ones must come from the same kernel build), and keep its
# ramdisks, fstab and command line as they are.
# Usage: pack-rom612.sh <ROM boot.img> <ROM vendor_boot.img>
set -e
BOOT=${1:?ROM boot.img}; VBOOT=${2:?ROM vendor_boot.img}
KP=~/redwood-6.12-kernel/kernel_platform
D=$KP/out/msm-kernel-lahaina-perf/dist
W=~/work; AN=$W/android; OUT=$AN/out; R=$AN/rom612
UNPACK=$KP/tools/mkbootimg/unpack_bootimg.py; MK=$KP/tools/mkbootimg/mkbootimg.py
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
M=$OUT/mods/lib/modules/$REL
bash ~/redwood-6.12/scripts/pack-android.sh > $W/pack-rom612-android.log
# DROP: more modules to leave out, e.g. DROP=qcom_pd_mapper.ko
TEST="testguard.ko bootlog.ko logcap.ko $DROP"

rm -rf $R; mkdir -p $R
python3 $UNPACK --boot_img $BOOT --out $R/b >/dev/null
python3 $UNPACK --boot_img $VBOOT --out $R/vb >/dev/null
VARGS=$(python3 $UNPACK --boot_img $VBOOT --out $R/vb --format=mkbootimg)

# vendor ramdisk: the ROM's, with this build's modules in place of its own
mkdir -p $R/vr
(cd $R/vr && lz4 -dc $R/vb/vendor_ramdisk00 2>/dev/null | cpio -id --quiet) ||
	(cd $R/vr && lz4 -dc $R/vb/vendor_ramdisk | cpio -id --quiet)
MD=$R/vr/lib/modules
[ -f $MD/modules.load ] || { echo "no lib/modules/modules.load in the ROM vendor ramdisk"; exit 1; }
old=$(ls $MD/*.ko | wc -l)
rm -f $MD/*.ko $MD/modules.*
for f in $M/*.ko; do
	case " $TEST " in *" $(basename $f) "*) continue ;; esac
	cp $f $MD/
done
grep -vxF $(for m in $TEST; do echo "-e $m"; done) $M/modules.load > $MD/modules.load
S=$R/dep/lib/modules/$REL; mkdir -p $S; cp $MD/*.ko $MD/modules.load $S/
depmod -b $R/dep $REL 2>/dev/null
cp $S/modules.dep $S/modules.alias $S/modules.softdep $MD/
while read m; do grep -q "^$m:" $MD/modules.dep || { echo "missing from modules.dep: $m"; exit 1; }; done < $MD/modules.load
echo "modules: ROM had $old, now $(ls $MD/*.ko | wc -l), load $(wc -l < $MD/modules.load)"
(cd $R/vr && find . | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) | lz4 -l -12 --favor-decSpeed -q > $R/vendor_ramdisk.lz4

# vendor_boot with the ROM's own arguments, new vendor ramdisk; boot with the new kernel
eval "set -- $VARGS"
args=(); while [ $# -gt 0 ]; do
	case "$1" in
	--vendor_ramdisk) args+=("$1" "$R/vendor_ramdisk.lz4"); shift 2 ;;
	--vendor_boot) shift 2 ;;
	*) args+=("$1"); shift ;;
	esac
done
python3 $MK "${args[@]}" --vendor_boot $R/vendor_boot.img
BARGS=$(python3 $UNPACK --boot_img $BOOT --out $R/b --format=mkbootimg)
eval "set -- $BARGS"
args=(); while [ $# -gt 0 ]; do
	case "$1" in
	--kernel) args+=("$1" "$D/Image"); shift 2 ;;
	--output|-o) shift 2 ;;
	*) args+=("$1"); shift ;;
	esac
done
python3 $MK "${args[@]}" --output $R/boot.img
cp $W/dtfast/dtbo.img $R/dtbo.img
strings $R/b/kernel | grep -m1 "^Linux version" | cut -c1-60
strings $D/Image | grep -m1 "^Linux version" | cut -c1-60
ls -la $R/*.img
sha1sum $R/*.img
