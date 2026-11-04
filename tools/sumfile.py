#!/usr/bin/env python3
"""Print the 64-bit sum of the little-endian 8-byte words of FILE (the last <8 bytes are ignored),
the same number the shim shows as SUM after `fastboot oem sum 1` + `fastboot stage FILE`.

usage: sumfile.py FILE
"""
import struct, sys

data = open(sys.argv[1], "rb").read()
n = len(data) // 8
s = 0
CH = 1 << 20
for i in range(0, n, CH):
    m = min(CH, n - i)
    s += sum(struct.unpack_from("<%dQ" % m, data, i * 8))
print("%016X" % (s & (2**64 - 1)))
