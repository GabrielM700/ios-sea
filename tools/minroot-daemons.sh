#!/bin/bash
# Add real iOS daemons to an EXISTING root.img: copies each daemon's binary from the OS image and
# rewrites /System/Library/xpc/launchd.plist (demo jobs + the labels you name; their output goes to the console and
# _PanicOnCrash is renamed so a crash does not panic the kernel).
#   tools/minroot-daemons.sh OS.dmg root.img --list [PATTERN]          what the stock cache has
#   tools/minroot-daemons.sh OS.dmg root.img LABEL [LABEL ...]          e.g. com.apple.logd com.apple.notifyd
# Needs apfs-fuse, sudo, python3.
set -euo pipefail
[[ $# -ge 3 ]] || { sed -n '2,8p' "$0"; exit 2; }
os=$(realpath "$1") img=$(realpath "$2"); shift 2
here=$(cd "$(dirname "$0")" && pwd)
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
O=$work/os/root
sudo cp "$O/System/Library/xpc/launchd.plist" "$work/stock.plist"
if [[ $1 == --list ]]; then
	sudo python3 "$here/minroot-programs.py" "$work/stock.plist" --list "${2:-}"
	exit 0
fi
sudo mount -t hfsplus -o loop,force "$img" "$work/root"
R=$work/root
sudo python3 "$here/minroot-programs.py" "$work/stock.plist" "$@" > "$work/found.txt" || true
labels=()
while IFS=$'\t' read -r label program; do
	[[ -n $label ]] || continue
	labels+=("$label")
	if [[ -n $program ]] && sudo test -e "$O$program"; then
		sudo rsync -aHR --numeric-ids "$O/./${program#/}" "$R/" && echo "  $label: $program"
	else
		echo "  $label: program '$program' not in the OS image (job kept anyway)" >&2
	fi
done < "$work/found.txt"
[[ ${#labels[@]} -gt 0 ]] || { echo "none of the labels exist in the stock cache (try --list)" >&2; exit 1; }
# the logs/db directories logd and friends insist on (same as mkiosroot.sh)
sudo install -d -o 272 -g 272 -m 0751 "$R/private/var/db/diagnostics"
sudo install -d -o 272 -g 272 -m 0755 "$R/private/var/db/uuidtext" "$R/private/var/db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do
	sudo install -o 272 -g 272 -m 0644 /dev/null "$R/private/var/db/$d/.migrated"
done
sudo python3 "$here/minroot-launchd.py" "$work/stock.plist" "$work/new.plist" --dir "$R/System/Library/LaunchDaemons" --keep "${labels[@]}"
sudo install -m 0644 -o 0 -g 0 "$work/new.plist" "$R/System/Library/xpc/launchd.plist"
sync
echo "done: ${labels[*]}"
