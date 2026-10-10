#!/usr/bin/env python3
"""Build a gzipped newc cpio archive, i.e. a Linux initramfs, from a directory.

Written here rather than shelling out to cpio because the machine this runs on is
Windows, and because the archive is small enough that the format is simpler to
implement than the cross-platform plumbing would be.

The format is old and fully specified: each record is a 110-byte ASCII header of
a magic and thirteen 8-digit hex fields, then the NUL-terminated name padded to a
4-byte boundary, then the file data padded the same way. The archive ends with a
record named TRAILER!!! whose size fields are zero.

    magic      6   "070701"
    ino        8
    mode       8   file type and permissions
    uid        8
    gid        8
    nlink      8   1 for regular files
    mtime      8
    filesize   8
    devmajor   8
    devminor   8
    rdevmajor  8
    rdevminor  8
    namesize   8   includes the NUL
    check      8   always 0 in practice

Note the padding is relative to the start of the record, not the end of the
previous field, which is the part that is easy to get wrong.
"""

import argparse
import gzip
import os
import stat
import struct
import sys

MAGIC = b"070701"
TRAILER = "TRAILER!!!"


def _align4(n):
    return (4 - (n % 4)) % 4


def _header(ino, mode, uid, gid, nlink, mtime, filesize, namesize,
            devmajor=0, devminor=0, rdevmajor=0, rdevminor=0):
    # All thirteen fields, in order, including check last. Leaving one out makes
    # the header 8 bytes short and every name after it is misread.
    fields = (ino, mode, uid, gid, nlink, mtime, filesize,
              devmajor, devminor, rdevmajor, rdevminor, namesize)
    out = MAGIC
    for value in fields:
        out += b"%08X" % (value & 0xFFFFFFFF)
    out += b"%08X" % 0          # check, never used
    assert len(out) == 110, len(out)
    return out


def _emit(name, mode, data, ino, mtime=0):
    raw = name.encode("utf-8") + b"\x00"
    rec = _header(ino, mode, 0, 0, 1, mtime, len(data), len(raw))
    rec += raw
    rec += b"\x00" * _align4(len(rec))
    rec += data
    rec += b"\x00" * _align4(len(rec))
    return rec


def build(source_dir, gzip_level=9):
    entries = []
    for dirpath, dirnames, filenames in os.walk(source_dir):
        dirnames.sort()
        filenames.sort()
        for d in dirnames:
            full = os.path.join(dirpath, d)
            rel = os.path.relpath(full, source_dir).replace(os.sep, "/")
            entries.append((rel + "/", stat.S_IFDIR | 0o755, b""))
        for f in filenames:
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, source_dir).replace(os.sep, "/")
            if os.path.islink(full):
                # A symlink in newc carries its target as the data and its type
                # in the mode. Writing it as a regular file would produce a
                # /bin/sh containing the text "busybox", which the kernel cannot
                # execute at all.
                entries.append((rel, stat.S_IFLNK | 0o777,
                                os.readlink(full).encode("utf-8")))
                continue
            with open(full, "rb") as fh:
                entries.append((rel, stat.S_IFREG | 0o755, fh.read()))

    # Directories must exist before their contents, and /init must exist at all.
    entries.sort(key=lambda e: (e[0].count("/"), e[0]))

    out = bytearray()
    ino = 1
    for name, mode, data in entries:
        out += _emit(name, mode, data, ino)
        ino += 1
    out += _emit(TRAILER, 0, b"", 0)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="directory to pack")
    ap.add_argument("-o", "--out", required=True, help="output .cpio.gz")
    args = ap.parse_args()

    if not os.path.isdir(args.source):
        raise SystemExit("not a directory: %s" % args.source)

    raw = build(args.source)
    comp = gzip.compress(raw, compresslevel=9)
    # gzip.compress embeds an mtime; zero it so the output is reproducible.
    comp = comp[:4] + b"\x00\x00\x00\x00" + comp[8:]

    with open(args.out, "wb") as f:
        f.write(comp)

    print("packed %s -> %s" % (args.source, args.out))
    print("  %d entries, %d bytes raw, %d bytes gzipped"
          % (raw.count(MAGIC) - 1, len(raw), len(comp)))
    return 0


if __name__ == "__main__":
    sys.exit(main())