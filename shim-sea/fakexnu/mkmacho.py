#!/usr/bin/env python3
"""Wrap a flat arm64 ELF (text at a high VA) in a minimal MH_EXECUTE Mach-O that tools/mkpayload.py accepts."""
import struct, subprocess, sys
elf, out = sys.argv[1:3]
subprocess.check_call(['llvm-objcopy', '-O', 'binary', elf, elf + '.bin'])
code = open(elf + '.bin', 'rb').read()
syms = {l.split()[2]: l.split()[0] for l in subprocess.check_output(['llvm-nm', elf], text=True).splitlines() if len(l.split()) == 3}
entry = int(syms['_start'], 16)
end = int(syms['_end'], 16)
text_va = entry - 0x4000
filesz = 0x4000 + len(code)
vmsize = (end - text_va + 0x3fff) & ~0x3fff
ncmds = 2
thread = struct.pack('<4I', 5, 16 + 68 * 4 + 0, 6, 68) + b'\0' * (68 * 4)
thread = bytearray(thread)
struct.pack_into('<Q', thread, 16 + 32 * 8, entry)
struct.pack_into('<I', thread, 0 + 4, len(thread))
seg = struct.pack('<II16sQQQQiiII', 0x19, 72, b'__TEXT', text_va, vmsize, 0, filesz, 7, 5, 0, 0)
hdr = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, ncmds, len(seg) + len(thread), 0, 0)
img = bytearray(hdr + seg + bytes(thread))
img += b'\0' * (0x4000 - len(img))
img += code
open(out, 'wb').write(img)
print('macho', out, 'entry %#x text_va %#x vmsize %#x filesz %#x' % (entry, text_va, vmsize, filesz))
