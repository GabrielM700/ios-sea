#!/bin/bash
# Send a kernel payload plus a big root image to the sea USB loader and boot it.
#   tools/seastage.sh PAYLOAD.bin ROOT.img
# PAYLOAD.bin comes from mkpayload.py ... --ramdisk ROOT.img --ramdisk-inplace (it prints ramdisk_stage_at);
# ROOT.img is NOT inside it: it is staged in 1 GiB pieces at that address, the payload last at 0xc0100000
# (needs 1 GiB free in $TMPDIR for one piece at a time).
# Needs the loader image (sea-usb-fb5.img or newer) running: fastboot devices shows SEA0001.
set -euo pipefail
PAYLOAD=${1:?payload.bin}; ROOT=${2:?root.img}
PIECE=$((1024*1024*1024))
size=$(stat -c %s "$ROOT")
# where the payload wants the root: the ramdisk segment (file_off = ~0) of its header, pa = va - virt_base + phys_base
RD_PA=$(python3 - "$PAYLOAD" <<'PY'
import struct, sys
b = open(sys.argv[1], 'rb').read(0x2a8 + 32 * 64)
assert b[:8] == b'SWEETXNU', 'not a SWEETXNU payload'
n, = struct.unpack_from('<I', b, 12)
virt, = struct.unpack_from('<Q', b, 0x10); phys, = struct.unpack_from('<Q', b, 0x38)
for i in range(n):
    va, vs, fo, fs = struct.unpack_from('<4Q', b, 0x2a8 + 32 * i)
    if fo == 0xFFFFFFFFFFFFFFFF:
        print(va - virt + phys); break
else:
    sys.exit('payload has no in-place ramdisk: build it with --ramdisk-inplace')
PY
)
printf 'root %d bytes -> %#x, then the payload at 0xc0100000\n' "$size" "$RD_PA"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
a=$RD_PA
off=0; n=0
while [ $off -lt $size ]; do
	len=$(( size - off < PIECE ? size - off : PIECE ))
	dd if="$ROOT" of="$tmp/piece" bs=1M iflag=skip_bytes,count_bytes skip=$off count=$len status=none
	echo "piece $n: $len bytes -> $(printf '%#x' $a)"
	fastboot oem addr "$(printf '%x' "$a")"
	fastboot stage "$tmp/piece"
	a=$(( a + len )); off=$(( off + len )); n=$(( n + 1 ))
done
fastboot oem addr c0100000
fastboot stage "$PAYLOAD"
fastboot oem go
