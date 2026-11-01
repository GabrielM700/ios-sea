/**
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Redmi Note 12S (MT6781): the one thing SEC must do before anything else runs is stop the watchdog
  (the MediaTek TOPRGU, 0x10007000): LK arms it and the phone resets after a few tens of seconds otherwise.
  The key 0x22 in the top byte makes MODE writes take effect; bit 0 (ENABLE) is written as 0.
**/

#include <Library/IoLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PlatformSecLib.h>

#define WDT_MODE_KEY  0x22000000

VOID
DisableWatchDogTimer ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WDogRegion;

  Status = LocateMemoryRegionByName ("WatchDog Timer", &WDogRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  MmioWrite32 (WDogRegion.Address, WDT_MODE_KEY);
}

VOID
PlatformInitialize ()
{
  DisableWatchDogTimer ();
}
