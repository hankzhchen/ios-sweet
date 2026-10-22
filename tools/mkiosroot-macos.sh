#!/bin/bash
# macOS variant of mkiosroot.sh: same root layout, built with the native APFS
# and HFS+ support instead of apfs-fuse/hfsprogs. Run as root (owners, modes
# and setuid bits must survive the copy):
#
#   sudo mkiosroot-macos.sh OS.dmg SYSTEMOS.dmg APPOS.dmg OUT.img [SIZE]   (SIZE default 16g)
#
# OUT.img is a raw, sparse case-sensitive journaled HFS+ volume for the
# userdata partition (fastboot flash userdata OUT.img). Reading the APFS
# images through macOS decompresses their files, so the copy is uncompressed,
# as with apfs-fuse. See mkiosroot.sh for why each step exists.
set -euo pipefail

[[ $# -ge 4 ]] || { sed -n '2,12p' "$0"; exit 2; }
[[ $EUID == 0 ]] || { echo "run with sudo" >&2; exit 1; }
os=$(realpath "$1") sysos=$(realpath "$2") appos=$(realpath "$3") out=$4 size=${5:-16g}
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }
RSYNC=${RSYNC:-/opt/homebrew/bin/rsync}   # GNU rsync: -H and --numeric-ids
[[ -x $RSYNC ]] || { echo "need GNU rsync (brew install rsync)" >&2; exit 1; }

work=$(mktemp -d /tmp/mkiosroot.XXXXXX)
devs=()
cleanup() {
	for m in "$work"/root "$work"/os "$work"/sysos "$work"/appos; do
		mount | grep -q " on $m " && umount "$m" 2>/dev/null || true
	done
	for d in ${devs[@]+"${devs[@]}"}; do hdiutil detach -quiet "$d" 2>/dev/null || true; done
	rmdir "$work"/* "$work" 2>/dev/null || true
}
trap cleanup EXIT

# Read-only, owners honoured (disk images default to "ignore ownership").
attach_ro() {	# image mountpoint [raw]
	mkdir "$work/$2"
	local opts=(-readonly -nobrowse -owners on -mountpoint "$work/$2")
	[[ ${3:-} == raw ]] && opts+=(-imagekey diskimage-class=CRawDiskImage)
	hdiutil attach "${opts[@]}" "$1" >/dev/null
}
attach_ro "$os" os
attach_ro "$sysos" sysos raw
attach_ro "$appos" appos raw

mkfile -n "$size" "$out"
dev=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$out" | awk 'NR==1 {print $1}')
devs+=("$dev")
newfs_hfs -s -J -v iOSRoot "$dev"
mkdir "$work/root"
mount -t hfs "$dev" "$work/root"	# mounted by root: ownership is honoured

copy() {
	echo "copying $1 -> ${2#"$work/root"}"
	mkdir -p "$2"
	"$RSYNC" -aH --numeric-ids "$1/" "$2/"
}
copy "$work/os" "$work/root"
copy "$work/sysos" "$work/root/private/preboot/Cryptexes/OS"
copy "$work/appos" "$work/root/private/preboot/Cryptexes/App"

dsc=private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
mkdir -p "$work/root/System/Library/Caches/com.apple.dyld"
for f in "$work/root/$dsc"/*; do
	ln "$f" "$work/root/System/Library/Caches/com.apple.dyld/${f##*/}"
done

for c in OS App; do
	(cd "$work/root/private/preboot/Cryptexes/$c" && find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
	while IFS= read -r f; do
		f=${f#./}
		if [[ ! -e $work/root/$f && ! -L $work/root/$f ]]; then
			mkdir -p "$work/root/$(dirname "$f")"
			src="$work/root/private/preboot/Cryptexes/$c/$f"
			if [[ -L $src ]]; then cp -P "$src" "$work/root/$f"; else ln "$src" "$work/root/$f"; fi
			echo "linked $c:/$f"
		fi
	done
done

db="$work/root/private/var/db"
install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do
	install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"
done

install -d -m 0700 -o 0 -g 0 "$work/root/private/var/keybags"
kb=$(mktemp /tmp/systembag.XXXXXX)
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' > "$kb"
install -m 0600 -o 0 -g 0 "$kb" "$work/root/private/var/keybags/systembag.kb"
rm -f "$kb"

python3 -I "$(dirname "$0")/sweet-root-tweaks.py" "$work/root"

df -h "$work/root"
umount "$work/root"
fsck_hfs -f -y "$dev" || true
fsck_hfs -f -n "$dev"
hdiutil detach -quiet "$dev"
devs=()
ls -ls "$out"
