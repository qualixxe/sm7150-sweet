#!/usr/bin/env python3
"""Rebuild an Android boot.img for the Xiaomi Mi 11 Lite 4G (courbet).

The point of this tool is the Windows installer problem, not the UEFI. The UEFI
firmware can only mount FAT32 and the phone's partitions are ext4 and f2fs, so
even a fully working UFS controller yields nothing readable. This instead boots
the phone's own kernel with a ramdisk we control, which gives a root shell on
the real block device - no root on Android, no EDL, no OTG adapter, and nothing
is written to flash because it goes through `fastboot boot`.

Layout of a boot.img, as measured on courbet-stock-android-boot.img:

    0x00000000  boot header, one page            4096 B
    0x00001000  kernel, gzip                     kernel_size B
    0x00f94000  ramdisk, gzip                    ramdisk_size B
    0x01059000  device tree                      dtb_size B
    0x08000000  end of the 128 MB file

The header's kernel_addr, ramdisk_addr and page_size are all left untouched, so
the kernel is loaded exactly where the stock bootloader expects it. Only the
ramdisk is swapped.

Verification is by round trip: rebuilding the stock image with its own ramdisk
must reproduce it byte for byte. That is checked by tools/verify_boot_img.py.
"""

import argparse
import hashlib
import struct
import sys

MAGIC = b"ANDROID!"
FDT_MAGIC = b"\xd0\x0d\xfe\xed"

# Offsets in the boot_img_hdr. BOOT_MAGIC_SIZE is 8, so kernel_size is at 8 and
# not at 4; this was measured, not assumed.
OFF_KERNEL_SIZE = 8
OFF_RAMDISK_SIZE = 16
OFF_PAGE_SIZE = 36
OFF_HEADER_VERSION = 40


class BootImage:
    def __init__(self, blob):
        self.blob = blob
        if blob[0:8] != MAGIC:
            raise ValueError("not an Android boot image: magic is %r" % blob[0:8])
        self.kernel_size = self._u32(OFF_KERNEL_SIZE)
        self.ramdisk_size = self._u32(OFF_RAMDISK_SIZE)
        self.page_size = self._u32(OFF_PAGE_SIZE)
        self.header_version = self._u32(OFF_HEADER_VERSION)
        self.header = blob[0:self.page_size]
        self.kernel = blob[self.page_size:self.page_size + self.kernel_size]
        ramdisk_off = self._align(self.page_size + self.kernel_size)
        self.ramdisk = blob[ramdisk_off:ramdisk_off + self.ramdisk_size]
        dtb_off = self._find_dtb()
        self.dtb_off = dtb_off
        totalsize = struct.unpack_from(">I", blob, dtb_off + 4)[0]
        self.dtb = blob[dtb_off:dtb_off + totalsize]
        self.file_size = len(blob)

    def _u32(self, off):
        return struct.unpack_from("<I", self.blob, off)[0]

    def _align(self, value):
        ps = self.page_size
        return ((value + ps - 1) // ps) * ps

    def _find_dtb(self):
        # Search from the end of the ramdisk onward, page aligned, so the tree
        # is not confused with anything inside the kernel stream.
        start = self._align(
            self.page_size + self.kernel_size + self.ramdisk_size)
        at = self.blob.find(FDT_MAGIC, start)
        if at < 0:
            raise ValueError("no device tree after the ramdisk")
        if at % self.page_size:
            raise ValueError("device tree at 0x%x is not page aligned" % at)
        return at

    def describe(self):
        return (
            "page size     %d\n"
            "header ver    %d\n"
            "kernel        %d B at 0x%08x\n"
            "ramdisk       %d B at 0x%08x\n"
            "device tree   %d B at 0x%08x\n"
            "file          %d B"
            % (
                self.page_size,
                self.header_version,
                len(self.kernel), self.page_size,
                len(self.ramdisk), self._align(self.page_size + self.kernel_size),
                len(self.dtb), self.dtb_off,
                self.file_size,
            )
        )

    def with_ramdisk(self, ramdisk):
        """Return a new image identical to this one but with a new ramdisk.

        The kernel, the device tree, the header addresses and the total file
        size all stay as they are, so only the ramdisk region changes.
        """
        ramdisk_off = self._align(self.page_size + self.kernel_size)
        dtb_off = self._align(ramdisk_off + len(ramdisk))

        header = bytearray(self.header)
        struct.pack_into("<I", header, OFF_RAMDISK_SIZE, len(ramdisk))

        out = bytearray()
        out += header
        out += self.kernel
        while len(out) % self.page_size:
            out += b"\x00"
        out += ramdisk
        while len(out) % self.page_size:
            out += b"\x00"
        out += self.dtb
        while len(out) < self.file_size:
            out += b"\x00"
        del out[self.file_size:]
        return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="stock Android boot.img for this phone")
    ap.add_argument("-r", "--ramdisk", help="replacement ramdisk (cpio.gz)")
    ap.add_argument("-o", "--out", help="output image (default: stdout summary only)")
    ap.add_argument("--describe", action="store_true",
                    help="print the layout and exit")
    args = ap.parse_args()

    with open(args.source, "rb") as f:
        image = BootImage(f.read())

    print("source: %s" % args.source)
    print(image.describe())

    if args.describe:
        return 0

    if not args.ramdisk:
        print("\nno --ramdisk given, nothing written")
        return 0

    with open(args.ramdisk, "rb") as f:
        ramdisk = f.read()
    if ramdisk[0:2] != b"\x1f\x8b":
        raise SystemExit("ramdisk is not gzip: starts %r" % ramdisk[0:2])

    out = image.with_ramdisk(ramdisk)
    if not args.out:
        raise SystemExit("no --out given")

    with open(args.out, "wb") as f:
        f.write(out)

    print("\nramdisk: %s  %d B  sha256 %s"
          % (args.ramdisk, len(ramdisk), hashlib.sha256(ramdisk).hexdigest()[:16]))
    print("wrote:   %s  %d B" % (args.out, len(out)))
    print("kernel and device tree carried over unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())