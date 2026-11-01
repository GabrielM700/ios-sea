##
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#  Redmi Note 12S (sea): MediaTek Helio G96 (MT6781), 8 GiB, UEFI started by LK from boot.img (v3).
##

[Defines]
  PLATFORM_NAME                  = sea
  PLATFORM_GUID                  = A1C94E3B-7D20-4F68-9B15-E8037C6A2D4F
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/seaPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = seaPkg/sea.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include MT6781Pkg/MT6781Pkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory (LK's DRAM starts at 0x40000000)
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x40000000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Redmi Note 12S"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"sea"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"Redmi_Note_12S_sea"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"Redmi Note 12S"

  #
  # Simple Frame Buffer: LK's frame 1, 1080x2400, pitch 4320 B (= 1080 * 4), a8r8g8b8
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|seaPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # Input Libraries (no keys yet)
  #
  KeypadDeviceLib|seaPkg/Library/KeypadDeviceLib/KeypadDeviceLib.inf

[Components]
  #
  # Input
  #
  SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf
  SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
