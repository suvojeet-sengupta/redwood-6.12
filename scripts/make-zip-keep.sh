#!/bin/bash
# An AnyKernel3 zip that keeps the phone's ramdisks: boot gets the new kernel
# and keeps its ramdisk (KernelSU or Magisk stay), vendor_boot keeps its
# ramdisk, dtb and command line with only lib/modules swapped for this build's.
# dtbo isn't touched, so the zip fits any redwood 6.12 ROM (AOSPA, PenguinOS).
# Usage: make-zip-keep.sh <ROM boot.img> <ROM vendor_boot.img>
#   (the ROM images only give the module set its layout, nothing of them is
#   flashed)
set -e
BOOT=$(realpath "${1:?ROM boot.img}"); VBOOT=$(realpath "${2:?ROM vendor_boot.img}")
D=~/redwood-6.12-kernel/kernel_platform/out/msm-kernel-lahaina-perf/dist
W=~/work; R=$W/android/rom612; Z=$W/zip; AK=$Z/ak3-keep
AK3=${AK3:-https://github.com/suvojeet-sengupta/AnyKernel3}

bash ~/redwood-6.12/scripts/pack-rom612.sh "$BOOT" "$VBOOT" > $W/make-zip-keep.log
grep "^modules:" $W/make-zip-keep.log
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
[ -n "$REL" ] || { echo "no kernel release in $D/Image"; exit 1; }
MD=$R/vr/lib/modules
[ -f $MD/modules.load ] && [ -f $MD/modules.dep ] || { echo "no module set in $MD"; exit 1; }

rm -rf $AK; mkdir -p $Z
git clone -q --depth 1 "$AK3" $AK
rm -rf $AK/.git $AK/.github $AK/README.md $AK/banner
cp $D/Image $AK/Image
mkdir $AK/vmods
cp $MD/*.ko $MD/modules.load $MD/modules.dep $MD/modules.alias $MD/modules.softdep $AK/vmods/

cat > $AK/anykernel.sh <<EOF
### AnyKernel3 Ramdisk Mod Script
properties() { '
kernel.string=$REL for redwood
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=redwood
device.name2=redwoodin
supported.versions=
supported.patchlevels=
'; }

IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;
NO_BLOCK_DISPLAY=1;

ui_print " " "$REL for redwood" "boot and vendor_boot ramdisks are kept" " ";

# vendor_boot first: if it aborts, nothing is written yet. The new kernel
# with the old GKI modules (signed per build) would not boot.
BLOCK=vendor_boot;
. tools/ak3-core.sh;
mv -f \$AKHOME/Image \$AKHOME/Image.kernel;
ui_print "- Swapping the modules in vendor_boot";
dump_boot;
[ -f \$RAMDISK/lib/modules/modules.load ] || abort "No lib/modules in the vendor_boot ramdisk. Aborting...";
ui_print "  \$(ls \$RAMDISK/lib/modules/*.ko | wc -l) modules in the ROM, \$(ls \$AKHOME/vmods/*.ko | wc -l) in this kernel";
rm -f \$RAMDISK/lib/modules/*.ko \$RAMDISK/lib/modules/modules.*;
cp -f \$AKHOME/vmods/* \$RAMDISK/lib/modules/;
chmod 644 \$RAMDISK/lib/modules/*;
write_boot;

ui_print "- Writing the kernel to boot";
BLOCK=boot;
reset_ak;
mv -f \$AKHOME/Image.kernel \$AKHOME/Image;
split_boot;
flash_boot;
ui_print " " "Done.";
EOF

OUT=$Z/Suvkernel-${REL#*-Suvkernel-}-redwood-$(date +%Y%m%d-%H%M)-keep-ramdisk.zip
(cd $AK && zip -qr9 "$OUT" . -x '*placeholder')
sha256sum "$OUT"
