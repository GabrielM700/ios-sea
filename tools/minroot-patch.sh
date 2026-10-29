#!/bin/bash
# Put the demo jobs (tools/minroot-jobs.py) into an existing root.img, without rebuilding it.
#   sudo tools/minroot-patch.sh root.img [--hid-stub]
set -euo pipefail
img=${1:?root.img}
here=$(cd "$(dirname "$0")" && pwd)
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
m=$(mktemp -d)
mount -t hfsplus -o loop,force "$img" "$m"
trap 'umount "$m" 2>/dev/null; rmdir "$m" 2>/dev/null || true' EXIT
python3 "$here/minroot-jobs.py" "$m/System/Library/LaunchDaemons" "${@:2}"
ls -ln "$m/System/Library/LaunchDaemons"
sync
