# ============================================================================
# >>> STRIP-DOWN (pristine do user) + _AEI do GIO0 desativado no DSDT <<<
# Backend Qualcomm TODO comentado [1]..[11]. ATIVOS: base Smem/HWIO/Clock/Pmic/
# SPMI/UFS. Teste atual = isolar o _AEI (GIO0 self-GPE -> 0x239) com tudo o resto
# como estava no strip-down que voce ja conhece. Se ainda travar, ativar 1 por 1.
# ============================================================================

# --- SmemDxe (ATIVO) ---
# sm7325, not sm7150. Its one depex protocol is already present in the image,
# so it dispatches. sm7150/SmemDxe had an empty depex and dispatched against
# register offsets belonging to a different SoC.
  FILE DRIVER = f541d663-4a48-40aa-aabf-ff158ccae34c {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/SmemDxe/SmemDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/SmemDxe/SmemDxe.efi
    SECTION UI = "SmemDxe"
  }

# --- HWIODxeDriver (ATIVO) ---
  FILE DRIVER = af9763a2-033b-4109-8e17-56a98d380c92 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/HWIODxeDriver/HWIODxeDriver.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/HWIODxeDriver/HWIODxeDriver.efi
    SECTION UI = "HWIODxeDriver"
  }

# --- [1] DALSys  [COMENTADO - isolando o reboot: base+fix<4GB boota?] ---
#  FILE DRIVER = 8E9BD160-B184-11DF-94E2-0800200C9A66 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/DALSys/DALSys.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/DALSys/DALSys.efi
#    SECTION UI = "DALSys"
#  }

# --- ClockDxe (ATIVO, base) ---
# sm7325, not sm7150.
#
# This is the one that most likely matters for storage. ClockDxe is what turns on
# the UFS core, bus aggregate and interface clocks, and the sm7150 build programs
# offsets belonging to a different SoC - so it ran, and turned on the wrong bits.
#
# sm7325/ClockDxe has three depex protocols instead of two, and all three are
# already present in the image, so it dispatches.
  FILE DRIVER = 4DB5DEA6-5302-4D1A-8A82-677A683B0D29 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/ClockDxe/ClockDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/ClockDxe/ClockDxe.efi
    SECTION UI = "ClockDxe"
  }

# --- PmicDxe (ATIVO, base) ---
  # Left on sm7150 deliberately.
  #
  # sm7325/PmicDxe is the SoC-matched build and looks like the right choice, but
  # it needs two protocols that nothing can supply:
  #
  #   F0F602A3-13A3-264A-F03E-F2E0DEC51234
  #   6B02342F-5F30-7DFA-F4C4-4AA47D882F82
  #
  # Both were searched across all 33 sm7325 drivers in the binary repository, in
  # both GUID byte orders, and neither appears in any of them. So switching would
  # trade a driver that dispatches for one that never does. The provider is not
  # in this driver set, so sm7150/PmicDxe stays until it is.
  FILE DRIVER = 5776232e-082d-4b75-9a0e-fe1d13f7a5d9 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/PmicDxe/PmicDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/PmicDxe/PmicDxe.efi
    SECTION UI = "PmicDxe"
  }

# --- SPMI (ATIVO, base) ---
# sm7325, not sm7150. One depex protocol, already present. SPMI is the bus the
# PMIC sits on, so getting it onto the right build matters for anything that
# touches power rails.
  FILE DRIVER = 2a7b4bef-80cd-49e1-b473-374ba4d673fc {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/SPMI/SPMI.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/SPMI/SPMI.efi
    SECTION UI = "SPMI"
  }

# --- UFSDxe (ATIVO, base) ---
  # sm7325, not sm7150. The driver repository ships one UFSDxe per SoC and they
  # differ substantially:
  #
  #   sm7150/UFSDxe.efi    761 856 bytes   depex 1 protocol
  #   sm7325/UFSDxe.efi    126 976 bytes   depex 2 protocols
  #   sm8150/UFSDxe.efi    126 976 bytes
  #
  # sm7150 is a six-times-larger build with a third of the dependencies, written
  # against a different memory layout. With it loaded the driver does not come
  # up: the File Manager then lists only LOGFS, bluetooth and VenHw, no
  # userdata, so nothing can be written to the phone and Windows cannot be
  # installed.
  #
  # sm7325 is the variant for this SoC. Its two extra depex protocols,
  # 02F6F0A3-134A-26F0-3EF2-E0DEC512342F and 3402D0C7-E5F4-39D2-CB47-AACD7F66EF76,
  # were verified present in the built firmware (30 and 1 references).
  FILE DRIVER = 0d35cd8e-97ea-4f9a-96af-0f0d89f76567 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/UFSDxe/UFSDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/UFSDxe/UFSDxe.efi
    SECTION UI = "UFSDxe"
  }

