#!/bin/bash
# Every 6.12 module redwood's Android test images need, in order, stopping at
# the first stage that fails. FROM=<stage> starts later in the list. Then
# pack-android.sh packs them with the 5.4 ramdisks in ~/work/android.
cd ~/work
stages="stage1 stage-ipa setup-k612 stage2 build-small build-video build-btpower build-camera build-fingerprint build-haptics dtbo-fast"
[ -n "$FROM" ] && stages=${stages#*${FROM%% *}} && stages="$FROM$stages"
for s in $stages; do
	echo "== $s"
	if ! bash $s.sh > $s.out 2>&1; then
		tail -${N:-15} $s.out
		echo "$s failed"
		exit 1
	fi
	tail -${N:-4} $s.out
done
