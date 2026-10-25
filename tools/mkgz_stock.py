#!/usr/bin/env python3
"""Stock-style gzip: header 1f 8b 08 00 00000000 02 03 (no FNAME, mtime 0, XFL=2, OS=3),
raw deflate level 9, CRC32 + ISIZE trailer. Same recipe as the fleur port's build script.
usage: mkgz_stock.py IN OUT [STOCK_BOOT.IMG]   (3rd arg: assert header == stock kernel's first 10 bytes)
"""
import sys, zlib

HDR = bytes.fromhex("1f8b0800000000000203")

def main(src, dst, stock=None):
    data = open(src, "rb").read()
    c = zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS)
    body = c.compress(data) + c.flush()
    gz = HDR + body + zlib.crc32(data).to_bytes(4, "little") + len(data).to_bytes(4, "little")
    open(dst, "wb").write(gz)
    import gzip
    assert gzip.decompress(gz) == data, "round-trip failed"
    if stock:
        sk = open(stock, "rb").read(4096 + 10)[4096:]
        assert sk == HDR, "stock kernel header differs: %s" % sk.hex()
        print("header matches the stock kernel's gzip header:", sk.hex())
    print("%s: %d bytes -> gzip %d bytes (round-trip ok)" % (src, len(data), len(gz)))

if __name__ == "__main__":
    main(*sys.argv[1:4])
