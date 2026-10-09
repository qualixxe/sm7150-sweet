#!/bin/bash
# Build bootable images carrying different device trees.
#
# sweetbuild.sh produces sweet_uefi.img, a boot image whose kernel is a gzip
# stream followed by a raw device tree. That layout is what the vendor
# bootloader expects on this platform, so swapping the tree is enough to retarget
# the firmware at another board of the same SoC family.
#
# sweet is a Redmi Note 10 Pro (SM7325). courbet is a Mi 11 Lite 4G, also
# SM7325, so the firmware is expected to carry over; the board differences live
# in the device tree.
#
# tools/swap_dtb.py rebuilds the image and refuses to write unless it can first
# reproduce the input byte for byte, so a packaging mistake cannot silently
# produce something unbootable.

set -euo pipefail

BASE="sweet_uefi.img"
PARTITION_SIZE=$((128 * 1024 * 1024))

if [ ! -f "$BASE" ]; then
    echo "error: $BASE not found; run sweetbuild.sh first" >&2
    exit 1
fi

mkdir -p out
echo "=== source image ==="
ls -l "$BASE"

build_variant() {
    local dtb="$1" name="$2"
    if [ ! -f "$dtb" ]; then
        echo "skipping $name: $dtb is missing"
        return
    fi
    echo
    echo "=== $name  (tree: $dtb, $(stat -c%s "$dtb") bytes) ==="
    python3 tools/swap_dtb.py "$BASE" "$dtb" "out/$name.img"
    # The partition-sized copy is for a later `fastboot flash boot`.
    # `fastboot boot` needs no padding: it transfers to RAM and writes nothing,
    # so that is the one to test with.
    cp "out/$name.img" "out/$name-128m.img"
    python3 tools/pad_to_partition.py "out/$name-128m.img" "$PARTITION_SIZE"
}

build_variant sweet.dtb  courbet-with-sweet-dtb
build_variant courbet.dtb courbet-with-courbet-dtb
build_variant 7150.dtb   courbet-with-7150-dtb

echo
echo "=== variants ==="
ls -l out/