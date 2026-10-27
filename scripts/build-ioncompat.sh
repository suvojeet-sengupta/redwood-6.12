#!/bin/bash
# ioncompat: /dev/ion on the DMA-BUF heaps for the 5.4 vendor.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/ioncompat; cp -r ~/redwood-6.12/modules/ioncompat ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers M=$HOME/work/ioncompat KCFLAGS="-I$SOC/include -I$SOC/include/uapi" modules -j8 > ~/work/ioncompat.log 2>&1
echo "errors: $(grep -c " error:" ~/work/ioncompat.log)"
grep -E " error:| warning:|undefined|WARNING" ~/work/ioncompat.log | sed "s#.*/ioncompat/##" | head -40
ls -la ~/work/ioncompat/*.ko 2>/dev/null
