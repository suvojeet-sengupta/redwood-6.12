# Audio

`audio-kernel-legacy.lnx.6.12.r1-rel` from
`platform/vendor/opensource/audio-kernel` still builds the lahaina machine
driver (q6/APR, bolero, WCD938x/WCD937x, WSA883x). It only builds with Kleaf:
copy it to `kernel_platform/vendor/qcom/opensource/audio-kernel`, copy
`mm-drivers` (with [../../patches/mm-drivers](../../patches/mm-drivers),
which adds lahaina to its targets) next to it, and build
`//vendor/qcom/opensource/audio-kernel:lahaina_perf_modules` with the same
Bazel flags `build_with_bazel.py` uses.

The device tree needs `audio-kernel/include/bindings` as
`dt-bindings/clock/qcom,audio-ext-clk.h` and
`dt-bindings/sound/{qcom,bolero-clk-rsc.h,audio-codec-port-types.h}`.

Status: all 30 modules load in the bring-up shell with the ADSP running,
but no sound card registers yet.
