#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
CF="--target=aarch64-linux-gnu -O2 -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-builtin -Wall -Wextra"
mkdir -p out
for f in start.S fx.c console.c; do clang $CF -c $f -o out/${f%.*}.o; done
ld.lld -T fx.ld --build-id=none -N -o out/fx.elf out/start.o out/fx.o out/console.o
python3 mkmacho.py out/fx.elf out/fakexnu.macho
