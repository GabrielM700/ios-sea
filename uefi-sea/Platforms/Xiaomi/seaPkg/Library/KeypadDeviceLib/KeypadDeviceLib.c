#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BitmapLib.h>
#include <Library/KeypadDeviceLib.h>
#include <Library/DebugLib.h>

#include <Library/IoLib.h>
#include <Library/TimerLib.h>


//
// sea (MT6781 + MT6366 PMIC), measured on the phone with ios-sea shim-sea/keyprobe.c:
//   PWRAP 0x1000D000 uses the MT6768 layout: INIT_DONE2 0x94, WACS2_CMD 0xC20, RDATA 0xC24 (FSM in [18:16]),
//   VLDCLR 0xC28. PMIC reg 0x008 reads 0x6610 (MT6366). TOPSTATUS (0x28): bit1 = power (0 = pressed),
//   bit3 = "home" = volume up (0 = pressed).
//   Volume down is the keypad matrix at 0x10010000: pressing it clears bits 0 and 9 of KP_MEM1 (all of
//   MEM1..MEM4 change, idle value 0xFFFF) and sets KP_STA bit0.
//
#define PWRAP_BASE       0x1000D000ULL
#define PWRAP_WACS2_CMD  (PWRAP_BASE + 0xC20)
#define PWRAP_WACS2_RD   (PWRAP_BASE + 0xC24)
#define PWRAP_WACS2_VLD  (PWRAP_BASE + 0xC28)
#define KP_BASE          0x10010000ULL
#define KP_STA           (KP_BASE + 0x0)
#define KP_MEM1          (KP_BASE + 0x4)
#define PMIC_TOPSTATUS   0x28

typedef enum {
  KEY_TYPE_UNKNOWN,
  KEY_TYPE_PMIC,
  KEY_TYPE_KP,
} KEY_TYPE;

typedef struct {
  KEY_CONTEXT EfiKeyContext;
  KEY_TYPE    Type;
  UINT32      Pin;
} KEY_CONTEXT_PRIVATE;

STATIC
BOOLEAN
PwrapFsm (UINT32 Want)
{
  UINTN Tries;

  for (Tries = 0; Tries < 5000; Tries++) {
    if (((MmioRead32 (PWRAP_WACS2_RD) >> 16) & 7) == Want) {
      return TRUE;
    }

    MicroSecondDelay (1);
  }

  return FALSE;
}

STATIC
BOOLEAN
PmicRead (UINT16 Address, UINT16 *Value)
{
  if (!PwrapFsm (0)) {
    MmioWrite32 (PWRAP_WACS2_VLD, 1);
    return FALSE;
  }

  MmioWrite32 (PWRAP_WACS2_CMD, ((UINT32)(Address >> 1)) << 16);
  if (!PwrapFsm (6)) {
    return FALSE;
  }

  *Value = (UINT16)(MmioRead32 (PWRAP_WACS2_RD) & 0xFFFF);
  MmioWrite32 (PWRAP_WACS2_VLD, 1);
  return TRUE;
}

UINTN gBitmapScanCodes[BITMAP_NUM_WORDS(0x18)]    = {0};
UINTN gBitmapUnicodeChars[BITMAP_NUM_WORDS(0x7f)] = {0};

EFI_KEY_DATA gKeyDataPowerDown      = {.Key = {.ScanCode = SCAN_RIGHT,}};
EFI_KEY_DATA gKeyDataPowerUp        = {.Key = {.ScanCode = SCAN_LEFT,}};
EFI_KEY_DATA gKeyDataPowerLongpress = {.Key = {.ScanCode = SCAN_ESC,}};

#define MS2NS(ms) (((UINT64)(ms)) * 1000000ULL)

STATIC
inline
VOID
KeySetState (
  UINT16  ScanCode,
  CHAR16  UnicodeChar,
  BOOLEAN Value)
{
  if (ScanCode && ScanCode < 0x18) {
    if (Value) {
      BitmapSet(gBitmapScanCodes, ScanCode);
    } else {
      BitmapClear(gBitmapScanCodes, ScanCode);
    }
  }

  if (UnicodeChar && UnicodeChar < 0x7f) {
    if (Value) {
      BitmapSet(gBitmapUnicodeChars, ScanCode);
    } else {
      BitmapClear(gBitmapUnicodeChars, ScanCode);
    }
  }
}

