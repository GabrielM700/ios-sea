#!/bin/bash
# Minimal iOS 16 root for the RAM disk (rd=md0): launchd, dyld, the shared cache and a few
# stock tools, as ONE journaled HFS+ image of about 2 GB. Run it on your own PC (Linux), with the
# two IPSW disk images; nothing from Apple is stored in the repository.
#
#   mkminroot.sh OS.dmg SYSTEMOS.dmg OUT.img [--keep LABEL ...]
#
# OS.dmg / SystemOS.dmg: the iPhone10,3 16.7.16 (20H392) IPSW members (see BuildManifest.plist;
# tools/remotezip.py fetches single members). apfs-fuse (with LZBITMAP, 2021+), hfsprogs, rsync, sudo.
#
# What goes in: /sbin /bin /usr/bin /usr/sbin /usr/lib /usr/libexec/{dyld*} (small), the SystemOS
# cryptex's dyld shared cache at /System/Library/Caches/com.apple.dyld (dyld at ignition_level=1 looks
# there), a launchd job cache with only demo jobs (tools/minroot-launchd.py), the /private skeleton and
# the plain system keybag. Not in: frameworks (they are in the cache), apps, daemons, fonts, ...
# Space: the script prints the staged size; the image is that plus 12 % and 128 MiB.
set -euo pipefail