# --- Модули, которых нет в нашей сборке, но есть в рабочей -------------
#   Пересобраны: тот же набор, что грузится на courbet - 157 модулей против
#   наших 57. Из них добавлены только те, чей DEPEX текущая сборка уже
#   удовлетворяет, чтобы ничего не сломать:
#
#     DDRInfoDxe       1 зависимость, есть
#     HALIOMMU        13 зависимостей, все есть
#     NpaDxe           1 зависимость, есть
#     ShmBridgeDxeLA   без DEPEX
#
#   Не добавлены, потому что заблокированы отсутствующими протоколами:
#     ChipInfo, DALTLMM, PlatformInfoDxeDriver  - нужен F0F602A3
#     DALSys                                    - нужен F0F60238
#
#   FFS GUID взяты из рабочей прошивки, а не выдуманы.

  # --- DDRInfoDxe ---
  FILE DRIVER = 7db0793a-4402-4be1-906e-d0fabad2707e {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/DDRInfoDxe/DDRInfoDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/DDRInfoDxe/DDRInfoDxe.efi
    SECTION UI = "DDRInfoDxe"
  }

  # --- HALIOMMU ---
  FILE DRIVER = 9a00771f-36d4-4dd5-8916-c48ed9b16b86 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/HALIOMMU/HALIOMMU.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/HALIOMMU/HALIOMMU.efi
    SECTION UI = "HALIOMMU"
  }

  # --- NpaDxe : управление питанием, ближе всего к цели ---
  FILE DRIVER = cb29f4d1-7f37-4692-a416-93e82e219711 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/NpaDxe/NpaDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/NpaDxe/NpaDxe.efi
    SECTION UI = "NpaDxe"
  }

  # --- ShmBridgeDxeLA ---
  FILE DRIVER = 9c1eb71f-dd6c-4ed5-9f6a-5cc0ca789f16 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7325/ShmBridgeDxeLA/ShmBridgeDxeLA.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7325/ShmBridgeDxeLA/ShmBridgeDxeLA.efi
    SECTION UI = "ShmBridgeDxe"
  }

# ===== BACKEND COMENTADO - descomentar UM por vez =====

# --- [2] ChipInfo ---
#  FILE DRIVER = 10e193df-9966-44e7-b17c-59dd831e20fc {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/ChipInfo/ChipInfo.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/ChipInfo/ChipInfo.efi
#    SECTION UI = "ChipInfo"
#  }

# --- [3] PlatformInfoDxeDriver ---
#  FILE DRIVER = B105211B-BBBD-4ADD-A3B0-D1CF4A52154C {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/PlatformInfoDxeDriver/PlatformInfoDxeDriver.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/PlatformInfoDxeDriver/PlatformInfoDxeDriver.efi
#    SECTION UI = "PlatformInfoDxeDriver"
#  }

# --- [4] ULogDxe ---
#  FILE DRIVER = E43128A8-8692-42B6-8AFA-676158578D18 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/ULogDxe/ULogDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/ULogDxe/ULogDxe.efi
#    SECTION UI = "ULogDxe"
#  }

# --- [5] CmdDbDxe ---
#  FILE DRIVER = ABA01FF8-2CCB-4E12-8B2E-CD3F4A742993 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/CmdDbDxe/CmdDbDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/CmdDbDxe/CmdDbDxe.efi
#    SECTION UI = "CmdDbDxe"
#  }

# --- [6] NpaDxe ---
#  FILE DRIVER = CB29F4D1-7F37-4692-A416-93E82E219711 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/NpaDxe/NpaDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/NpaDxe/NpaDxe.efi
#    SECTION UI = "NpaDxe"
#  }

# --- [7] RpmhDxe ---
#  FILE DRIVER = CB29F4D1-7F37-4692-A416-93E82E219766 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/RpmhDxe/RpmhDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/RpmhDxe/RpmhDxe.efi
#    SECTION UI = "RpmhDxe"
#  }

# --- [8] PdcDxe ---
#  FILE DRIVER = B43C22DB-6333-490C-872D-0A73439059FD {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/PdcDxe/PdcDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/PdcDxe/PdcDxe.efi
#    SECTION UI = "PdcDxe"
#  }

# --- [9] ShmBridgeDxe ---
#  FILE DRIVER = 9c1eb71f-dd6c-4ed5-9f6a-5cc0ca789f16 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/ShmBridgeDxe/ShmBridgeDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/ShmBridgeDxe/ShmBridgeDxe.efi
#    SECTION UI = "ShmBridgeDxe"
#  }

# --- [10] ScmDxe ---
#  FILE DRIVER = 2d7a83e3-e343-4ffb-9109-631f2ee11694 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/ScmDxe/ScmDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/ScmDxe/ScmDxe.efi
#    SECTION UI = "ScmDxe"
#  }

# --- [11] TzDxe ---
#  FILE DRIVER = 6925fad3-e243-4b69-ad61-3f978c8abc60 {
#    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/TzDxe/TzDxe.depex
#    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/TzDxe/TzDxe.efi
#    SECTION UI = "TzDxe"
#  }

# NOTA: ButtonsDxe do sweet ja vem pelo sweet.inc; nao duplicar aqui.