STATIC
inline
BOOLEAN
KeyGetState (
  UINT16 ScanCode,
  CHAR16 UnicodeChar)
{
  if (ScanCode && ScanCode < 0x18) {
    if (!BitmapTest(gBitmapScanCodes, ScanCode)) {
      return FALSE;
    }
  }

  if (UnicodeChar && UnicodeChar < 0x7f) {
    if (!BitmapTest(gBitmapUnicodeChars, ScanCode)) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
inline
VOID
LibKeyInitializeKeyContext (KEY_CONTEXT *Context)
{
  SetMem(&Context->KeyData, sizeof(Context->KeyData), 0);

  Context->Time      = 0;
  Context->State     = KEYSTATE_RELEASED;
  Context->Repeat    = FALSE;
  Context->Longpress = FALSE;
}

STATIC
inline
VOID
LibKeyUpdateKeyStatus (
  KEY_CONTEXT       *Context,
  KEYPAD_RETURN_API *KeypadReturnApi,
  BOOLEAN            IsPressed,
  UINT64             Delta)
{
  // Keep Track of the Actual State
  KeySetState (Context->KeyData.Key.ScanCode, Context->KeyData.Key.UnicodeChar, IsPressed);

  // Update Key Time
  Context->Time += Delta;

  switch (Context->State) {
    case KEYSTATE_RELEASED:
      if (IsPressed) {
        // Change to Pressed
        Context->Time  = 0;
        Context->State = KEYSTATE_PRESSED;
      }

      break;

    case KEYSTATE_PRESSED:
      if (IsPressed) {
        // Key Repeat
        if (Context->Repeat && Context->Time >= MS2NS(100)) {
          KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &Context->KeyData);
          Context->Time   = 0;
          Context->Repeat = TRUE;
        } else if (!Context->Longpress && Context->Time >= MS2NS(500)) {
          // Handle Key Combos
          if (Context->KeyData.Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
            if (KeyGetState (SCAN_DOWN, 0)) {
              // Report SCAN_RIGHT
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &gKeyDataPowerDown);
            } else if (KeyGetState (SCAN_UP, 0)) {
              // Report SCAN_LEFT
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &gKeyDataPowerUp);
            } else {
              // Report SCAN_ESC
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &gKeyDataPowerLongpress);
            }
          } else { // Post first Keyrepeat Event
            // only Start Keyrepeat if we're not doing a Combo
            if (!KeyGetState (0, CHAR_CARRIAGE_RETURN)) {
              KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &Context->KeyData);
              Context->Time   = 0;
              Context->Repeat = TRUE;
            }
          }

          Context->Longpress = TRUE;
        }
      } else {
        if (!Context->Longpress) {
          // We Supressed Down, so Report it Now
          KeypadReturnApi->PushEfikeyBufTail (KeypadReturnApi, &Context->KeyData);
          Context->State = KEYSTATE_LONGPRESS_RELEASE;
        } else if (Context->Time >= MS2NS(10)) {
          // We Reported another Key Already
          Context->Time      = 0;
          Context->Repeat    = FALSE;
          Context->Longpress = FALSE;
          Context->State     = KEYSTATE_RELEASED;
        }
      }

      break;

    case KEYSTATE_LONGPRESS_RELEASE:
      // Change to Released
      Context->Time      = 0;
      Context->Repeat    = FALSE;
      Context->Longpress = FALSE;
      Context->State     = KEYSTATE_RELEASED;
      break;

    default:
      ASSERT(FALSE);
      break;
  }
}

STATIC KEY_CONTEXT_PRIVATE KeyContextVolumeUp;
STATIC KEY_CONTEXT_PRIVATE KeyContextVolumeDown;
STATIC KEY_CONTEXT_PRIVATE KeyContextPower;

STATIC KEY_CONTEXT_PRIVATE *KeyList[] = { &KeyContextVolumeUp, &KeyContextVolumeDown, &KeyContextPower };

RETURN_STATUS
EFIAPI
KeypadDeviceConstructor ()
{
  KeyContextVolumeUp.Type   = KEY_TYPE_PMIC;
  KeyContextVolumeUp.Pin    = 3;   // TOPSTATUS bit3
  KeyContextVolumeDown.Type = KEY_TYPE_KP;
  KeyContextVolumeDown.Pin  = 0;   // KP_MEM1 bit0
  KeyContextPower.Type      = KEY_TYPE_PMIC;
  KeyContextPower.Pin       = 1;   // TOPSTATUS bit1
  return RETURN_SUCCESS;
}

EFI_STATUS
EFIAPI
KeypadDeviceReset (KEYPAD_DEVICE_PROTOCOL *This)
{
  LibKeyInitializeKeyContext(&KeyContextVolumeUp.EfiKeyContext);
  KeyContextVolumeUp.EfiKeyContext.KeyData.Key.ScanCode   = SCAN_UP;

  LibKeyInitializeKeyContext(&KeyContextVolumeDown.EfiKeyContext);
  KeyContextVolumeDown.EfiKeyContext.KeyData.Key.ScanCode = SCAN_DOWN;

  LibKeyInitializeKeyContext(&KeyContextPower.EfiKeyContext);
  KeyContextPower.EfiKeyContext.KeyData.Key.UnicodeChar   = CHAR_CARRIAGE_RETURN;

  return EFI_SUCCESS;
}

EFI_STATUS
KeypadDeviceGetKeys (
  KEYPAD_DEVICE_PROTOCOL *This,
  KEYPAD_RETURN_API      *KeypadReturnApi,
  UINT64                  Delta)
{
  UINT16  Top    = 0xFFFF;
  UINT32  Mem1   = MmioRead32 (KP_MEM1);
  BOOLEAN TopOk  = PmicRead (PMIC_TOPSTATUS, &Top);
  UINTN   Index;

  for (Index = 0; Index < (sizeof(KeyList) / sizeof(KeyList[0])); Index++) {
    KEY_CONTEXT_PRIVATE *Context = KeyList[Index];
    BOOLEAN              IsPressed;

    if (Context->Type == KEY_TYPE_PMIC) {
      if (!TopOk) {
        continue;
      }

      IsPressed = ((Top >> Context->Pin) & 1) == 0;
    } else if (Context->Type == KEY_TYPE_KP) {
      IsPressed = ((Mem1 >> Context->Pin) & 1) == 0;
    } else {
      continue;
    }

    LibKeyUpdateKeyStatus (&Context->EfiKeyContext, KeypadReturnApi, IsPressed, Delta);
  }

  return EFI_SUCCESS;
}
