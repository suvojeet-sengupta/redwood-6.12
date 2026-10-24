#!/bin/bash
# The tree out-of-tree modules build against: GKI's modules_prepare output
# with the lahaina perf config, and the symbols the soc-repo modules export.
# Needs a finished build_with_bazel.py run.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
E=$(ls -d $KP/bazel-cache/bazel-cache/*/execroot/_main/bazel-out/k8-fastbuild/bin)
D=$KP/out/msm-kernel-lahaina-perf/dist
mkdir -p ~/k612
ln -sfn ~/redwood-6.12-kernel ~/k612/platform
ln -sfn ~/redwood-6.12-kernel/vendor ~/k612/vendor-tree
M=~/k612/mp-lahaina
rm -rf $M; mkdir -p $M
tar -xzf $E/common/kernel_aarch64_modules_prepare/modules_prepare_outdir.tar.gz -C $M
C=$E/soc-repo/lahaina_perf_config/out_dir
cp $C/.config $M/.config
rm -rf $M/include/config; cp -r $C/include/config $M/include/config
cp $C/include/generated/autoconf.h $M/include/generated/autoconf.h
cp $E/common/kernel_aarch64/Module.symvers $M/Module.symvers
cat $D/*_Module.symvers | sort -u > ~/k612/vendor-tree/soc-modules.symvers
echo "$(grep -c . $M/Module.symvers) GKI and $(grep -c . ~/k612/vendor-tree/soc-modules.symvers) soc-repo symbols"
