#!/bin/bash
# redwood's 5.4 lahaina camera driver (LineageOS sm8350 techpack/camera, kept
# in redwood-6.12-kernel/vendor/camera54), which the 5.4 CamX userspace and
# ICP firmware speak to, ported by modules/camera54 (compat headers and
# port.py). ~/los54/techpack/camera is the fallback for older kernel trees.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo; C=~/work/camera54; P=~/redwood-6.12/modules/camera54
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
S=~/redwood-6.12-kernel/vendor/camera54; [ -d $S ] || S=~/los54/techpack/camera
rm -rf $C; cp -r $S $C; cp -r $P/compat $C/compat
python3 $P/port.py $C/drivers
[ -f $P/smmu.py ] && python3 $P/smmu.py $C/drivers
sed -e 's#^cameratree := $(srctree)/techpack/camera/drivers#cameratree := $(src)#' \
    -e 's#-I$(srctree)/techpack/camera/include/uapi/camera#-I$(src)/../compat -I$(src)/../include/uapi/camera -I$(src)/../include/uapi -include $(src)/../compat/cam_compat_612.h -include $(src)/../config/yupikcameraconf.h#' \
    -e '/BOARD_VENDOR_KERNEL_MODULES/d' -e '/SPECTRA CAMERA IS BUILT AS DLKM/d' $C/drivers/Makefile > $C/drivers/Kbuild
echo 'CFLAGS_cam_trace.o := -I$(src)/cam_utils' >> $C/drivers/Kbuild
# the 5.4 structs end in one-element arrays used as flexible ones, which
# GKI's UBSAN bounds traps turn into panics (the sensor probe hit one)
echo 'UBSAN_SANITIZE := n' >> $C/drivers/Kbuild
# the HFI packets are memcpy'd into those arrays too, and FORTIFY's
# warnings on each copy stall the display while they print
echo 'ccflags-y += -D__NO_FORTIFY' >> $C/drivers/Kbuild
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 \
	KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers M=$C/drivers \
	CONFIG_SPECTRA_CAMERA=m CONFIG_SPECTRA_ISP=y CONFIG_SPECTRA_SENSOR=y CONFIG_SPECTRA_ICP=y \
	CONFIG_SPECTRA_JPEG=y CONFIG_SPECTRA_LRME=y \
	KCFLAGS="-I$SOC/include -I$SOC/include/uapi -Wno-error" modules -k -j32 > ~/work/camera54.log 2>&1
echo "errors: $(grep -c " error:" ~/work/camera54.log)"
grep -E " error:|undefined!|ERROR:" ~/work/camera54.log | sed "s#.*/camera54/drivers/##" | sort | uniq -c | sort -rn | head -${N:-50}
ls -la $C/drivers/*.ko 2>/dev/null
