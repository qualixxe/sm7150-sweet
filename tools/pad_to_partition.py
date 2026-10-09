#!/usr/bin/env python3
"""Pad a boot image out to a partition size, in place.

Only needed for `fastboot flash boot`. `fastboot boot` transfers the image to
RAM and writes nothing, so the unpadded image is the right one for testing and
the padded one is only for the eventual flash.
"""

import sys


def main():
    if len(sys.argv) != 3:
        print("usage: pad_to_partition.py <image> <size-in-bytes>")
        return 1
    path, size = sys.argv[1], int(sys.argv[2])
    data = open(path, "rb").read()
    if len(data) > size:
        print("%s is %d bytes, already larger than %d; leaving it alone"
              % (path, len(data), size))
        return 0
    padded = data + b"\x00" * (size - len(data))
    open(path, "wb").write(padded)
    print("  padded %s: %d -> %d bytes" % (path, len(data), len(padded)))
    return 0


if __name__ == "__main__":
    sys.exit(main())