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

  On the partition structure
  --------------------------
  EFI_PARTITION_INFO_PROTOCOL is declared here rather than included. The
  Protocol/Partition.h header does not exist in this edk2 revision, and adding a
  header to a submodule that is pinned by SHA is not something worth doing for
  one structure. The layout is fixed by PI 1.8 and has not changed.

  Usage: from the application menu, pick "Storage Report". It prints and returns
  to the menu by itself.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>

#define PROGRESS_SECONDS 6

//
// EFI_PARTITION_INFO_PROTOCOL, PI 1.8 section 10.5.
//
typedef struct {
  UINT32    Revision;
  UINT32    Type;                 ///< 0 = MBR, 1 = GPT
  EFI_GUID  PartitionType;
  EFI_GUID  UniquePartitionGUID;
  UINT64    StartingLBANumber;
  UINT64    EndingLBANumber;
  CHAR16    *PartitionName;
} EFI_PARTITION_INFO_PROTOCOL;

#define EFI_PARTITION_INFO_PROTOCOL_GUID \
  { 0x8cf2f62c, 0xbc9b, 0x4821, { 0x9d, 0x14, 0x98, 0x9a, 0x78, 0xbe, 0x2e, 0xc5 } }

/**
  Print one block device and, if it has any, its partitions.

  @param[in] Handle  The block device handle.
  @param[in] Index   Its position in the handle buffer, so the reader can refer
                     to it.
**/
STATIC
VOID
PrintBlockDevice (
  IN EFI_HANDLE  Handle,
  IN UINTN       Index
  )
{
  EFI_BLOCK_IO_PROTOCOL        *BlockIo;
  EFI_PARTITION_INFO_PROTOCOL  *PartitionInfo;
  EFI_HANDLE                  *PartHandles;
  UINTN                       NoParts;
  EFI_STATUS                  Status;
  UINTN                       PartIndex;
  UINT32                      BlockSize;

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
  BlockSize = BlockIo->Media->BlockSize;

  Print (L"  [%2d] %lu MB  (%lu byte blocks)\r\n",
         (UINT32)Index,
         (UINT32)((BlockIo->Media->LastBlock + 1) * BlockSize / (1024 * 1024)),
         (UINT32)BlockSize
         );

  //
  // A partition handle is not required to carry its own openable protocol
  // instances, so the children are found by LocateHandleBuffer against the
  // parent's interface rather than by opening anything on them.
  //
  PartHandles = NULL;
  NoParts     = 0;
  Status      = gBS->LocateHandleBuffer (
                         ByProtocol,
                         &gEfiPartitionInfoProtocolGuid,
                         (VOID *)Handle,
                         &NoParts,
                         &PartHandles
                         );
  if (EFI_ERROR (Status) || (NoParts == 0) || (PartHandles == NULL)) {
    gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    return;
  }

  for (PartIndex = 0; PartIndex < NoParts; PartIndex++) {
    EFI_PARTITION_INFO_PROTOCOL  *ThisPart;

    Status = gBS->HandleProtocol (
                     PartHandles[PartIndex],
                     &gEfiPartitionInfoProtocolGuid,
                     (VOID **)&ThisPart
                     );
    if (EFI_ERROR (Status) || (ThisPart == NULL)) {
      continue;
    }

    //
    // PartitionName is null when the entry carries no name, which happens for
    // an MBR partition and for a zeroed GPT slot. Both are printed anyway, so
    // the list of sizes is complete either way.
    //
    Print (L"        %-20s %6lu MB   LBA %lu..%lu\r\n",
           (ThisPart->PartitionName != NULL) ? ThisPart->PartitionName : L"<no name>",
           (UINT32)((ThisPart->EndingLBANumber - ThisPart->StartingLBANumber + 1) * BlockSize / (1024 * 1024)),
           (UINT32)ThisPart->StartingLBANumber,
           (UINT32)ThisPart->EndingLBANumber
           );
  }

  FreePool (PartHandles);
  gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
}

/**
  Entry point. Waits for the drivers to settle, then prints everything.

  @param[in] ImageHandle  Image handle of this application.
  @param[in] SystemTable  System table.

  @retval EFI_SUCCESS  Always. This is a report, there is nothing to fail at.
**/
EFI_STATUS
EFIAPI
UefiApplicationEntryPoint (
  IN EFI_IMAGE_HANDLE  ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  *Buffer;
  UINTN       NoHandles;
  EFI_STATUS  Status;
  UINTN       Index;
  UINTN       Seconds;
  EFI_HANDLE  *SecondBuffer;
  UINTN       SecondCount;

  //
  // Drivers connect over time and a partition table shows up well after the
  // last splash. Reporting an empty list because nothing had connected yet would
  // be worse than useless, so wait first and look again afterwards.
  //
  for (Seconds = 0; Seconds < PROGRESS_SECONDS; Seconds++) {
    gST->ConOut->OutputString (gST->ConOut, L".");
    gBS->Stall (1000000);
  }
  gST->ConOut->OutputString (gST->ConOut, L"\r\n");

  Print (L"========================================================\r\n");
  Print (L"  STORAGE REPORT\r\n");
  Print (L"========================================================\r\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &NoHandles,
                  &Buffer
                  );
  if (EFI_ERROR (Status) || (NoHandles == 0) || (Buffer == NULL)) {
    Print (L"\r\nNo BlockIo handle exists at all.\r\n");
    Print (L"The storage driver never registered anything.\r\n");
    Print (L"\r\nBack to the menu in 10 seconds.\r\n");
    gBS->Stall (10000000);
    return EFI_SUCCESS;
  }

  Print (L"\r\n%u block device(s):\r\n", (UINT32)NoHandles);
  for (Index = 0; Index < NoHandles; Index++) {
    PrintBlockDevice (Buffer[Index], Index);
  }

  //
  // Look a second time. A larger list means drivers connected while this was
  // printing, so the first pass was simply too early to mean anything, and that
  // is worth saying out loud rather than leaving a wrong answer on screen.
  //
  SecondBuffer = NULL;
  SecondCount  = 0;
  Status       = gBS->LocateHandleBuffer (
                          ByProtocol,
                          &gEfiBlockIoProtocolGuid,
                          NULL,
                          &SecondCount,
                          &SecondBuffer
                          );
  if (!EFI_ERROR (Status) && (SecondCount > NoHandles)) {
    Print (L"\r\nRe-checked later: %u block device(s) now exist, so more\r\n", (UINT32)SecondCount);
    Print (L"drivers connected while this was printing. Full list:\r\n");
    for (Index = 0; Index < SecondCount; Index++) {
      PrintBlockDevice (SecondBuffer[Index], Index);
    }
    FreePool (SecondBuffer);
  }
  FreePool (Buffer);

  Print (L"\r\n--------------------------------------------------------\r\n");
  Print (L"  A partition of tens of GB means the controller enumerated\r\n");
  Print (L"  it and storage works. UEFI still cannot mount it: only Fat\r\n");
  Print (L"  is in this build and the phone's partitions are ext4 and\r\n");
  Print (L"  f2fs, so the File Manager would show nothing either way.\r\n");
  Print (L"\r\nBack to the menu in 15 seconds.\r\n");
  gBS->Stall (15000000);

  return EFI_SUCCESS;
}