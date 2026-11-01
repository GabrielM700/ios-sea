#include <Library/MemoryMapLib.h>

//
// Redmi Note 12S (sea, MT6781, 8 GiB). Every address below was measured on the phone by the ios-sea probes
// (docs/SEA.md, NOTES.md), nothing is copied from another MediaTek chip:
//   * LK loads the boot image's kernel at 0x40080000 and enters it at EL1 with MMU and caches off.
//   * The low bank 0x40000000..0xC0000000 belongs to LK, ATF, the modem, ADSP, SCP, SSPM and the display
//     buffer: only what UEFI itself needs is declared there (same layout as Silicium's other MediaTek ports).
//   * RAM 0xC0100000..0x20D808000 reads and writes (probe 17); the DRAMC rank page 0x13FFFF000 is left out
//     here and the reserved block above 0x20D808000 stays unmapped.
//   * LK's frame 1 is the framebuffer: 0x7C606000, 1080x2400, pitch 4320 B, a8r8g8b8 (10 MiB).
//
STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
  {"UEFI Stack",         0x40000000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"UEFI FD",            0x40080000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"DXE Heap",           0x40280000, 0x03C00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  {"Display Reserved",   0x7C606000, 0x00A00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH},
  {"RAM Low",            0xC0100000, 0x7FEFF000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK},
  {"RAM High",           0x140000000, 0xCD808000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv,  WRITE_BACK},

  // Register Regions
  {"GIC Distributor",    0x0C000000, 0x00040000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"GIC Redistributors", 0x0C040000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"WatchDog Timer",     0x10007000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"I2C-0",              0x11007000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"USB MUSB",           0x11200000, 0x00010000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"UFS HCI",            0x11270000, 0x00003000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE},
  {"USB PHY",            0x11CA0000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO,   NS_DEVICE}
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
