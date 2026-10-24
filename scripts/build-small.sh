#!/bin/bash
# The speaker amps (mainline aw88261, which GKI doesn't build), FastRPC and
# the bring-up modules.
set -e
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo; R=~/redwood-6.12/modules
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
MK="make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers -j32"
C=$KP/common/sound/soc/codecs
rm -rf ~/work/speaker; cp -r $R/speaker ~/work/speaker
cp $C/aw88261.c $C/aw88261.h ~/work/speaker/; cp -r $C/aw88395 ~/work/speaker/
sed -e "/EXPORT_SYMBOL/d" -e "/MODULE_/d" $KP/common/lib/crc8.c > ~/work/speaker/crc8.c
for m in speaker bootlog testguard logcap adsprpc; do
	[ $m = speaker ] || { rm -rf ~/work/$m; cp -r $R/$m ~/work/; }
	$MK M=$HOME/work/$m KCFLAGS="-I$SOC/include -I$SOC/include/uapi" modules > ~/work/$m.log 2>&1 || { echo "$m failed"; grep -E " error|ERROR" ~/work/$m.log | head; exit 1; }
	ls ~/work/$m/*.ko
done
