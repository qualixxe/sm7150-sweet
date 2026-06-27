# edk2-sweet — Windows on ARM for the Xiaomi Redmi Note 10 Pro (sweet)

UEFI (EDK2 / Project Mu) firmware that boots **Windows 11 ARM64** on the **Xiaomi Redmi Note 10 Pro** — codename **sweet**, Snapdragon 732G (**SM7150**).

Forked from [edk2-porting/edk2-sm7150](https://github.com/edk2-porting/edk2-sm7150) and built on top of the Renegade / Project Aloha / dopaemon Windows-on-ARM ecosystem.

> **It boots to a usable Windows 11 ARM64 desktop.** Storage, display and buttons work. Several subsystems (touch, GPU acceleration, USB-OTG, modem, etc.) are still works in progress — see [Status](#status) below.

---

## ⚠️ DISCLAIMER — READ BEFORE FLASHING ⚠️

**This is experimental, hobbyist, work-in-progress firmware. Flashing it can permanently brick your device.**

- This software is provided **"AS IS", with NO WARRANTY of any kind**, express or implied.
- **There is a real risk of bricking, data loss, or hardware damage.** You can lose your data, your bootloader, or the entire phone.
- **The author takes NO responsibility and accepts NO liability** for any damage, loss, or consequence resulting from using, flashing, or modifying anything in this repository.
- This is **not** an official product and is **not** affiliated with Xiaomi, Qualcomm, or Microsoft.
- By using anything here, **you accept full responsibility for whatever happens to your device. You have been warned.**
- Only use a device you are willing to lose, keep a **full backup**, and make sure you know how to restore stock firmware via **EDL / 9008** before you start.

If you are not comfortable with bootloaders, fastboot, EDL recovery, and the real possibility of a hard brick — **stop here.**

---

## What is this?

An AArch64 UEFI implementation (Project Mu based) that lets the SM7150-based Redmi Note 10 Pro (sweet) boot Windows 11 ARM64. UEFI comes up in place of the Android boot chain, exposes ACPI + a framebuffer + storage, and hands off to the Windows ARM64 boot manager.

## Status

> Honest, current state. "Works" = usable. "WIP" = it boots / enumerates but is not functional yet.

### Works
- **Boots to the Windows 11 ARM64 desktop** (usable with the touchscreen disabled).
- **Internal storage** — UFS.
- **Display** — framebuffer (no GPU acceleration; see below).
- **Buttons** — power / volume / side keys.
- **ACPI** (DSDT).
- **Watchdog** handling.
- **Touch power rail (AVDD)** — brought up from UEFI via a raw RPMh vote (the touch IC ACKs on I2C 0x5D).

### Work in progress / not functional
- **Touchscreen (Goodix gt9896, I2C 0x5D)** — disabled by default. The ACPI device and the `qci2c` driver enumerate, but the I2C transaction to the QUP **deadlocks a core → `CLOCK_WATCHDOG_TIMEOUT (0x101)`**. IRQ type, I2C clock and GPIOs are all stock-matched; the deadlock lives in the qci2c ↔ QUP / clock-NoC path and needs a serial / WinDbg session to pinpoint. Re-enable by removing the `_STA` override on the `TSC1` device in the DSDT.
- **GPU acceleration** — no Adreno DXGK driver loads, so Windows renders in **software** via the framebuffer (slow UI, no hardware acceleration).
- **USB-OTG (host mode)** — the role-switch (`UrsSynopsys`) binds, but VBUS comes from the pm6150 SMB5 charger, which needs the PMIC stack (not driven) — so no OTG power. USB **device** mode (fastboot) works.
- **Battery / power management** — intermittent `INTERNAL_POWER_ERROR (0xA0)`; the battery stack is non-functional.
- **Not brought up:** modem / cellular, Wi-Fi / Bluetooth, camera, GPS, sensors, audio.

## Build

Requires a Linux environment (the build does **not** run on Windows or Android).

```bash
./sweetbuild.sh
```

This runs `iasl` on the DSDT and builds the UEFI, producing `sweet_uefi.img`.

## Flash

> Bootloader must be unlocked. **Brick risk — read the disclaimer first.**

```bash
fastboot flash boot sweet_uefi.img
```

Refer to the Renegade / WoA community guides for the exact partition layout and the full install flow (including deploying the Windows ARM64 image to userdata).

## Drivers (Windows BSP)

Windows-side hardware support comes from a separate board support package (Qualcomm / moorea-derived drivers), injected into the Windows image offline via DISM. The drivers live in a companion repository so this repo stays focused on the UEFI:

**→ [sm7150-sweet-drivers](https://github.com/Cristiano1103/sm7150-sweet-drivers)** — curated sweet driver pack + injection scripts.

## Documentation

In-depth reverse-engineering notes and the debugging journey live in [`docs/`](docs/):
- [`docs/TOUCH-SAGA.md`](docs/TOUCH-SAGA.md)
- [`docs/sweet-woa-saga.html`](docs/sweet-woa-saga.html)
- [`docs/sweet-commands.html`](docs/sweet-commands.html)

## Acknowledgements

- [edk2-porting / edk2-sm7150](https://github.com/edk2-porting/edk2-sm7150) (Renegade Project) — the SM7150 UEFI base.
- [Project Aloha](https://github.com/Project-Aloha) and [Project Silicium](https://github.com/Project-Silicium) — the Windows-on-Snapdragon ecosystem.
- Gustave Monce and [mu_andromeda_platforms](https://github.com/WOA-Project/mu_andromeda_platforms) / the [WOA-Project](https://github.com/WOA-Project).
- [Project Mu](https://microsoft.github.io/mu/).
- Everyone in the WoA / dopaemon community who documented the SM7150.

## License

The UEFI code inherits the licenses of its upstream EDK2 / Project Mu sources. Driver binaries are the property of their respective owners and are redistributed for interoperability and research purposes only. See [LICENSE](LICENSE) if present.
