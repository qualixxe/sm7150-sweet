
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