#!/usr/bin/env python3
"""Swap the device tree appended to a repacked UEFI boot image.

Layout, as measured on both known-good images:

    page 0        Android boot header, header_version 0, page 4096
    page 1 ..     gzip stream: MBN header || AArch64 Image
    after gzip    raw FDT, then padding

and the header's kernel_size is exactly:

    kernel_size = len(gzip_stream) + fdt.totalsize

verified to hold on both work/xiaomi-courbet.img and
work/poco-x3-pro-uefi.img.

Rebuilding naively risks getting the padding wrong, so every run first
rebuilds the image with its own device tree and checks the result is
byte-identical to the input. Only then is the substitution applied.
"""

import hashlib
import os
import struct
import sys
import zlib


def u32le(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


class Layout(object):
    def __init__(self, path):
        self.path = path
        self.orig = open(path, "rb").read()
        data = self.orig
        if data[:8] != b"ANDROID!":
            raise SystemExit("%s: not an Android boot image" % path)
        self.page = u32le(data, 36)
        self.kernel_size = u32le(data, 8)
        self.ramdisk_size = u32le(data, 16)

        stream = zlib.decompressobj(31)
        inflated = stream.decompress(data[self.page:])
        tail = stream.unused_data
        self.gz_start = self.page
        self.gz_end = len(data) - len(tail)
        self.gz = data[self.gz_start:self.gz_end]
        self.inflated_size = len(inflated)
        self.magic_at = inflated.find(b"ARM\x64")

        if tail[:4] != b"\xd0\x0d\xfe\xed":
            raise SystemExit("%s: no device tree after the gzip stream" % path)
        self.tail = tail
        self.fdt = tail[:be32(tail, 4)]
        # Anything past the tree's own totalsize is padding, and the declared
        # ramdisk lives inside that slack. Keep it verbatim.
        self.tail_slack = tail[be32(tail, 4):]

        expect = len(self.gz) + be32(self.fdt, 4)
        if expect != self.kernel_size:
            print("  note: kernel_size is %d but gz+totalsize is %d; "
                  "keeping the original value's convention"
                  % (self.kernel_size, expect))

    def rebuild(self, fdt):
        """Assemble a full image carrying the given device tree."""
        header = bytearray(self.orig[:self.gz_start])
        kernel_size = len(self.gz) + be32(fdt, 4)
        struct.pack_into("<I", header, 8, kernel_size)

        body = bytes(header) + self.gz + fdt + self.tail_slack
        # Pad the kernel region up to a page boundary, as the originals are.
        if len(body) % self.page:
            body += b"\x00" * (self.page - len(body) % self.page)
        return bytes(body)

    def id_field(self):
        """The 32-byte SHA1 of the kernel, if this image carries one."""
        value = self.orig[576:608]
        return value if any(value) else None

    def describe(self, label):
        print("%s" % label)
        print("  file          : %d bytes" % len(self.orig))
        print("  page_size     : %d" % self.page)
        print("  kernel_size   : %d" % self.kernel_size)
        print("  gzip stream   : %d bytes" % len(self.gz))
        print("  inflated      : %d bytes, ARM64 magic at 0x%x"
              % (self.inflated_size, self.magic_at))
        print("  fdt           : totalsize %d, raw %d, slack %d"
              % (be32(self.fdt, 4), len(self.fdt), len(self.tail_slack)))
        sha = self.id_field()
        print("  kernel sha1   : %s"
              % (sha.hex() if sha else "not present (left as is)"))


def main():
    src, new_dtb_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    layout = Layout(src)
    layout.describe("=== source ===")

    print()
    print("=== round-trip check ===")
    rebuilt = layout.rebuild(layout.fdt)
    if rebuilt == layout.orig:
        print("  rebuild with the original tree is byte-identical: "
              "the layout model is correct")
    else:
        print("  MISMATCH: rebuilt %d bytes vs original %d bytes"
              % (len(rebuilt), len(layout.orig)))
        for i in range(min(len(rebuilt), len(layout.orig))):
            if rebuilt[i] != layout.orig[i]:
                print("  first difference at 0x%x" % i)
                break
        raise SystemExit("refusing to write a possibly corrupt image")

    new_dtb = open(new_dtb_path, "rb").read()
    if new_dtb[:4] != b"\xd0\x0d\xfe\xed":
        raise SystemExit("%s: not a flattened device tree" % new_dtb_path)
    print()
    print("=== new device tree ===")
    print("  source        : %s" % new_dtb_path)
    print("  totalsize     : %d (was %d)" % (be32(new_dtb, 4), be32(layout.fdt, 4)))

    out = layout.rebuild(new_dtb)
    parent = os.path.dirname(os.path.abspath(out_path))
    os.makedirs(parent, exist_ok=True)
    open(out_path, "wb").write(out)
    print()
    print("=== written ===")
    print("  path          : %s" % out_path)
    print("  size          : %d bytes" % len(out))

    # Re-read what was just written and confirm the contract still holds.
    check = Layout(out_path)
    check.describe("  verification")
    if check.magic_at != 0x38:
        raise SystemExit("written image breaks the boot contract")
    print()
    print("  OK: ARM64 magic still at 0x38, device tree readable")


if __name__ == "__main__":
    main()