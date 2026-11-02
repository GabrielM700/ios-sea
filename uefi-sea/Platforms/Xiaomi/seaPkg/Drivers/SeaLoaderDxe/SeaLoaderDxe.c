/**
  Redmi Note 12S (MT6781): the shim's USB loader inside UEFI.

  What ios-sea's shim does (shim-sea/usb.c): a fastboot-compatible gadget on the phone's own MUSB (VID:PID 18d1:4ee0), so that
      fastboot stage payload.bin && fastboot oem go
  (and, for a big root, `fastboot oem addr HEX` + several `fastboot stage`: tools/seastage.sh) puts a SWEETXNU payload
  straight into RAM and starts XNU. Here it runs once, at EndOfDxe (before the boot manager), and returns to UEFI after
  120 s without a host so the phone still boots to its menu. usb.c is the shim's file, unchanged (build.sh copies it).

  Output: DebugLib (DEBUG builds print it on the screen) and a coloured bar on the GOP (visible in RELEASE too):
  blue = waiting, cyan = command, yellow = download size, green = download finished, white = jumping into XNU.
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include "console.h"
#include "usb.h"

EFI_STATUS SeaBootPayload (UINT64 Addr, UINT64 Len);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
STATIC int                           mRow;
STATIC UINTN                         mCmdColour;

STATIC
VOID
Bar (UINT8 R, UINT8 G, UINT8 B)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Px = { B, G, R, 0 };

  if (mGop == NULL) {
    return;
  }

  mGop->Blt (mGop, &Px, EfiBltVideoFill, 0, 0, 0, 0, mGop->Mode->Info->HorizontalResolution, 48, 0);
}

void
con_clear (unsigned Colour)
{
  mRow = 0;
  Bar (0, 0, 0xc0);
}

void
con_skip (void)
{
  mRow++;
}

int
con_row (void)
{
  return mRow;
}

void
con_str (const char *L, const char *S)
{
  mRow++;
  DebugPrint (DEBUG_ERROR, "%a: %a\n", L, S);
  if (AsciiStrnCmp (L, "CMD", 3) == 0) {
    mCmdColour++;
    Bar (0, 0xc0, (mCmdColour & 1) ? 0xc0 : 0x80);
  } else if (AsciiStrnCmp (L, "USB", 3) == 0) {
    Bar (0, 0, 0xc0);
  }
}

void
con_hex (const char *L, uint64_t V)
{
  mRow++;
  DebugPrint (DEBUG_ERROR, "%a: 0x%lx\n", L, V);
  if (AsciiStrnCmp (L, "SIZE", 4) == 0) {
    Bar (0xc0, 0xc0, 0);
  } else if (AsciiStrnCmp (L, "DONE", 4) == 0) {
    Bar (0, 0xc0, 0);
  }
}

void
con_hex2 (const char *L1, const char *L2, uint64_t V1, const char *L3, uint64_t V2)
{
  mRow++;
  DebugPrint (DEBUG_ERROR, "%a %a0x%lx %a0x%lx\n", L1, L2, V1, L3, V2);
}

STATIC
VOID
EFIAPI
SeaLoaderRun (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT64      Addr = 0, Len = 0;
  int         Rc;
  EFI_STATUS  Status;

  gBS->CloseEvent (Event);
  gBS->SetWatchdogTimer (0, 0, 0, NULL);        // staging a 3 GiB root takes ~8 minutes: no 5-minute reset
  gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);

  Rc = usb_fastboot (&Addr, &Len);
  if (Rc == 1) {
    // usb_fastboot leaves Addr and Len at 0 when nobody came for 120 s, and sets them for an explicit "reboot"
    if ((Addr == 0) && (Len == 0)) {
      DebugPrint (DEBUG_ERROR, "SeaLoader: no host, continuing to the boot manager\n");
      return;
    }

    gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  }

  if (Rc == 0) {
    Bar (0xff, 0xff, 0xff);
    Status = SeaBootPayload (Addr, Len);
    DebugPrint (DEBUG_ERROR, "SeaLoader: payload not started (%r)\n", Status);
    Bar (0xc0, 0, 0);
    gBS->Stall (5000000);
  }
}

EFI_STATUS
EFIAPI
SeaLoaderEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  Ev;

  return gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, SeaLoaderRun, NULL, &gEfiEndOfDxeEventGroupGuid, &Ev);
}
