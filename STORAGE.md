
## Storage: why userdata is missing, and what was ruled out

The File Manager lists only LOGFS, bluetooth and VenHw. No userdata, so there is
nowhere to put the Windows installer. Without a filesystem to write to, that
goal is blocked at the firmware level, not by the boot method or the device
tree.

UFS does not come out of its sleep state without power management being driven
first, so the chain has to be: PmicDxe -> RpmhDxe -> power rail -> UFS.

What was changed and verified:

- `UFSDxe` moved from `sm7150` to `sm7325`. The two are not variants of each
  other: sm7150 is 761 856 bytes with one depex protocol, sm7325 is 126 976 bytes
  with two. Verified inside the built image that a 126 976-byte PE appears and
  the 761 856-byte one is gone. The driver still does not dispatch, so this was
  necessary but not sufficient.
- `sm7325/PmicDxe` was evaluated and rejected. It needs two protocols that
  nothing supplies:

      F0F602A3-13A3-264A-F03E-F2E0DEC51234
      6B02342F-5F30-7DFA-F4C4-4AA47D882F82

  Both were searched across all 363 .efi binaries in dopaemon/edk2-msm-binary, in
  both GUID byte orders. Neither appears anywhere in the repository. Switching to
  it would trade a driver that dispatches for one that never does, so
  `sm7150/PmicDxe` stays.
- `PmicGlinkDxe` was suspected of supplying those two protocols and is not: its
  binary contains neither GUID.
- `RpmhDxe` and `DALSys` remain blocked for the same reason, matching the note
  already in `F11/inc/dxe.inc` that RpmhDxe's depex pulls in a DALSys that
  crashes. `NpaDxe`, `ShmBridgeDxeLA`, `PwrUtilsDxe`, `DDRInfoDxe`, `SPMI`,
  `ClockDxe`, `HALIOMMU` and `VcsDxe` were checked and their depex is satisfiable
  by the current build.

### What this means

The remaining blocker is a missing protocol provider, not a wrong file choice.
Going further needs either a driver that exports those GUIDs, or an SPMI/Glink
rail brought up without `RpmhDxe` - which is the same I2C path that hangs a core
on the touch controller. That is a real piece of firmware work, not a config
change.

Booting Linux from RAM via the Boot Linux app does not need storage and works
today. Windows does, so it stays blocked until the above is solved.
## Follow-up: the missing protocols are not power protocols

Picking the storage investigation further produced a correction to the earlier
conclusion.

The two protocols that block `sm7325/PmicDxe` and `RpmhDxe`:

    F0F602A3-13A3-264A-F03E-F2E0DEC51234
    6B02342F-5F30-7DFA-F4C4-4AA47D882F82

are absent from all 396 binaries in dopaemon/edk2-msm-binary, in both GUID byte
orders, and are not declared in qualcomm-linux/edk2-platforms either. So they
cannot be satisfied by swapping in a different file from that repository.

They are not, however, specific to this build. The depex survey across the
repository shows a family sharing the tail `-13A3-264A-F03E-F2E0DEC51234`:

    F0F60281  required by Devices/sweet and sm8150/WP_Binaries
    F0F6023B  required by ScmDxe, ButtonsDxe, TzDxe on sm7125/sm7325/sm8150/sm8350
    F0F602F1  required by UsbConfigDxe on sm7150/sm8150
    F0F602A3  required by 20 files, including PmicDxe, DALTLMM, HWIODxeDriver

These are per-subsystem protocols, not interchangeable, and nothing in the
repository exports any of them.

### The working firmware as the oracle

The surya image that boots on this phone was used as a reference, since it is
known to reach the point where its own drivers dispatch. It contains three
members of that family which our build does not:

    F0F60281  x3
    F0F6023B  x1
    F0F602F1  x1

The one at F0F6023B lives inside a single 18 180-byte FFS file,
`6442BCC0-BFDF-43FA-9564-7E8389AF7B5B`, which is `SecureBootProvisioningDxe` -
a Secure Boot module, not power management. Our build does not contain it.

That file exports nothing any driver in this ecosystem declares a dependency on,
so it is infrastructure the working firmware happens to carry, not the answer.

### What this rules out

`PmicDxe` is not present in the working firmware at all, under its FDF GUID
`5776232E-082D-4B75-9A0E-FE1D13F7A5D9`. The working firmware therefore does not
take the same path to storage that the sm7xxx driver set implies, and its
storage behaviour cannot be copied file for file. It reaches DXE and a GUI; it
has never been shown to expose userdata either.

So the earlier framing - "sm7325 is the right variant but two protocols are
missing" - is only half right. Those two protocols are unreachable from this
driver repository in the first place. Continuing down this route means finding
the DAL/Glink/RPMh stack from Qualcomm's own source and building it, which is a
much larger job than swapping binaries, and it is recorded here rather than
half-finished.