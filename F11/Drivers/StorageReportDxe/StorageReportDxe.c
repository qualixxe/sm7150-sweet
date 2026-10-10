/** @file
  Storage Report - prints the block devices and their partitions.

  Why this exists
  ---------------
  The UEFI Shell has exactly the commands needed for this, dblk, dh and map, but
  the phone has no keyboard attached, so nothing can be typed into it. The Shell
  is useless here for the one job it was wanted for.

  So the same information is produced by an application that needs no input at
  all: the user picks it from the menu with the mouse, and it prints what it
  found on the screen that is already there.

  What it prints
  --------------
  Every BlockIo handle, its media size, and every partition behind it with its
  name taken from the GPT. That last part is the point. The File Manager cannot
  answer the storage question at all, because the firmware mounts FAT32 and this
  phone's partitions are ext4 and f2fs, so a working controller would show
  nothing either. A partition name needs no filesystem, which is exactly what is
  missing everywhere else.

  Usage: from the application menu, pick "Storage Report". It prints and returns
  to the menu by itself.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/Partition.h>

#define PROGRESS_SECONDS 6

/**
  Print one block device and, if it has any, its partitions.

  @param[in] Handle    The block device handle.
  @param[in] Index     Its position in the handle buffer, for the reader to
                       refer to.
**/
STATIC
VOID
PrintBlockDevice (
  IN EFI_HANDLE  Handle,
  IN UINTN       Index
  )
{
  EFI_BLOCK_IO_PROTOCOL            *BlockIo;
  EFI_PARTITION_INFO_PROTOCOL      *PartitionInfo;
  EFI_HANDLE                      *PartHandles;
  UINTN                           NoParts;
  EFI_STATUS                      Status;
  CHAR16                          Name[72];
  UINTN                           PartIndex;

  Status = gBS->OpenProtocol (
                  Handle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **)&BlockIo,
                  EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status) || (BlockIo == NULL) || (BlockIo->Media == NULL)) {
    return;
  }

  Print (L"  [%2d] handle %p  block size %lu  blocks %lu  => %lu MB\r\n",
         (UINT32)Index,
         Handle,
         (UINT32)BlockIo->Media->BlockSize,
         (UINT32)BlockIo->Media->LastBlock,
         (UINT32)((BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize / (1024 * 1024))
         );

  Status = gBS->OpenProtocol (
                  Handle,
                  &gEfiPartitionInfoProtocolGuid,
                  (VOID **)&PartitionInfo,
                  EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status) || (PartitionInfo == NULL)) {
    gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    return;
  }

  //
  // A partition handle is not required to have its own protocol instances, so
  // ask the boot services rather than opening anything further on it.
  //
  PartHandles = NULL;
  NoParts     = 0;
  Status      = gBS->LocateHandleBuffer (
                         ByProtocol,
                         &gEfiPartitionInfoProtocolGuid,
                         PartitionInfo,
                         &NoParts,
                         &PartHandles
                         );
  if (EFI_ERROR (Status) || (NoParts == 0) || (PartHandles == NULL)) {
    gBS->CloseProtocol (Handle, &gEfiPartitionInfoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    return;
  }

  for (PartIndex = 0; PartIndex < NoParts; PartIndex++) {
    EFI_PARTITION_INFO_PROTOCOL  *ThisPart;

    Status = gBS->HandleProtocol (PartHandles[PartIndex], &gEfiPartitionInfoProtocolGuid, (VOID **)&ThisPart);
    if (EFI_ERROR (Status) || (ThisPart == NULL) || (ThisPart->PartitionName == NULL)) {
      continue;
    }

    StrCpyS (Name, ARRAY_SIZE (Name), ThisPart->PartitionName);

    if (ThisPart->EndingLBANumber > ThisPart->StartingLBANumber) {
      Print (L"        %-20s LBA %lu..%lu  %lu MB\r\n",
             Name,
             (UINT32)ThisPart->StartingLBANumber,
             (UINT32)ThisPart->EndingLBANumber,
             (UINT32)((ThisPart->EndingLBANumber - ThisPart->StartingLBANumber + 1) *
                      BlockIo->Media->BlockSize / (1024 * 1024))
             );
    } else {
      Print (L"        %-20s (no range in the partition info)\r\n", Name);
    }
  }

  FreePool (PartHandles);
  gBS->CloseProtocol (Handle, &gEfiPartitionInfoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
}

/**
  Entry point. Waits for the drivers to settle, then prints everything.

  @param[in] ImageHandle  Image handle of this application.
  @param[in] SystemTable  System table.

  @retval EFI_SUCCESS     Always. This is a report, nothing to fail at.
**/
EFI_STATUS
EFIAPI
UefiApplicationEntryPoint (
  IN EFI_IMAGE_HANDLE  ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE    *Buffer;
  UINTN         NoHandles;
  EFI_STATUS    Status;
  UINTN         Index;
  UINTN         Seconds;
  EFI_HANDLE    SavedBuffer;
  UINTN         SavedNoHandles;

  //
  // Drivers connect over a period of time and a partition table shows up well
  // after the last splash. Reporting an empty list because nothing had connected
  // yet would be worse than useless, so wait first and look again afterwards.
  //
  for (Seconds = 0; Seconds < PROGRESS_SECONDS; Seconds++) {
    gST->ConOut->OutputString (gST->ConOut, L".");
    gBS->Stall (1000000);
  }
  gST->ConOut->OutputString (gST->ConOut, L"\r\n");

  Print (L"========================================================\r\n");
  Print (L"  STORAGE REPORT\r\n");
  Print (L"  no keyboard needed, read this off the screen\r\n");
  Print (L"========================================================\r\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &NoHandles,
                  &Buffer
                  );
  if (EFI_ERROR (Status) || (NoHandles == 0) || (Buffer == NULL)) {
    Print (L"\r\nNo BlockIo handle exists. The storage driver never registered.\r\n");
    Print (L"\r\nReturning to the menu in 10 seconds.\r\n");
    gBS->Stall (10000000);
    return EFI_SUCCESS;
  }

  Print (L"\r\n%u block device(s):\r\n", (UINT32)NoHandles);
  for (Index = 0; Index < NoHandles; Index++) {
    PrintBlockDevice (Buffer[Index], Index);
  }

  //
  // Look a second time. If the list grew, drivers connected while we were
  // printing, and the first pass was simply too early to mean anything.
  //
  SavedBuffer   = NULL;
  SavedNoHandles = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &SavedNoHandles,
                  &SavedBuffer
                  );
  if (!EFI_ERROR (Status) && (SavedNoHandles > NoHandles)) {
    Print (L"\r\nRe-checked later: now %u block device(s) exist, so more\r\n", (UINT32)SavedNoHandles);
    Print (L"drivers connected while this was printing. Full list:\r\n");
    for (Index = 0; Index < SavedNoHandles; Index++) {
      PrintBlockDevice (SavedBuffer[Index], Index);
    }
    FreePool (SavedBuffer);
  }
  FreePool (Buffer);

  Print (L"\r\n--------------------------------------------------------\r\n");
  Print (L"  If a partition above is tens of GB, the controller\r\n");
  Print (L"  enumerated it and storage works. UEFI simply cannot\r\n");
  Print (L"  mount it: only Fat is present here and the phone's\r\n");
  Print (L"  partitions are ext4 and f2fs, which is why the File\r\n");
  Print (L"  Manager never shows them either way.\r\n");
  Print (L"\r\nReturning to the menu in 15 seconds.\r\n");
  gBS->Stall (15000000);

  return EFI_SUCCESS;
}