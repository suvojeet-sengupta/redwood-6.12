#!/bin/bash
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/btpower; cp -r ~/redwood-6.12/modules/btpower ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers M=$HOME/work/btpower KCFLAGS="-I$SOC/include -I$SOC/include/uapi" modules -k -j8 > ~/work/btpower.log 2>&1
echo "errors: $(grep -c " error:" ~/work/btpower.log)"
grep -E " error:|undefined" ~/work/btpower.log | sed "s#.*/btpower/##" | head -40
ls -la ~/work/btpower/*.ko 2>/dev/null
