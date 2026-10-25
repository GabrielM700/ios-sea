#!/usr/bin/env python3
"""Pack an Android boot.img header v3 from a kernel and an (optional) ramdisk,
cloning every other header field from the device's stock boot.img.

usage: pack_boot_v3.py STOCK_BOOT.IMG KERNEL OUT.img [RAMDISK|-]
  KERNEL   raw arm64 Image, or a gzip of it
  RAMDISK  file, or omitted/'-' for a minimal valid gzip'd newc cpio

boot_img_hdr_v3: magic[8] kernel_size ramdisk_size os_version header_size
                 reserved[4] header_version cmdline[1536]   (1580 bytes, page = 4096)
"""
import gzip, io, struct, sys

PAGE = 4096
HDR_SIZE = 1580


def pad(b, n=PAGE):
    return b + b"\0" * (-len(b) % n)


def empty_cpio_gz():
    name = b"TRAILER!!!\0"
    fields = [0] * 13
    fields[11] = len(name)  # c_namesize
    hdr = b"070701" + b"".join(b"%08X" % f for f in fields)
    rec = hdr + name
    rec += b"\0" * (-len(rec) % 4)
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0, compresslevel=9) as g:
        g.write(rec + b"\0" * 124)  # cpio is conventionally padded to 512
    return buf.getvalue()


def main(stock, kernel, out, ramdisk=None):
    s = open(stock, "rb").read(PAGE)
    assert s[:8] == b"ANDROID!", "not a boot image"
    ver = struct.unpack_from("<I", s, 40)[0]
    hsz = struct.unpack_from("<I", s, 20)[0]
    assert ver == 3 and hsz == HDR_SIZE, "stock header is v%d/%d, expected v3/1580" % (ver, hsz)
    k = open(kernel, "rb").read()
    if ramdisk == "stock":  # reuse the stock ramdisk byte for byte
        sk, srd = struct.unpack_from("<II", s, 8)
        with open(stock, "rb") as f:
            f.seek(PAGE + len(pad(b"\0" * sk)))
            rd = f.read(srd)
        assert len(rd) == srd
    elif ramdisk in (None, "-"):
        rd = empty_cpio_gz()
    else:
        rd = open(ramdisk, "rb").read()
    hdr = bytearray(s[:HDR_SIZE])
    struct.pack_into("<II", hdr, 8, len(k), len(rd))
    img = pad(bytes(hdr)) + pad(k) + pad(rd)
    open(out, "wb").write(img)

    # ---- self-check: re-parse and compare against the stock header
    o = open(out, "rb").read()
    assert o[:8] == b"ANDROID!" and len(o) % PAGE == 0
    ksz, rsz, osv, hs = struct.unpack_from("<4I", o, 8)
    assert (ksz, rsz) == (len(k), len(rd))
    assert o[24:HDR_SIZE] == s[24:HDR_SIZE], "reserved/version/cmdline must match stock"
    assert osv == struct.unpack_from("<I", s, 16)[0] and hs == HDR_SIZE
    assert o[HDR_SIZE:PAGE] == b"\0" * (PAGE - HDR_SIZE)
    assert o[PAGE:PAGE + len(k)] == k
    roff = PAGE + len(pad(k))
    assert o[roff:roff + len(rd)] == rd
    print("%s: %d bytes | kernel %d @0x%x | ramdisk %d @0x%x | os_version 0x%x | cmdline %r" % (
        out, len(o), ksz, PAGE, rsz, roff, osv, o[44:44 + 1536].split(b"\0")[0].decode()))


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) < 3:
        sys.exit(__doc__)
    main(a[0], a[1], a[2], a[3] if len(a) > 3 else None)
