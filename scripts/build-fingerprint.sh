#!/bin/bash
# The Goodix fingerprint and Xiaomi fingerprint class drivers from the 5.4 tree.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/fingerprint; cp -r ~/redwood-6.12/modules/fingerprint ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers M=$HOME/work/fingerprint KCFLAGS="-I$SOC/include -I$SOC/include/uapi" modules -j8 > ~/work/fingerprint.log 2>&1
echo "errors: $(grep -c " error:" ~/work/fingerprint.log)"
grep -E " error:| warning:|undefined|WARNING" ~/work/fingerprint.log | sed "s#.*/fingerprint/##" | head -40
ls -la ~/work/fingerprint/*.ko 2>/dev/null
