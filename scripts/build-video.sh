#!/bin/bash
KP=~/redwood-6.12-kernel/kernel_platform; SOC=$KP/soc-repo
export PATH=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin:$KP/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$KP/prebuilts/kernel-build-tools/linux-x86/lib64
rm -rf ~/work/video; cp -r ~/redwood-6.12/modules/video ~/work/
make -C $KP/common O=$HOME/k612/mp-lahaina ARCH=arm64 LLVM=1 LLVM_IAS=1 KBUILD_EXTRA_SYMBOLS=$HOME/k612/vendor-tree/soc-modules.symvers M=$HOME/work/video KCFLAGS="-I$SOC/include -I$SOC/include/uapi -ferror-limit=0 $EXTRA" modules -k -j32 > ~/work/video.log 2>&1
echo "errors: $(grep -c " error:" ~/work/video.log)"
grep " error:" ~/work/video.log | sed "s/.*vidc\///" | sed "s/^[a-z0-9_.]*:[0-9]*:[0-9]*: //" | sort | uniq -c | sort -rn | head -${N:-60}
ls -la ~/work/video/*.ko 2>/dev/null
