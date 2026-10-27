#!/bin/bash
# The mainline ir-spi driver for the IR blaster; GKI builds rc-core but not it.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/irspi; cp -r ~/redwood-6.12/modules/irspi ~/work/
cp $KP/common/drivers/media/rc/ir-spi.c ~/work/irspi/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 M=$HOME/work/irspi modules -j8 > ~/work/irspi.log 2>&1
echo "errors: $(grep -c -E " error:|ERROR:" ~/work/irspi.log)"
grep -E " error:|ERROR:|undefined" ~/work/irspi.log | head -20
ls -la ~/work/irspi/*.ko 2>/dev/null
