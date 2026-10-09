# courbet notes

This fork builds the sweet (Redmi Note 10 Pro) Windows-on-ARM UEFI and packages
it for **courbet**, the Xiaomi Mi 11 Lite 4G.

## Why that should work at all

Both boards are the same SoC:

| | sweet | courbet |
|---|---|---|
| device | Redmi Note 10 Pro | Mi 11 Lite 4G |
| SoC | SM7325, Snapdragon 732G | SM7325, Snapdragon 732G |

Same silicon, so the firmware carries over; what differs between the boards is
the device tree. That is the same reasoning that made the surya UEFI boot on
courbet.

This is the important part, and it is not true of the surya firmware this
repository's parent project used: **the sweet firmware drives internal UFS
storage.** That was the blocker for reaching Windows at all, because without a
storage driver the firmware cannot read any installation medium.

## What was changed here

Upstream, unchanged, builds one image:

```bash
./sweetbuild.sh          # -> sweet_uefi.img
```

Here, the same firmware is packaged three times with three device trees, so the
right one can be chosen by testing rather than by guessing. `fastboot boot`
writes nothing, so every variant is safe to try.

| variant | device tree | rationale |
|---|---|---|
| `courbet-with-sweet-dtb` | `sweet.dtb` | baseline, exactly what upstream builds |
| `courbet-with-courbet-dtb` | `courbet.dtb` | courbet's own Android tree, the one the working surya image used |
| `courbet-with-7150-dtb` | `7150.dtb` | the SoC-family tree in this repository |

```bash
fastboot boot courbet-with-courbet-dtb.img
```

## Tools

- `tools/swap_dtb.py` — replaces the tree appended to a boot image. It refuses
  to write unless it can first rebuild the input byte for byte, so a packaging
  error cannot quietly produce an unbootable image.
- `tools/pad_to_partition.py` — grows an image to 128 MiB for a future
  `fastboot flash boot`. Not needed for `fastboot boot`, which transfers to RAM.
- `tools/verify_boot_image.py` — checks the contract the loader enforces:
  Android boot header, page size 4096, an inflating gzip stream, the `ARM\x64`
  magic at offset 0x38, a flattened device tree after the stream, and
  `kernel_size == len(gzip) + tree totalsize`.

## Known limits of the sweet firmware

From upstream's own status report, on sweet:

- boots to a usable Windows 11 ARM64 desktop
- internal storage (UFS), display, ACPI and buttons work
- **no GPU acceleration** — Windows renders through the framebuffer
- touchscreen disabled; the I2C path deadlocks a core
- no Wi-Fi, no audio, no modem
- the DSDT in `F11/AcpiTables` is sweet's, not courbet's

Those are expected to carry over, and the ACPI one is the most likely to matter
here.

## A fixed upstream workflow

The workflow inherited here triggered on `push` to `master` while the branch is
`main`, so it never ran. It also did not check out submodules, which the build
needs. Both are corrected in `.github/workflows/build.yml`.