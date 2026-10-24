#!/bin/bash
# Kernel, first stage modules, DTBs and the audio modules for redwood 6.12.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
cd $KP
mkdir -p vendor/qcom/opensource
ln -sfn ../../../../vendor/audio-kernel vendor/qcom/opensource/audio-kernel
ln -sfn ../../../../vendor/mm-drivers vendor/qcom/opensource/mm-drivers

python3 build_with_bazel.py -t lahaina perf --skip abl

C=$KP/bazel-cache
tools/bazel --output_root=$C --output_user_root=$C/bazel-cache run \
	--incompatible_sandbox_hermetic_tmp=false --noenable_workspace \
	--override_module=rules_kotlin=%workspace%/build/kernel/kleaf/bzlmod/fake_modules/rules_kotlin \
	--override_module=protobuf=%workspace%/build/kernel/kleaf/bzlmod/fake_modules/protobuf \
	--override_module=rules_java=%workspace%/build/kernel/kleaf/bzlmod/fake_modules/rules_java \
	--symlink_prefix=/ \
	//vendor/qcom/opensource/audio-kernel:lahaina_perf_modules_dist

ls out/msm-kernel-lahaina-perf/dist | head -50
ls out/target/product/lahaina/dlkm/lib/modules/
echo STAGE1 DONE
