#!/bin/bash
# Fills ~/work for build-all.sh and the packing scripts: the script copies
# build-all.sh runs, and the ROM's ramdisks and command line.
# Usage: setup-work.sh <ROM boot.img> <ROM vendor_boot.img>
set -e
BOOT=$(realpath "${1:?ROM boot.img}"); VBOOT=$(realpath "${2:?ROM vendor_boot.img}")
KP=~/redwood-6.12-kernel/kernel_platform
UNPACK=$KP/tools/mkbootimg/unpack_bootimg.py
W=~/work; AN=$W/android
[ -f "$UNPACK" ] || { echo "no $UNPACK: do the repo sync step first"; exit 1; }

mkdir -p $AN
cp ~/redwood-6.12/scripts/*.sh $W/

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 $UNPACK --boot_img "$BOOT" --out $T/b > /dev/null
python3 $UNPACK --boot_img "$VBOOT" --out $T/vb > $T/vb.txt
sed -n 's/^vendor command line args: //p' $T/vb.txt > $AN/vendor_cmdline.txt
[ -s $AN/vendor_cmdline.txt ] || { echo "no command line in $VBOOT"; exit 1; }

rm -rf $AN/b54 $AN/v54; mkdir -p $AN/b54 $AN/v54
(cd $AN/b54 && lz4 -dc $T/b/ramdisk | cpio -id --quiet)
VR=$T/vb/vendor_ramdisk00; [ -f $VR ] || VR=$T/vb/vendor_ramdisk
(cd $AN/v54 && lz4 -dc $VR | cpio -id --quiet)

# pack-android.sh copies this into its test images. The ROM keeps its own in
# pack-rom612.sh, so an empty one only leaves those test images without speakers.
if [ ! -f $W/aw88261_acf.bin ]; then
	F=$(find $AN/v54 -name aw88261_acf.bin -o -name aw882xx_acf.bin | head -1)
	if [ -n "$F" ]; then
		cp "$F" $W/aw88261_acf.bin
	else
		: > $W/aw88261_acf.bin
		echo "no aw88261_acf.bin in the ROM's vendor ramdisk, using an empty one"
	fi
fi
echo "~/work is ready"
