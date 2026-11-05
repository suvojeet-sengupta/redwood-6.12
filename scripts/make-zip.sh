#!/bin/bash
# An AnyKernel3 zip from a finished build-all.sh. boot gets the new Image and
# keeps its ramdisk, so KernelSU or Magisk stay. dtbo and vendor_boot are
# written whole; vendor_boot comes from pack-rom612.sh, so the zip fits the ROM
# whose images it was made from.
# Usage: make-zip.sh <ROM boot.img> <ROM vendor_boot.img>
set -e
BOOT=$(realpath "${1:?ROM boot.img}"); VBOOT=$(realpath "${2:?ROM vendor_boot.img}")
D=~/redwood-6.12-kernel/kernel_platform/out/msm-kernel-lahaina-perf/dist
W=~/work; R=$W/android/rom612; Z=$W/zip; AK=$Z/ak3
AK3=${AK3:-https://github.com/suvojeet-sengupta/AnyKernel3}

bash ~/redwood-6.12/scripts/pack-rom612.sh "$BOOT" "$VBOOT"
REL=$(strings $D/Image | sed -n "s/^Linux version \([^ ]*\) .*/\1/p" | head -1)
[ -n "$REL" ] || { echo "no kernel release in $D/Image"; exit 1; }

rm -rf $AK; mkdir -p $Z
git clone -q --depth 1 "$AK3" $AK
rm -rf $AK/.git $AK/.github $AK/README.md $AK/banner
cp $D/Image $AK/Image
cp $R/dtbo.img $AK/dtbo.img
cp $R/vendor_boot.img $AK/vendor_boot.img

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

BLOCK=boot;
IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;
NO_BLOCK_DISPLAY=1;

. tools/ak3-core.sh;

ui_print " " "$REL for redwood" " ";
ui_print "- Writing the kernel to boot";
split_boot;
flash_boot;
ui_print "- Writing dtbo";
flash_generic dtbo;
ui_print "- Writing vendor_boot";
flash_generic vendor_boot;
ui_print " " "Done.";
EOF

OUT=$Z/Suvkernel-${REL#*-Suvkernel-}-redwood-$(date +%Y%m%d-%H%M).zip
(cd $AK && zip -qr9 "$OUT" . -x '*placeholder')
sha256sum "$OUT"
