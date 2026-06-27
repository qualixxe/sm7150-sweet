/** @file
  EFIDroid Keypad Device Protocol.

  Recriado a partir do uso real no KeypadDxe (KeypadController.c / Keypad.c),
  para substituir o header que vinha do Lumia930Pkg (inexistente neste repo).

  Cadeia do input:
    [ButtonsDxe do sweet] instala gEFIDroidKeypadDeviceProtocolGuid num handle
      -> KeypadDxe (driver-binding) liga nesse handle, chama GetKeys/Reset e
         instala SimpleTextIn / SimpleTextInputEx
      -> PlatformBm adiciona esse handle ao ConIn
      -> menu (SimpleInit) le o ConIn.

  GUID (em F11.dec): gEFIDroidKeypadDeviceProtocolGuid
    = b27625b5-0b6c-4614-aa3c-3313b51d3646

  ABI (confirmada pelo source):
    KEYPAD_RETURN_API.PushEfikeyBufTail(This, EFI_KEY_DATA*)   [Keypad.c]
    KEYPAD_DEVICE_PROTOCOL.Reset(This)                          [KeypadController.c]
    KEYPAD_DEVICE_PROTOCOL.GetKeys(This, KEYPAD_RETURN_API*, UINT64 Delta)
**/

#ifndef __PROTOCOL_KEYPAD_DEVICE_H__
#define __PROTOCOL_KEYPAD_DEVICE_H__

#include <Protocol/SimpleTextInEx.h>

typedef struct _KEYPAD_RETURN_API       KEYPAD_RETURN_API;
typedef struct _KEYPAD_DEVICE_PROTOCOL  KEYPAD_DEVICE_PROTOCOL;

//
// Callback API que o KeypadDxe passa para o produtor (ButtonsDxe). O produtor
// chama PushEfikeyBufTail() para cada tecla lida no GetKeys().
//
struct _KEYPAD_RETURN_API {
  VOID
  (EFIAPI *PushEfikeyBufTail) (
    IN KEYPAD_RETURN_API  *This,
    IN EFI_KEY_DATA       *KeyData
    );
};

typedef
EFI_STATUS
(EFIAPI *KEYPAD_DEVICE_RESET) (
  IN KEYPAD_DEVICE_PROTOCOL  *This
  );

typedef
EFI_STATUS
(EFIAPI *KEYPAD_DEVICE_GETKEYS) (
  IN KEYPAD_DEVICE_PROTOCOL  *This,
  IN KEYPAD_RETURN_API       *KeypadReturnApi,
  IN UINT64                   Delta
  );

struct _KEYPAD_DEVICE_PROTOCOL {
  KEYPAD_DEVICE_RESET     Reset;
  KEYPAD_DEVICE_GETKEYS   GetKeys;
};

extern EFI_GUID gEFIDroidKeypadDeviceProtocolGuid;

#endif // __PROTOCOL_KEYPAD_DEVICE_H__
