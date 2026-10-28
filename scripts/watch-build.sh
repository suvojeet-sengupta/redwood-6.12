#!/bin/bash
# Live view of build-all: finished stages, the one running, and its newest lines.
L=${1:-$HOME/work/build-all-mh.log}
while :; do
	cur=$(grep "^== " $L 2>/dev/null | tail -1 | cut -c4-)
	out=$HOME/work/$cur.out
	clear
	echo "build-all  $(date +%H:%M:%S)   log: $L"
	echo "stages: $(grep "^== " $L 2>/dev/null | cut -c4- | tr "\n" " ")"
	if grep -qE "failed$" $L; then echo; echo ">>> FAILED"; tail -15 $L
	elif tail -1 $L | grep -q "dtbo.img$"; then echo; echo ">>> DONE"; tail -2 $L
	else echo "running: $cur"; echo; [ -f $out ] && tail -c 2000 $out | tr "\r" "\n" | grep -v "^\s*$" | tail -12 | cut -c1-${COLUMNS:-150}; fi
	sleep 2
done
