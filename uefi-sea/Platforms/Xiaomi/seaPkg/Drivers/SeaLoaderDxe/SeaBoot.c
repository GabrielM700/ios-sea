/**
  Redmi Note 12S: act as iBoot for XNU. Port of ios-sea's shim-sea/loader.c (boot_from), for a payload already in RAM
  (the USB loader put it there): copy the segments of the SWEETXNU payload to phys_base + (vmaddr - virt_base), place the
  ADT and boot_args behind the kernel, then leave UEFI (ExitBootServices), clean the caches, turn the MMU and the caches
  off and jump to the entry point with x0 = boot_args (physical) - the state LK gives the shim and the shim gives XNU.
**/

#include <Uefi.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define BOOT_LINE_LENGTH  608
#define PAGE_16K          0x4000ULL

// the LK framebuffer the kernel keeps drawing its console on (docs/SEA.md)
#define FB_BASE    0x7c606000ULL
#define FB_W       1080
#define FB_H       2400
#define FB_STRIDE  (1080 * 4)

#pragma pack(1)
typedef struct {
  UINT64 VmAddr, VmSize, FileOff, FileSize;
} PAYLOAD_SEG;

typedef struct {
  CHAR8   Magic[8];
  UINT32  Version, NSegs;
  UINT64  VirtBase, EntryVa, AdtOff, AdtSize, KernelEnd;
  UINT64  PhysBase, MemSize;
  CHAR8   CmdLine[BOOT_LINE_LENGTH];
  PAYLOAD_SEG Segs[];
} PAYLOAD;
#pragma pack()

// the layout XNU reads is packed to its own offsets (shim-sea/loader.c asserts them: Video at 40, CommandLine at 108,
// total 736); write the fields at those offsets instead of relying on this compiler's struct padding
#define BA_REVISION    0
#define BA_VERSION     2
#define BA_VIRTBASE    8
#define BA_PHYSBASE    16
#define BA_MEMSIZE     24
#define BA_TOPKDATA    32
#define BA_VIDEO       40
#define BA_MACHINETYPE 88
#define BA_DTP         96          // see boot.h: machineType (4) + pad, deviceTreeP at 96
#define BA_DTLEN       104
#define BA_CMDLINE     108
#define BA_BOOTFLAGS   720
#define BA_MEMACTUAL   728
#define BA_SIZE        736

typedef VOID (*XNU_ENTRY)(UINT64 BootArgsPa);

STATIC
BOOLEAN
MagicOk (CONST CHAR8 *M)
{
  return CompareMem (M, "SWEETXNU", 8) == 0;
}

