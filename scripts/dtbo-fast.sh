#!/bin/bash
# Rebuild only redwood-overlay.dtbo and dtbo.img, without Kleaf.
set -e
KP=~/redwood-6.12-kernel/kernel_platform
DT=$KP/qcom/opensource/devicetree
B=$KP/prebuilts/clang/host/linux-x86/clang-r536225/bin
O=${O:-~/work/dtfast}
mkdir -p $O
$B/clang -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp -P \
	-I $DT/qcom -I $DT/bindings -I $DT/include \
	-I $KP/soc-repo/include -I $KP/soc-repo/arch/arm64/boot/dts \
	-I $KP/common/include -I $KP/common/scripts/dtc/include-prefixes \
	-o $O/redwood-overlay.pp $DT/qcom/redwood-overlay.dtso
$KP/external/qcom-dtc/dtc -@ -q -I dts -O dtb -o $O/redwood-overlay.dtbo $O/redwood-overlay.pp 2>/dev/null || dtc -@ -q -I dts -O dtb -o $O/redwood-overlay.dtbo $O/redwood-overlay.pp
python3 $KP/external/libufdt/utils/src/mkdtboimg.py create $O/dtbo.img $O/redwood-overlay.dtbo
sha1sum $O/dtbo.img
