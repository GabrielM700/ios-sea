##
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
#  MediaTek Helio G96 (MT6781). Written for the Redmi Note 12S (sea) from what ios-sea measured on the phone.
#  No clock, GPIO, PMIC or MSDC driver yet: LK leaves the clocks on, and the first UEFI only needs the
#  framebuffer, the GIC, the timer and the CPUs (the MT6768/MT6789 drivers are written for those chips' registers).
##

[Defines]
  #
  # Timer: LK enters EL1; the virtual timer (INTID 27) was verified on this SoC, the physical one was not.
  #
  USE_PHYSICAL_TIMER                         = 0

!include SiliciumPkg/SiliciumPkg.dsc.inc

[BuildOptions]
  *_*_AARCH64_CC_FLAGS = -march=armv8.2-a+crypto+rcpc

[PcdsFixedAtBuild]
  #
  # CPU: 6x Cortex-A55 (cores 0-5) and 2x Cortex-A76 (cores 6-7); MPIDR 0x000..0x700, one cluster each.
  #
  gArmPlatformTokenSpaceGuid.PcdCoreCount|8
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  #
  # GIC (GICv3)
  #
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x0C000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0C040000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"MediaTek (TM) Helio G96 @ 2.05 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"MT6781"

  #
  # ACPI (no tables in this port, the values are still read by the common code)
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemId|"MTK   "

[LibraryClasses]
  #
  # SEC Libraries
  #
  PlatformSecLib|MT6781Pkg/Library/PlatformSecLib/PlatformSecLib.inf
