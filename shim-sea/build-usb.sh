#!/bin/bash
# sea XNU loader shim -> boot.img v3 (stock header/vendor layout) carrying a SWEETXNU payload as ramdisk.
#   build-xnu.sh PAYLOAD.bin OUT.img
set -euo pipefail
cd "$(dirname "$0")"
PAYLOAD=${1:?payload.bin from tools/mkpayload.py}
OUT=${2:-out/sea-xnu.img}
T=aarch64-linux-gnu
CF="--target=$T -O2 -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-builtin -Wall -Wextra -Wno-unused-function -DUSB_PROBE"
STOCK=${STOCK:-../stock/boot.img}
TOOLS=${TOOLS:-../tools}
mkdir -p out/usb
SRC="start.S shim.c console.c mmu.c fdt.c ufs.c usb.c"
OBJS=""
for f in $SRC; do clang $CF -c $f -o out/usb/${f%.*}.o; OBJS="$OBJS out/usb/${f%.*}.o"; done
abs=$(llvm-readelf -r $(ls out/usb/*.o | grep -v start.o) | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
ld.lld -T shim.ld --build-id=none -N -o out/usb/shim.elf $OBJS
llvm-objcopy -O binary out/usb/shim.elf out/usb/Image
size=$(llvm-nm out/usb/shim.elf | awk '$3=="_end"{print "0x"$1}')
truncate -s $((size)) out/usb/Image
python3 - <<'PY'
import os
p='out/usb/Image'; n=os.path.getsize(p)
print('shim image', n)
if n < 0x40000: open(p,'ab').write(b'\0'*(0x40000-n))
PY
python3 "$TOOLS/mkgz_stock.py" out/usb/Image out/usb/Image.gz
python3 "$TOOLS/pack_boot_v3.py" "$STOCK" out/usb/Image.gz "$OUT" "$PAYLOAD"
ls -l out/usb/Image out/usb/Image.gz "$OUT"
