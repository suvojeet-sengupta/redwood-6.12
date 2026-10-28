#!/bin/bash
# The redwood 6.12 kernel as a prebuilt for the ROM's device tree, in the
# layout device/xiaomi/redwood-kernel uses: Image, dtb/, dtbo.img and
# kernel-headers/, plus vendor_ramdisk/ with every module and the order
# first stage init loads them in. Takes the modules from pack-android.sh's
# output, without the test-only ones.
# Usage: make-prebuilt.sh <prebuilt repo dir> <5.4 prebuilt dir for kernel-headers>
set -e
P=${1:?prebuilt repo dir}; H=${2:?5.4 prebuilt dir}
KP=~/redwood-6.12-kernel/kernel_platform
D=$KP/out/msm-kernel-lahaina-perf/dist
OUT=~/work/android/out
TEST="testguard.ko bootlog.ko logcap.ko"

bash ~/redwood-6.12/scripts/pack-android.sh > ~/work/make-prebuilt-pack.log
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
M=$OUT/mods/lib/modules/$REL
[ -f $M/modules.load ] || { echo "no modules.load in $M"; exit 1; }

find $P -mindepth 1 -maxdepth 1 ! -name .git ! -name README.md -exec rm -rf {} +
mkdir -p $P/dtb $P/vendor_ramdisk
cp $D/Image $P/Image
# the name BoardConfig passes to mkbootimg --dtb
cp $D/yupik.dtb "$P/dtb/01_dtbdump_Qualcomm_Technologies,_Inc._Yupik_SoC.dtb"
cp ~/work/dtfast/dtbo.img $P/dtbo.img
# userspace (vendor blobs and the HALs built from source) is the 5.4 one
cp -a $H/kernel-headers $P/kernel-headers

for f in $M/*.ko; do
	case " $TEST " in *" $(basename $f) "*) continue ;; esac
	cp $f $P/vendor_ramdisk/
done
grep -vxF $(for m in $TEST; do echo "-e $m"; done) $M/modules.load > $P/vendor_ramdisk/modules.load
while read m; do [ -f $P/vendor_ramdisk/$m ] || { echo "listed but missing: $m"; exit 1; }; done < $P/vendor_ramdisk/modules.load

{
	echo "release=$REL"
	echo "version=$(strings $D/Image | grep -m1 '^Linux version')"
	echo "modules=$(wc -l < $P/vendor_ramdisk/modules.load)"
} > $P/build-info.txt
cat $P/build-info.txt
du -sh $P/Image $P/vendor_ramdisk $P/dtbo.img
