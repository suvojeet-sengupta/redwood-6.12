#!/bin/bash
# NXP's SN1xx NFC driver from vendor/nfc-driver, as nxp-nci.ko. It needs the
# pin controller's MPM wakeup helper, a soc-repo module.
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/nfc; cp -r ~/redwood-6.12-kernel/vendor/nfc-driver ~/work/nfc
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 M=$HOME/work/nfc CONFIG_NXP_NFC_I2C=m KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers KCFLAGS="-I$SOC/include" modules -j8 > ~/work/nfc.log 2>&1
echo "errors: $(grep -c -E " error:|ERROR:" ~/work/nfc.log)"
grep -E " error:|ERROR:|undefined" ~/work/nfc.log | head -20
ls -la ~/work/nfc/*.ko 2>/dev/null
