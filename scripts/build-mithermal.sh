#!/bin/bash
# Xiaomi's thermal_message nodes for mi_thermald.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/mithermal; cp -r ~/redwood-6.12/modules/mithermal ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 M=$HOME/work/mithermal modules -j8 > ~/work/mithermal.log 2>&1
echo "errors: $(grep -c -E " error:|ERROR:" ~/work/mithermal.log)"
grep -E " error:|ERROR:|undefined" ~/work/mithermal.log | head -20
ls -la ~/work/mithermal/*.ko 2>/dev/null
