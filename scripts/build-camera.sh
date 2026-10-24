#!/bin/bash
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo; C=~/work/camera
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf $C; cp -r ~/redwood-6.12-kernel/vendor/camera-kernel $C
cp $C/Kbuild.redwood $C/Kbuild
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers M=$C CAMERA_KERNEL_ROOT=$C KERNEL_ROOT=$KP/common KBUILD_EXTRA_CONFIGS=$C/config/qcm6490-camera.mk SOC_INCLUDE=$SOC/include KCFLAGS="-I$SOC/include -I$SOC/include/uapi" modules -k -j32 > ~/work/camera.log 2>&1
echo "errors: $(grep -c " error:" ~/work/camera.log)"
grep -E " error:|undefined!|ERROR:" ~/work/camera.log | sed "s#.*/camera/##" | sort | uniq -c | sort -rn | head -${N:-40}
ls -la $C/*.ko 2>/dev/null
