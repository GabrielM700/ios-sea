#!/bin/bash
# Copy files from the iOS OS image into an EXISTING root.img (no rebuild of the 2.6 GB cache part).
#   tools/minroot-addfiles.sh OS.dmg root.img usr/libexec/xpcproxy [more/paths ...]
# Paths are relative to the OS root; files are copied with their directories (rsync -R). Needs apfs-fuse + sudo.
set -euo pipefail
[[ $# -ge 3 ]] || { sed -n '2,5p' "$0"; exit 2; }
os=$(realpath "$1") img=$(realpath "$2"); shift 2
apfs_fuse=${APFS_FUSE:-$(command -v apfs-fuse || true)}
[[ -n $apfs_fuse ]] || { echo "missing apfs-fuse" >&2; exit 1; }
work=$(mktemp -d)
cleanup() {
	set +e
	for m in root os; do
		sudo umount "$work/$m" 2>/dev/null || sudo fusermount -u "$work/$m" 2>/dev/null || sudo fusermount3 -u "$work/$m" 2>/dev/null
	done
	if grep -q " $work/" /proc/mounts; then echo "still mounted under $work: left in place" >&2
	else sudo rm -rf --one-file-system "$work"; fi
}
trap cleanup EXIT
ln -s "$os" "$work/os.dmg"; mkdir "$work/os" "$work/root"
sudo "$apfs_fuse" "$work/os.dmg" "$work/os"
sudo mount -t hfsplus -o loop,force "$img" "$work/root"
for f in "$@"; do
	sudo test -e "$work/os/root/$f" || { echo "not in the OS image: $f" >&2; exit 1; }
	(cd "$work/os/root" && sudo rsync -aHR --numeric-ids "./$f" "$work/root/")
	echo "added $f"
done
sudo ls -ln "$work/root/usr/libexec" | head
sync
