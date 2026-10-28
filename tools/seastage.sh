#!/bin/bash
# Send a kernel payload plus a big root image to the sea USB loader and boot it.
#   tools/seastage.sh PAYLOAD.bin ROOT.img
# PAYLOAD.bin comes from mkpayload.py ... --ramdisk ROOT.img --ramdisk-pa 0x140000000 (it prints payload_pa);
# ROOT.img is NOT inside it: it is staged in 1 GiB pieces at 0x140000000+, the payload last, right after it
# (needs 1 GiB free in $TMPDIR for one piece at a time).
# Needs the loader image (sea-usb-fb5.img or newer) running: fastboot devices shows SEA0001.
set -euo pipefail
PAYLOAD=${1:?payload.bin}; ROOT=${2:?root.img}
RD_PA=${RD_PA:-0x140000000}
PIECE=$((1024*1024*1024))
size=$(stat -c %s "$ROOT"); rd_size=$(( (size + 0x3fff) & ~0x3fff ))
payload_pa=$(( (RD_PA + rd_size + 0xfffff) & ~0xfffff ))
printf 'root %d bytes at %#x, payload at %#x\n' "$size" "$RD_PA" "$payload_pa"
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
fastboot oem addr "$(printf '%x' "$payload_pa")"
fastboot stage "$PAYLOAD"
fastboot oem go
