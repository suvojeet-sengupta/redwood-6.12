#!/bin/bash
# Out of tree vendor modules for Android on redwood 6.12: display, GPU,
# TrustZone, touch, subsys_compat, usbrole, speaker amps. Needs stage1.sh
# first (kernel, modules_prepare and the lahaina config in ~/k612/mp-lahaina).
set -e
KP=~/redwood-6.12-kernel/kernel_platform; V=~/redwood-6.12-kernel/vendor; SOC=$KP/soc-repo
M=~/k612/mp-lahaina; W=~/work/vb; SYM=$HOME/k612/vendor-tree/soc-modules.symvers
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
mkdir -p $W
for d in mm-drivers display-drivers graphics-kernel securemsm-kernel; do rsync -a --delete $V/$d/ $W/$d/; done
rm -rf $W/touch $W/subsys_compat $W/usbrole
cp -r ~/redwood-6.12/modules/touch ~/redwood-6.12/modules/subsys_compat ~/redwood-6.12/modules/usbrole $W/
T=$KP/common/drivers/input/touchscreen
cp $T/goodix_berlin_core.c $T/goodix_berlin_spi.c $T/goodix_berlin.h $W/touch/
mkdir -p ~/redwood-6.12-kernel/vendor/qcom/opensource
ln -sfn $W/securemsm-kernel ~/redwood-6.12-kernel/vendor/qcom/opensource/securemsm-kernel
MK="make -C $KP/common O=$M ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$SYM -j64"
FENCE="-I$W/mm-drivers/hw_fence/include -I$W/mm-drivers/sync_fence/include"
$MK M=$W/display-drivers DISPLAY_ROOT=$W/display-drivers CONFIG_ARCH_LAHAINA=y CONFIG_ARCH_YUPIK=y CONFIG_DRM_MSM=m \
	SOCINCLUDE="-I$SOC/include -I$SOC/include/uapi $FENCE -I$W/mm-drivers/msm_ext_display/include" modules > $W/display.log 2>&1
$MK M=$W/graphics-kernel KGSL_PATH=$W/graphics-kernel CONFIG_ARCH_LAHAINA=y CONFIG_ARCH_YUPIK=y \
	KCFLAGS="-I$KP/common/drivers/devfreq" SOCINCLUDE="-I$SOC/include -I$SOC/include/uapi $FENCE" modules > $W/gpu.log 2>&1
$MK M=$W/securemsm-kernel SSG_MODULE_ROOT=$W/securemsm-kernel CONFIG_ARCH_LAHAINA=y CONFIG_DISABLE_TEST_MODULES=y \
	CONFIG_QSEECOM=m SOCINCLUDE="-I$SOC/include -I$SOC/include/uapi" modules > $W/securemsm.log 2>&1
for d in touch subsys_compat usbrole; do $MK M=$W/$d modules > $W/$d.log 2>&1; done
mkdir -p $W/out && rm -f $W/out/*.ko
find $W/display-drivers $W/graphics-kernel $W/securemsm-kernel $W/touch $W/subsys_compat $W/usbrole -name "*.ko" -exec cp {} $W/out/ \;
ls $W/out
echo STAGE2 DONE