[[ $# -ge 3 ]] || { sed -n '2,15p' "$0"; exit 2; }
os=$(realpath "$1") sysos=$(realpath "$2") out=$3; shift 3
keep=()
if [[ ${1:-} == --keep ]]; then shift; keep=("$@"); fi
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)

apfs_fuse=${APFS_FUSE:-$(command -v apfs-fuse || true)}
miss=()
[[ -n $apfs_fuse ]] || miss+=(apfs-fuse)
for t in mkfs.hfsplus fsck.hfsplus rsync python3; do command -v $t >/dev/null || miss+=($t); done
[[ ${#miss[@]} == 0 ]] || { echo "missing tools: ${miss[*]}" >&2; exit 1; }

work=$(mktemp -d)
cleanup() {
	set +e
	# The FUSE mounts belong to root: only root can see them, so ALWAYS try to unmount
	# (a user-side `mountpoint` says "no" and a following rm -rf would walk into the image).
	for m in root sysos os; do
		sudo umount "$work/$m" 2>/dev/null || sudo fusermount -u "$work/$m" 2>/dev/null || sudo fusermount3 -u "$work/$m" 2>/dev/null
	done
	if grep -q " $work/" /proc/mounts; then
		echo "still mounted under $work: left in place (sudo umount them, then rm -rf $work)" >&2
	else
		sudo rm -rf --one-file-system "$work"
	fi
}
trap cleanup EXIT

apfs_mount() {	# apfs-fuse splits options on ',' (iPhone10,3 paths) and wants *.dmg: use links
	ln -s "$1" "$work/$2.dmg"; mkdir "$work/$2"
	sudo "$apfs_fuse" "$work/$2.dmg" "$work/$2"
}
apfs_mount "$os" os
apfs_mount "$sysos" sysos
O=$work/os/root S=$work/sysos/root
stage=$work/stage
sudo mkdir -p "$stage"

# apfs-fuse runs as root and the mounts are not readable by you: every test below uses sudo.
sudo test -e "$O/sbin/launchd" || {
	echo "OS.dmg has no /sbin/launchd: it is not the root filesystem. Top level:" >&2
	sudo ls "$O" | head -20 >&2; exit 1; }
[[ ${NOCACHE:-0} == 1 ]] || sudo test -d "$S/System/Library/Caches/com.apple.dyld" || {
	echo "SystemOS.dmg has no System/Library/Caches/com.apple.dyld: not the SystemOS cryptex. Top level:" >&2
	sudo ls "$S" | head -20 >&2; exit 1; }

echo "== staging the OS files"
for d in sbin bin usr/bin usr/sbin usr/lib; do
	sudo mkdir -p "$stage/$(dirname "$d")"
	if ! sudo test -e "$O/$d"; then echo "  (no $d in this OS image)"; continue; fi
	set +e; sudo rsync -aH --numeric-ids "$O/$d" "$stage/$(dirname "$d")/" 2>>"$work/rsync-failed.log"; rc=$?; set -e
	[[ $rc == 0 || $rc == 23 ]] || exit $rc
done
sudo mkdir -p "$stage/usr/libexec"
sudo find "$O/usr/libexec" -maxdepth 1 -name 'dyld*' -exec rsync -aH {} "$stage/usr/libexec/" \; || true
# top-level symlinks and the /private skeleton
for l in etc var tmp; do if sudo test -L "$O/$l"; then sudo rsync -aH "$O/$l" "$stage/"; fi; done
sudo mkdir -p "$stage/private" "$stage/dev" "$stage/System/Library/xpc" "$stage/System/Library/LaunchDaemons"
sudo rsync -aH --numeric-ids "$O/private/etc" "$stage/private/" || true
sudo install -d -m 0755 "$stage/private/var/root" "$stage/private/var/run" "$stage/private/var/log" \
	"$stage/private/var/tmp" "$stage/private/var/db" "$stage/private/var/mobile"
sudo install -d -m 1777 "$stage/private/tmp"

if [[ ${NOCACHE:-0} == 1 ]]; then
	echo "== NOCACHE=1: no shared cache (launchd will not get past dyld; this only tests the HFS+ mount and exec)"
else
	echo "== shared cache from the SystemOS cryptex"
	dsc=System/Library/Caches/com.apple.dyld
	sudo mkdir -p "$stage/$dsc"
	# iOS 16 split cache: dyld_shared_cache_arm64, .01 ... .NN, and the .dyldlinkedit / .dylddata parts
	# are all needed; only the map/atlas/symbols files are debugging aids.
	sudo find "$S/$dsc" -maxdepth 1 -type f -name 'dyld_shared_cache_*' ! -name '*.map' ! -name '*.atlas' ! -name '*.symbols' \
		-exec rsync -aH --numeric-ids {} "$stage/$dsc/" \;
	sudo sh -c "ls -l '$stage/$dsc' | tail -n +2 | wc -l" | sed 's/^/  files: /'
	sudo du -sh "$stage/$dsc" | sed 's/^/  cache size: /'

fi

echo "== launchd job cache"
sudo python3 "$here/minroot-launchd.py" "$O/System/Library/xpc/launchd.plist" "$stage/System/Library/xpc/launchd.plist" ${keep:+--keep "${keep[@]}"}
sudo chmod 0644 "$stage/System/Library/xpc/launchd.plist"

# system keybag for the built-in AppleKeyStore (no passcode); logd stores
sudo install -d -m 0700 "$stage/private/var/keybags"
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
 '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
 '<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' |
 sudo install -m 0600 /dev/stdin "$stage/private/var/keybags/systembag.kb"

if [[ -s $work/rsync-failed.log ]]; then
	echo "warning: apfs-fuse could not read some files (LZBITMAP?):" >&2
	head -5 "$work/rsync-failed.log" >&2
	echo "  use sgan81/apfs-fuse from 2021 or later (see tools/mkiosroot.sh)" >&2
fi

bytes=$(sudo du -sb "$stage" | cut -f1)
img=$(( (bytes * 112 / 100 + 128*1024*1024 + 16*1024*1024 - 1) / (16*1024*1024) * (16*1024*1024) ))
echo "== staged $((bytes/1024/1024)) MiB -> image $((img/1024/1024)) MiB"
truncate -s "$img" "$out"
mkfs.hfsplus -s -J -v iOSRoot "$out"
mkdir "$work/root"
sudo mount -t hfsplus -o loop,force "$out" "$work/root"; mounts+=("$work/root")
sudo rsync -aH --numeric-ids "$stage/" "$work/root/"
df -h "$work/root" | tail -1
sudo umount "$work/root"
fsck.hfsplus -f -y "$out" | grep -v 'Illegal name' || true
echo "done: $out"
