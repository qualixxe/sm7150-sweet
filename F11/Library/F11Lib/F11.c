/** @file
*
*  Copyright (c) 2018, Linaro Limited. All rights reserved.
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

#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>

#include <Ppi/ArmMpCoreInfo.h>

// APSS Watchdog (APSS_WDT_TMR1) base, do uefiplat7150.cfg / mem.h
// Offsets corretos do layout KPSS (driver oficial Linux/u-boot qcom-wdt):
//   WDT_RST=0x4, WDT_EN=0x8, WDT_STS=0xC, WDT_BARK=0x10, WDT_BITE=0x14
#define APSS_WDT_BASE        0x17C10000
#define APSS_WDT_RST         (APSS_WDT_BASE + 0x04)
#define APSS_WDT_EN          (APSS_WDT_BASE + 0x08)
#define APSS_WDT_STS         (APSS_WDT_BASE + 0x0C)
#define APSS_WDT_BARK_TIME   (APSS_WDT_BASE + 0x10)
#define APSS_WDT_BITE_TIME   (APSS_WDT_BASE + 0x14)

ARM_CORE_INFO mHiKey960InfoTable[] = {
  {
    // Cluster 0, Core 0
    0x0, 0x0,

    // MP Core MailBox Set/Get/Clear Addresses and Clear Value
    (UINT64)0xFFFFFFFF
  },
};

/**
  Return the current Boot Mode

  This function returns the boot reason on the platform

  @return   Return the current Boot Mode of the platform

**/
EFI_BOOT_MODE
ArmPlatformGetBootMode (
  VOID
  )
{
  return BOOT_WITH_FULL_CONFIGURATION;
}

/**
  Initialize controllers that must setup in the normal world

  This function is called by the ArmPlatformPkg/Pei or ArmPlatformPkg/Pei/PlatformPeim
  in the PEI phase.

**/
RETURN_STATUS
ArmPlatformInitialize (
  IN  UINTN                     MpId
  )
{
  //
  // DESABILITAR o watchdog de hardware do APSS o mais cedo possivel
  // (fase PEI, antes da cadeia de drivers Qualcomm do Apriori).
  // O WDT estava resetando o aparelho a cada ~3s.
  //
  MmioWrite32 (APSS_WDT_EN, 0x00000000);         // DESABILITA (WDT_EN @ +0x08 = 0)
  MmioWrite32 (APSS_WDT_RST, 0x00000001);        // kick o contador

  return RETURN_SUCCESS;
}

EFI_STATUS
PrePeiCoreGetMpCoreInfo (
  OUT UINTN                   *CoreCount,
  OUT ARM_CORE_INFO           **ArmCoreTable
  )
{
  // Only support one cluster
  *CoreCount    = sizeof(mHiKey960InfoTable) / sizeof(ARM_CORE_INFO);
  *ArmCoreTable = mHiKey960InfoTable;
  return EFI_SUCCESS;
}

// Needs to be declared in the file. Otherwise gArmMpCoreInfoPpiGuid is undefined in the contect of PrePeiCore
EFI_GUID mArmMpCoreInfoPpiGuid = ARM_MP_CORE_INFO_PPI_GUID;
ARM_MP_CORE_INFO_PPI mMpCoreInfoPpi = { PrePeiCoreGetMpCoreInfo };

EFI_PEI_PPI_DESCRIPTOR      gPlatformPpiTable[] = {
  {
    EFI_PEI_PPI_DESCRIPTOR_PPI,
    &mArmMpCoreInfoPpiGuid,
    &mMpCoreInfoPpi
  }
};

VOID
ArmPlatformGetPlatformPpiList (
  OUT UINTN                   *PpiListSize,
  OUT EFI_PEI_PPI_DESCRIPTOR  **PpiList
  )
{
  *PpiListSize = sizeof(gPlatformPpiTable);
  *PpiList = gPlatformPpiTable;
}
