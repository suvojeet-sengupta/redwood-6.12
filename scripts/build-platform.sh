K=~/k612/platform/kernel_platform; V=~/k612/vendor-tree; M=~/k612/mp-lahaina; SOC=$K/soc-repo
export PATH=$K/prebuilts/clang/host/linux-x86/clang-r536225/bin:$K/prebuilts/build-tools/linux-x86/bin:$PATH
export LD_LIBRARY_PATH=$K/prebuilts/kernel-build-tools/linux-x86/lib64
cd $V/wlan/platform
grep -nE "^obj-|CONFIG_" Kbuild | grep obj | head -20
make -C $K/common O=$M M=$PWD ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  CONFIG_ICNSS2=m CONFIG_ICNSS2_QMI=y CONFIG_CNSS_UTILS=m CONFIG_CNSS_GENL=m \
  CONFIG_WCNSS_MEM_PRE_ALLOC=m CONFIG_CNSS_QMI_SVC=m CONFIG_CNSS_OUT_OF_TREE=y \
  KBUILD_EXTRA_SYMBOLS=$V/soc-modules.symvers SOCINCLUDE="-I$SOC/include -I$SOC/include/uapi -I$SOC/arch/arm64/include" \
  KCFLAGS="-I$SOC/include -I$PWD/inc -I$PWD/cnss_utils -I$PWD/cnss2 -I$PWD/icnss2 -I$PWD/cnss_prealloc" -j64 modules > ~/k612/wlanplat.log 2>&1; echo rc=$?
grep -E " error:|ERROR|undefined" ~/k612/wlanplat.log | sort -u | head -15
find . -name "*.ko" | head
