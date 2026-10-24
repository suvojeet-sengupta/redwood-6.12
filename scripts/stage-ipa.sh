#!/bin/bash
# dataipa (GSI, IPA, rmnet IPA) and rmnet for lahaina.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
cd $KP
mkdir -p vendor/qcom/opensource
for p in dataipa datarmnet datarmnet-ext; do ln -sfn ../../../../vendor/$p vendor/qcom/opensource/$p; done
C=$KP/bazel-cache
tools/bazel --output_root=$C --output_user_root=$C/bazel-cache run \
	--incompatible_sandbox_hermetic_tmp=false --noenable_workspace \
	--override_module=rules_kotlin=%workspace%/build/kernel/kleaf/bzlmod/fake_modules/rules_kotlin \
	--override_module=protobuf=%workspace%/build/kernel/kleaf/bzlmod/fake_modules/protobuf \
	--override_module=rules_java=%workspace%/build/kernel/kleaf/bzlmod/fake_modules/rules_java \
	--symlink_prefix=/ \
	//vendor/qcom/opensource/dataipa:lahaina_perf_modules_dist
find out/target/product/lahaina -name "*.ko" -newer ~/work/stage-ipa.sh | xargs -n1 basename
echo STAGE-IPA DONE
