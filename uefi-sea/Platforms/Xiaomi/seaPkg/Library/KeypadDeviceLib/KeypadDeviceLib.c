#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/KeypadDeviceLib.h>

//
// The sea's power and volume keys sit on the PMIC and a GPIO; no driver reads them yet (docs/uefi-sea.md),
// so the keypad reports "nothing pressed". The UEFI boots straight to the loader, it needs no keys for that.
//

EFI_STATUS
EFIAPI
KeypadDeviceReset (KEYPAD_DEVICE_PROTOCOL *This)
{
  return EFI_SUCCESS;
}

EFI_STATUS
KeypadDeviceGetKeys (
  KEYPAD_DEVICE_PROTOCOL *This,
  KEYPAD_RETURN_API      *KeypadReturnApi,
  UINT64                  Delta)
{
  return EFI_SUCCESS;
}