EFI_STATUS
SeaBootPayload (
  UINT64  Addr,
  UINT64  Len
  )
{
  CONST PAYLOAD  *P = (CONST PAYLOAD *)(UINTN)Addr;
  UINT64         PaOfVaBase, RdStart = Addr, RdEnd = Addr + Len;
  UINT64         AdtPa, BaPa, Top, EntryPa;
  UINT32         I;
  UINT8          *Ba;

  if (Addr & 7) {
    return EFI_INVALID_PARAMETER;
  }

  if (!MagicOk (P->Magic)) {
    DEBUG ((DEBUG_ERROR, "SeaBoot: no SWEETXNU payload at 0x%lx\n", Addr));
    return EFI_NOT_FOUND;
  }

  if ((P->Version != 1) || (P->AdtOff & 7)) {
    return EFI_INCOMPATIBLE_VERSION;
  }

  PaOfVaBase = P->PhysBase - P->VirtBase;       // pa = va + this
  DEBUG ((DEBUG_ERROR, "SeaBoot: phys 0x%lx mem 0x%lx virt 0x%lx entry 0x%lx\n", P->PhysBase, P->MemSize, P->VirtBase, P->EntryVa));

  // the payload must not overlap where the kernel goes
  if ((RdEnd > P->PhysBase) && (RdStart < P->PhysBase + P->MemSize)) {
    DEBUG ((DEBUG_ERROR, "SeaBoot: payload overlaps kernel memory\n"));
    return EFI_UNSUPPORTED;
  }

  for (I = 0; I < P->NSegs; I++) {
    CONST PAYLOAD_SEG  *S = &P->Segs[I];
    UINT8              *Dst = (UINT8 *)(UINTN)(S->VmAddr + PaOfVaBase);

    if (S->FileOff == ~0ULL) {                  // external (in-place) ramdisk: the host staged it there
      continue;
    }

    CopyMem (Dst, (VOID *)(UINTN)(RdStart + S->FileOff), (UINTN)S->FileSize);
    if (S->VmSize > S->FileSize) {
      ZeroMem (Dst + S->FileSize, (UINTN)(S->VmSize - S->FileSize));
    }
  }

  // ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours
  AdtPa = P->KernelEnd + PaOfVaBase;
  CopyMem ((VOID *)(UINTN)AdtPa, (VOID *)(UINTN)(RdStart + P->AdtOff), (UINTN)P->AdtSize);
  BaPa = (AdtPa + P->AdtSize + PAGE_16K - 1) & ~(PAGE_16K - 1);
  Top  = BaPa + PAGE_16K;
  ZeroMem ((VOID *)(UINTN)BaPa, (UINTN)PAGE_16K);

  Ba = (UINT8 *)(UINTN)BaPa;
  *(UINT16 *)(Ba + BA_REVISION) = 2;
  *(UINT16 *)(Ba + BA_VERSION)  = 2;
  *(UINT64 *)(Ba + BA_VIRTBASE) = P->VirtBase;
  *(UINT64 *)(Ba + BA_PHYSBASE) = P->PhysBase;
  *(UINT64 *)(Ba + BA_MEMSIZE)  = P->MemSize;
  *(UINT64 *)(Ba + BA_TOPKDATA) = Top;
  *(UINT64 *)(Ba + BA_VIDEO + 0)  = FB_BASE;
  *(UINT64 *)(Ba + BA_VIDEO + 8)  = 1;
  *(UINT64 *)(Ba + BA_VIDEO + 16) = FB_STRIDE;
  *(UINT64 *)(Ba + BA_VIDEO + 24) = FB_W;
  *(UINT64 *)(Ba + BA_VIDEO + 32) = FB_H;
  *(UINT64 *)(Ba + BA_VIDEO + 40) = 32;
  *(UINT64 *)(Ba + BA_DTP)        = AdtPa - PaOfVaBase;   // XNU reads it with the MMU on: a VA
  *(UINT32 *)(Ba + BA_DTLEN)      = (UINT32)P->AdtSize;
  CopyMem (Ba + BA_CMDLINE, P->CmdLine, BOOT_LINE_LENGTH);
  *(UINT64 *)(Ba + BA_MEMACTUAL)  = P->MemSize;
  (VOID)BA_MACHINETYPE; (VOID)BA_BOOTFLAGS; (VOID)BA_SIZE;

  EntryPa = P->EntryVa + PaOfVaBase;
  DEBUG ((DEBUG_ERROR, "SeaBoot: boot_args 0x%lx top 0x%lx jump 0x%lx\n", BaPa, Top, EntryPa));

  // leave UEFI: ExitBootServices (ArmGicDxe quiesces the GIC), with the usual memory-map-key retry
  {
    UINTN                  MapSize = 0, MapKey, DescSize;
    UINT32                 DescVer;
    EFI_MEMORY_DESCRIPTOR  *Map = NULL;
    EFI_STATUS             Status;
    UINTN                  Try;

    gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    MapSize += 8 * DescSize;
    Map = AllocatePool (MapSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    for (Try = 0; Try < 5; Try++) {
      UINTN  Sz = MapSize;
      Status = gBS->GetMemoryMap (&Sz, Map, &MapKey, &DescSize, &DescVer);
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Status = gBS->ExitBootServices (gImageHandle, MapKey);
      if (!EFI_ERROR (Status)) {
        break;
      }
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  ArmDisableInterrupts ();
  ArmDisableFiq ();
  ArmDisableAsynchronousAbort ();

  // timers off, interrupt groups off: XNU's own GIC and timer drivers start from scratch, as after LK
  __asm__ volatile ("msr cntv_ctl_el0, xzr; msr cntp_ctl_el0, xzr; isb" ::: "memory");
  __asm__ volatile ("msr S3_0_C12_C12_6, xzr; msr S3_0_C12_C12_7, xzr; isb" ::: "memory");   // ICC_IGRPEN0/1_EL1

  // every byte written above must be in RAM before the caches go
  WriteBackInvalidateDataCache ();
  ArmInvalidateInstructionCache ();

  // MMU, data cache and instruction cache off, then jump: registers only from here on (no stack, no data accesses)
  {
    UINT64  Sctlr;

    __asm__ volatile (
      "mrs   %0, sctlr_el1\n"
      "bic   %0, %0, #(1 << 0)\n"       // M
      "bic   %0, %0, #(1 << 2)\n"       // C
      "bic   %0, %0, #(1 << 12)\n"      // I
      "bic   %0, %0, #(1 << 19)\n"      // WXN
      "dsb   sy\n"
      "msr   sctlr_el1, %0\n"
      "isb\n"
      "tlbi  vmalle1\n"
      "ic    iallu\n"
      "dsb   sy\n"
      "isb\n"
      "mov   x0, %2\n"
      "br    %1\n"
      : "=&r" (Sctlr)
      : "r" (EntryPa), "r" (BaPa)
      : "x0", "memory"
      );
  }

  return EFI_DEVICE_ERROR;              // not reached
}
