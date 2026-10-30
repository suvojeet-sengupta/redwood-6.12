#!/bin/bash
# FocalTech FT3680 touch (redwood's other panel). Links against xiaomi_touch,
# so build-xiaomitouch runs first.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/focaltech; cp -r ~/redwood-6.12/modules/focaltech ~/work/
cp ~/redwood-6.12/modules/xiaomitouch/xiaomi_touch.h $SOC/include/linux/soc/qcom/panel_event_notifier.h ~/work/focaltech/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 M=$HOME/work/focaltech \
	KBUILD_EXTRA_SYMBOLS="$HOME/k612/vendor-tree/soc-modules.symvers $HOME/work/xiaomitouch/Module.symvers" \
	modules -j16 > ~/work/focaltech.log 2>&1
echo "errors: $(grep -c -E " error:|ERROR:" ~/work/focaltech.log)"
grep -E " error:|ERROR:|undefined|warning:" ~/work/focaltech.log | head -30
ls -la ~/work/focaltech/*.ko 2>/dev/null
