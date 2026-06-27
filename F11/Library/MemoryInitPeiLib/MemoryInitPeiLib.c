/** @file
*
*  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
*  Copyright (c) 2019, RUIKAI LIU and MR TUNNEL. All rights reserved.
*
*  This program and the accompanying materials
*  are licensed and made available under the terms and conditions of the BSD License
*  which accompanies this distribution.  The full text of the license may be found at
*  http://opensource.org/licenses/bsd-license.php
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/
#include <PiPei.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

// APSS Watchdog (APSS_WDT_TMR1 @ 0x17C10000)
// Existem DOIS layouts no kernel Linux (qcom-wdt.c). Cobrimos OS DOIS:
// KPSS:     RST=0x4,  EN=0x8,  STS=0xC,  BARK=0x10, BITE=0x14
// APCS_TMR: RST=0x38, EN=0x40, STS=0x44, BARK=0x4C, BITE=0x5C
#define APSS_WDT_BASE             0x17C10000
// layout KPSS
#define WDT_KPSS_RST         (APSS_WDT_BASE + 0x04)
#define WDT_KPSS_EN          (APSS_WDT_BASE + 0x08)
#define WDT_KPSS_BARK        (APSS_WDT_BASE + 0x10)
#define WDT_KPSS_BITE        (APSS_WDT_BASE + 0x14)
// layout APCS_TMR
#define WDT_APCS_RST         (APSS_WDT_BASE + 0x38)
#define WDT_APCS_EN          (APSS_WDT_BASE + 0x40)
#define WDT_APCS_BARK        (APSS_WDT_BASE + 0x4C)
#define WDT_APCS_BITE        (APSS_WDT_BASE + 0x5C)

extern UINT64 mSystemMemoryEnd;
VOID
BuildMemoryTypeInformationHob (
  VOID
);
STATIC
VOID
InitMmu (
  IN ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable
)
{
  VOID                          *TranslationTableBase;
  UINTN                         TranslationTableSize;
  RETURN_STATUS                 Status;
  //Note: Because we called PeiServicesInstallPeiMemory() before to call InitMmu() the MMU Page Table resides in
  //      DRAM (even at the top of DRAM as it is the first permanent memory allocation)
  Status = ArmConfigureMmu (MemoryTable, &TranslationTableBase, &TranslationTableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Error: Failed to enable MMU\n"));
  }
}
EFI_STATUS
EFIAPI
MemoryPeim (
  IN EFI_PHYSICAL_ADDRESS               UefiMemoryBase,
  IN UINT64                             UefiMemorySize
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable;

  // Get Virtual Memory Map from the Platform Library
  ArmPlatformGetVirtualMemoryMap (&MemoryTable);
  // Ensure PcdSystemMemorySize has been set
  ASSERT (PcdGet64 (PcdSystemMemorySize) != 0);
  InitMmu (MemoryTable);

  //
  // DESABILITAR o watchdog APSS (KPSS, base 0x17C10000) - estrategia agressiva.
  // Endereco e layout confirmados pelo DTB do u-boot (qcom,kpss-wdt).
  // Truque: zerar BITE_TIME = 0 faz o watchdog NUNCA resetar, mesmo ligado.
  //
  MmioWrite32 (WDT_KPSS_EN, 0x00000000);     // 1. desabilita EN
  MmioWrite32 (WDT_KPSS_BITE, 0x00000000);   // 2. BITE_TIME=0 (nunca morde!)
  MmioWrite32 (WDT_KPSS_BARK, 0x00000000);   // 3. BARK_TIME=0
  MmioWrite32 (WDT_KPSS_RST, 0x00000001);    // 4. kick RST
  MmioWrite32 (WDT_KPSS_EN, 0x00000000);     // 5. desabilita EN de novo

  //
  // Confirmacao (sem travar): le WDT_EN de volta. Comprovado em teste visual
  // (tela verde) que a escrita PEGA e EN fica 0 -> watchdog desabilitado.
  // Mantemos so o DEBUG para log; o aparelho segue o boot normalmente.
  //
  {
    UINT32 EnVal = MmioRead32 (WDT_KPSS_EN);
    DEBUG ((EFI_D_ERROR, "WDT: EN apos disable = 0x%08x (esperado 0)\n", EnVal));
  }
  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)){
        // Optional feature that helps prevent EFI memory map fragmentation.
        BuildMemoryTypeInformationHob();
  }
  return EFI_SUCCESS;
}
