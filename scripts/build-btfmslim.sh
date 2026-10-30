#!/bin/bash
# The Bluetooth SLIMbus audio codec (bt_fm_slim), needs btpower's symbols.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/btfmslim; cp -r ~/redwood-6.12/modules/btfmslim ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS="$HOME/k612/vendor-tree/soc-modules.symvers $HOME/work/btpower/Module.symvers" M=$HOME/work/btfmslim KCFLAGS="-I$SOC/include -I$SOC/include/uapi" modules -k -j8 > ~/work/btfmslim.log 2>&1
echo "errors: $(grep -c " error:" ~/work/btfmslim.log)"
grep -E " error:|warning:|undefined" ~/work/btfmslim.log | sed "s#.*/btfmslim/##" | head -40
ls -la ~/work/btfmslim/*.ko 2>/dev/null
