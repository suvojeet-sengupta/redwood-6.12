#!/bin/bash
# Xiaomi's touch class (/sys/class/touch/touch_dev) from the LineageOS sm8350 tree.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/xiaomitouch; cp -r ~/redwood-6.12/modules/xiaomitouch ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 M=$HOME/work/xiaomitouch modules -j8 > ~/work/xiaomitouch.log 2>&1
echo "errors: $(grep -c -E " error:|ERROR:" ~/work/xiaomitouch.log)"
grep -E " error:|ERROR:|undefined" ~/work/xiaomitouch.log | head -20
ls -la ~/work/xiaomitouch/*.ko 2>/dev/null
