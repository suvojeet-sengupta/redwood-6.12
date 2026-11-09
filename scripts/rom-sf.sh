#!/bin/bash
# Publish the newest redwood build of a ROM on SourceForge, replacing the
# newest date folder there (20261108/ goes, 20261109/ comes in). Older date
# folders stay. The OTA feed isn't touched.
# Usage: rom-sf.sh aospa|pengu [--wait] [-y] [--dry-run] [zip]
#   --wait  wait for a running rom-build.sh to finish and for a new zip
#   -y      don't ask before deleting the old folder
#   --dry-run  show what would happen, change nothing on SourceForge
#   zip     the zip to publish (default: the newest one in the ROM's tree)
set -e

case "$1" in
aospa)
	TREE=${AOSPA:-$HOME/aospa}; GLOB='aospa-*-redwood-*.zip'; SF_ROM=AOSPA ;;
pengu|penguin|penguinos)
	TREE=${PENGU:-$HOME/pengu}; GLOB='PenguinOS-*-redwood.zip'; SF_ROM=PenguinOS ;;
-h|--help|"")
	sed -n '2,10p' "$0"; exit 0 ;;
*)
	echo "unknown ROM '$1', use aospa or pengu" >&2; exit 1 ;;
esac
shift
KEY=${SF_KEY:-$HOME/.ssh/sourceforge_ed25519}
SF_USER=${SF_USER:-suvojeet123}
SF_HOST=frs.sourceforge.net
SF_DIR=/home/frs/project/builds-by-suvojeet/$SF_ROM/redwood
DL=https://downloads.sourceforge.net/project/builds-by-suvojeet/$SF_ROM/redwood
SSH_OPTS="-o BatchMode=yes -o IdentitiesOnly=yes -i $KEY"

WAIT=0; YES=0; DRY=0; ZIP=
for a in "$@"; do
	case "$a" in
	--wait) WAIT=1 ;;
	-y) YES=1 ;;
	--dry-run|-n) DRY=1 ;;
	*) ZIP=$a ;;
	esac
done

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m!!\033[0m %s\n' "$*" >&2; exit 1; }
sf() { printf '%s\n' "$@" | sftp -q $SSH_OPTS -b - "$SF_USER@$SF_HOST"; }
newest_zip() {
	ls -t "$TREE"/$GLOB 2>/dev/null | grep -v -E 'target_files|-img' | head -1
}

[ -r "$KEY" ] || die "no SourceForge key at $KEY"

if [ "$WAIT" = 1 ]; then
	before=$(newest_zip); before_t=$( [ -n "$before" ] && stat -c %Y "$before" || echo 0)
	say "waiting for the build to finish"
	while pgrep -f '[r]om-build.sh' >/dev/null || [ "$(stat -c %Y "$(newest_zip)" 2>/dev/null || echo 0)" -le "$before_t" ]; do
		if ! pgrep -f '[r]om-build.sh' >/dev/null && [ "$(stat -c %Y "$(newest_zip)" 2>/dev/null || echo 0)" -le "$before_t" ]; then
			die "rom-build.sh is not running and there is no new zip"
		fi
		printf '\r    still building... %s' "$(date +%H:%M:%S)"
		sleep 30
	done
	echo
fi

[ -n "$ZIP" ] || ZIP=$(newest_zip)
[ -f "$ZIP" ] || die "no $SF_ROM zip found in $TREE"
NAME=$(basename "$ZIP")
DATE=$(echo "$NAME" | grep -o -E '20[0-9]{6}' | head -1)
[ -n "$DATE" ] || die "no date in $NAME"
say "zip: $NAME ($(du -h "$ZIP" | cut -f1)), date folder $DATE"

TS=$(unzip -p "$ZIP" META-INF/com/android/metadata 2>/dev/null | sed -n 's/^post-timestamp=//p')
BD=$(cat "$TREE/out/build_date.txt" 2>/dev/null || true)
say "post-timestamp $TS, build date $BD"
[ -n "$BD" ] && [ "$TS" != "$BD" ] &&
	printf '\033[1;33m!!\033[0m post-timestamp is not this build'"'"'s date; build with -i for a fresh date\n'

say "sha256"
if [ "$DRY" = 1 ]; then sha256sum "$ZIP" | sed 's#  .*/#  #'; else (cd "$(dirname "$ZIP")" && sha256sum "$NAME" | tee "$NAME.sha256"); fi

say "SourceForge folders now"
LIST=$(sf "ls -1 $SF_DIR" | grep -v '^sftp>' | sed 's#.*/##' | grep -E '^20[0-9]{6}$' | sort)
echo "$LIST" | sed 's/^/    /'
OLD=
echo "$LIST" | grep -qx "$DATE" || OLD=$(echo "$LIST" | tail -1)

if [ -n "$OLD" ] && [ "$OLD" \> "$DATE" ]; then
	die "SourceForge already has a newer folder ($OLD) than $DATE"
fi

if [ -n "$OLD" ]; then
	say "will delete $SF_DIR/$OLD and upload to $SF_DIR/$DATE"
else
	say "will upload to $SF_DIR/$DATE"
fi
[ "$DRY" = 1 ] && { say "dry run, stopping here"; exit 0; }

if [ -n "$OLD" ]; then
	if [ "$YES" != 1 ]; then
		read -r -p "    go ahead? [y/N] " r
		[ "$r" = y ] || [ "$r" = Y ] || die "stopped"
	fi
	FILES=$(sf "ls -1 $SF_DIR/$OLD" | grep -v '^sftp>' | sed 's#.*/##')
	CMDS=()
	for f in $FILES; do CMDS+=("rm $SF_DIR/$OLD/$f"); done
	CMDS+=("rmdir $SF_DIR/$OLD")
	sf "${CMDS[@]}" >/dev/null
	say "deleted $OLD"
fi

if echo "$LIST" | grep -qx "$DATE"; then
	say "$DATE already exists, its files get replaced"
else
	sf "mkdir $SF_DIR/$DATE" >/dev/null
fi

say "uploading"
rsync -t --partial --info=progress2 -e "ssh $SSH_OPTS" \
	"$ZIP" "$ZIP.sha256" "$SF_USER@$SF_HOST:$SF_DIR/$DATE/"

say "checking"
sf "ls -l $SF_DIR/$DATE" | grep -v '^sftp>' | sed 's/^/    /'
SIZE=$(stat -c %s "$ZIP")
for i in 1 2 3 4 5 6; do
	LEN=$(curl -sIL --max-time 60 "$DL/$DATE/$NAME" | grep -i '^content-length' | tail -1 | tr -dc 0-9)
	[ "$LEN" = "$SIZE" ] && break
	sleep 20
done
REMOTE_SHA=$(curl -sL --max-time 60 "$DL/$DATE/$NAME.sha256" | cut -d' ' -f1)
LOCAL_SHA=$(cut -d' ' -f1 "$ZIP.sha256")
[ "$LEN" = "$SIZE" ] || die "public link size $LEN, expected $SIZE"
[ "$REMOTE_SHA" = "$LOCAL_SHA" ] || die "public sha256 $REMOTE_SHA, expected $LOCAL_SHA"
say "done: $DL/$DATE/$NAME"
