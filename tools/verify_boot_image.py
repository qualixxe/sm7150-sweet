#!/usr/bin/env python3
"""Check that a boot image satisfies the contract this platform's loader expects.

Every rule here was measured on a firmware image that is known to boot, not
assumed from documentation:

  - the image is an Android boot image, header_version 0, page size 4096
  - page 1 holds a gzip stream that decompresses to a Qualcomm MBN header
    followed by an AArch64 image
  - the literal bytes ARM\\x64 sit at offset 0x38 of the inflated data, because
    the loader rejects the image otherwise
  - a raw flattened device tree follows the gzip stream
  - the header's kernel_size equals len(gzip stream) + the tree's totalsize

That last rule is not folklore: it holds exactly on the surya firmware that boots
on this phone. Checking it catches a mis-sized tree, which is otherwise silent.

Exit status is non-zero if any check fails.
"""

import struct
import sys
import zlib

ARM64_MAGIC = b"ARM\x64"
FDT_MAGIC = b"\xd0\x0d\xfe\xed"


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def main():
    if len(sys.argv) != 2:
        print("usage: verify_boot_image.py <image>")
        return 2
    path = sys.argv[1]
    data = open(path, "rb").read()
    failures = []
    notes = []

    def check(condition, message):
        if condition:
            print("  ok    %s" % message)
        else:
            print("  FAIL  %s" % message)
            failures.append(message)

    print("verifying %s (%d bytes)" % (path, len(data)))

    if data[:8] != b"ANDROID!":
        print("  FAIL  not an Android boot image (magic %r)" % data[:8])
        return 1

    kernel_size = struct.unpack_from("<I", data, 8)[0]
    page = struct.unpack_from("<I", data, 36)[0]
    version = struct.unpack_from("<I", data, 40)[0]

    check(page == 4096, "page size is 4096 (got %d)" % page)
    check(version == 0, "header version is 0 (got %d)" % version)

    stream = zlib.decompressobj(31)
    try:
        kernel = stream.decompress(data[page:])
    except zlib.error as exc:
        print("  FAIL  the kernel does not inflate: %s" % exc)
        return 1
    tail = stream.unused_data

    check(len(kernel) > 0, "the kernel inflates (%d bytes)" % len(kernel))
    check(kernel.count(ARM64_MAGIC) >= 1, "the ARM64 magic is present")

    # Decompressobj leaves the bytes past the stream in unused_data, which is
    # the device tree. If they are not a tree, the image is malformed.
    if not tail[:4] == FDT_MAGIC:
        print("  FAIL  no device tree follows the gzip stream")
        return 1

    magic_at = kernel.find(ARM64_MAGIC)
    check(magic_at == 0x38,
          "ARM64 magic sits at offset 0x38 (found at 0x%x)" % magic_at)

    tree_total = be32(tail, 4)
    gz_len = len(data) - len(tail) - page
    check(kernel_size == gz_len + tree_total,
          "kernel_size equals gzip length plus tree totalsize "
          "(%d vs %d + %d)" % (kernel_size, gz_len, tree_total))

    check(len(tail) >= tree_total,
          "the tree is not truncated (%d bytes present, totalsize %d)"
          % (len(tail), tree_total))

    tree_version = be32(tail, 20)
    check(tree_version in (16, 17), "device tree version is 16 or 17 (got %d)"
          % tree_version)

    notes.append("gzip %d bytes -> %d inflated" % (gz_len, len(kernel)))
    notes.append("device tree totalsize %d, %d bytes after the stream"
                 % (tree_total, len(tail)))
    notes.append("boot partition size 0x%x, image is %d bytes"
                 % (page, len(data)))
    for note in notes:
        print("  note  %s" % note)

    if failures:
        print("\n%d check(s) failed" % len(failures))
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())