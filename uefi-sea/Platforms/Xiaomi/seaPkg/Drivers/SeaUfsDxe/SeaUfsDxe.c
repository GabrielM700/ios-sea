/**
  Redmi Note 12S (MT6781): the UFS the phone boots from, as a read-only EFI_BLOCK_IO_PROTOCOL.

  Port of ios-sea's shim-sea/ufs.c (measured on the phone): the INFRA clock gates are enabled, the UFSHCI is taken over
  at its transfer request list, the device is woken with START STOP UNIT on the UFS Device W-LUN (LK leaves it in
  UFS-PowerDown), and the LUN whose LBA 1 reads "EFI PART" is published (LUN 2 on the sea: the GPT with boot_a, super,
  userdata...). 4 KiB blocks. There is no SCSI WRITE in this file: PartitionDxe and the file systems see a read-only disk.
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

#define UFS_BASE        0x11270000UL
#define REG_VER         0x08
#define REG_HCS         0x30
#define REG_HCE         0x34
#define REG_UTRLBA      0x50
#define REG_UTRLBAU     0x54
#define REG_UTRLDBR     0x58
#define REG_UTRLCLR     0x5c
#define REG_UTRLRSR     0x60

#define HCS_DP          BIT0
#define HCS_UTRLRDY     BIT1

// MT6781 infracfg_ao clock gates (INFRA2 and INFRA3): writing 1 to CLR enables the clock, status bit 1 = gated.
#define INFRA_AO        0x10001000UL
#define INFRA2_CLR      (INFRA_AO + 0xa8)
#define INFRA2_STA      (INFRA_AO + 0xac)
#define INFRA3_CLR      (INFRA_AO + 0xc4)
#define G_UNIPRO_SCK    BIT11
#define G_UNIPRO_TICK   BIT12
#define G_MP_SAP_BCLK   BIT13
#define G_AES_UFS       BIT29
#define G_UFS           BIT30
#define G_UFS_TICK      BIT31
#define G_UFS_AXI       BIT5

#define BLOCK           4096u
#define CHUNK_BLOCKS    64u                     // 256 KiB per command, through the bounce buffer

#pragma pack(1)
typedef struct {
  UINT32 Dw0;       // [31:28] CT, [26:25] DD, [24] I
  UINT32 Dw1;
  UINT32 Dw2;       // [7:0] OCS
  UINT32 Dw3;
  UINT32 UcdBa;
  UINT32 UcdBaU;
  UINT32 Resp;
  UINT32 Prdt;
} UTRD;

typedef struct {
  UINT8  Cmd[512];
  UINT8  Resp[512];
  struct {
    UINT32 AddrLo, AddrHi, Rsvd, Dbc;
  } Prdt[1];
} UCD;
#pragma pack()

typedef struct {
  EFI_BLOCK_IO_PROTOCOL       BlockIo;
  EFI_BLOCK_IO_MEDIA          Media;
  UINT8                       Lun;
} SEA_UFS_DISK;

STATIC UTRD   *mUtrl;       // 32 slots, 1 KiB aligned; only slot 0 is used
STATIC UCD    *mUcd;        // 128 B aligned
STATIC UINT8  *mData;       // bounce buffer, below 4 GiB
STATIC UINT8  mTag;

STATIC
EFI_GUID  mSeaUfsDevGuid = { 0x6a0e4c19, 0x35d8, 0x4b72, { 0x9f, 0x61, 0x0d, 0xa2, 0x7e, 0x84, 0xc3, 0x15 } };

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH        Vendor;
  UINT8                     Lun;
  EFI_DEVICE_PATH_PROTOCOL  End;
} SEA_UFS_DEVPATH;
#pragma pack()

STATIC
UINT32
Rd (UINT32 Off)
{
  return MmioRead32 (UFS_BASE + Off);
}

STATIC
VOID
Wr (UINT32 Off, UINT32 V)
{
  MmioWrite32 (UFS_BASE + Off, V);
}

STATIC
UINT32
Be32 (CONST UINT8 *P)
{
  return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) | ((UINT32)P[2] << 8) | P[3];
}

STATIC
VOID
UfsClocks (VOID)
{
  MmioWrite32 (INFRA2_CLR, G_UFS | G_UFS_TICK | G_UNIPRO_SCK | G_UNIPRO_TICK | G_MP_SAP_BCLK | G_AES_UFS);
  MmioWrite32 (INFRA3_CLR, G_UFS_AXI);
  __asm__ volatile ("dsb sy" ::: "memory");
  DEBUG ((DEBUG_INFO, "SeaUfs: infra2 gates 0x%x\n", MmioRead32 (INFRA2_STA)));
}

/*
  One SCSI command in slot 0. Returns 0 on success: 1 timeout, 2 OCS error, 3 not a RESPONSE UPIU, 4 SCSI status != GOOD.
*/
STATIC
UINT32
Scsi (
  UINT8        Lun,
  CONST UINT8  *Cdb,
  UINTN        CdbLen,
  VOID         *Buf,
  UINT32       Len
  )
{
  UINTN  I;
  UINT64 Pa;

  ZeroMem (mUtrl, sizeof (UTRD));
  ZeroMem (mUcd, sizeof (UCD));

  mUcd->Cmd[0]  = 0x01;                       // COMMAND UPIU
  mUcd->Cmd[1]  = Len ? 0x40 : 0x00;          // flags: data in
  mUcd->Cmd[2]  = Lun;
  mUcd->Cmd[3]  = ++mTag;
  mUcd->Cmd[12] = (UINT8)(Len >> 24);
  mUcd->Cmd[13] = (UINT8)(Len >> 16);
  mUcd->Cmd[14] = (UINT8)(Len >> 8);
  mUcd->Cmd[15] = (UINT8)Len;
  for (I = 0; I < CdbLen; I++) {
    mUcd->Cmd[16 + I] = Cdb[I];
  }

  Pa             = (UINT64)(UINTN)mUcd;
  mUtrl->Dw0     = (1u << 28) | ((Len ? 2u : 0u) << 25) | (1u << 24);
  mUtrl->Dw2     = 0x0f;                      // OCS invalid until the host updates it
  mUtrl->UcdBa   = (UINT32)Pa;
  mUtrl->UcdBaU  = (UINT32)(Pa >> 32);
  mUtrl->Resp    = ((512u / 4) << 16) | (512u / 4);
  if (Len) {
    Pa                   = (UINT64)(UINTN)Buf;
    mUcd->Prdt[0].AddrLo = (UINT32)Pa;
    mUcd->Prdt[0].AddrHi = (UINT32)(Pa >> 32);
    mUcd->Prdt[0].Dbc    = Len - 1;
    mUtrl->Prdt          = ((1024u / 4) << 16) | 1;
  }

  WriteBackInvalidateDataCacheRange (mUcd, sizeof (UCD));
  WriteBackInvalidateDataCacheRange (mUtrl, sizeof (UTRD));
  if (Len) {
    WriteBackInvalidateDataCacheRange (Buf, Len);
  }

  Wr (REG_UTRLDBR, 1);
  for (I = 0; Rd (REG_UTRLDBR) & 1; I++) {
    if (I > 100000) {                         // 100000 x 10 us = 1 s
      Wr (REG_UTRLCLR, ~1u);
      return 1;
    }
    MicroSecondDelay (10);
  }

  InvalidateDataCacheRange (mUtrl, sizeof (UTRD));
  InvalidateDataCacheRange (mUcd, sizeof (UCD));
  if (Len) {
    InvalidateDataCacheRange (Buf, Len);
  }

  if (mUtrl->Dw2 & 0xff) {
    return 2;
  }

  if (mUcd->Resp[0] != 0x21) {
    return 3;
  }

  return mUcd->Resp[7] ? 4 : 0;
}

STATIC
UINT32
ReadBlocksRaw (
  UINT8   Lun,
  UINT32  Lba,
  UINT32  Count,
  VOID    *Buf
  )
{
  UINT8 Cdb[10] = { 0x28, 0, (UINT8)(Lba >> 24), (UINT8)(Lba >> 16), (UINT8)(Lba >> 8), (UINT8)Lba, 0,
                    (UINT8)(Count >> 8), (UINT8)Count, 0 };

  return Scsi (Lun, Cdb, sizeof (Cdb), Buf, Count * BLOCK);
}

STATIC
EFI_STATUS
EFIAPI
SeaUfsReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SeaUfsReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                 MediaId,
  IN  EFI_LBA                Lba,
  IN  UINTN                  BufferSize,
  OUT VOID                   *Buffer
  )
{
  SEA_UFS_DISK  *Disk = (SEA_UFS_DISK *)This;
  UINTN         Blocks;
  UINT8         *Out = Buffer;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }

  if (BufferSize % BLOCK) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (MediaId != Disk->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  Blocks = BufferSize / BLOCK;
  if (Lba + Blocks - 1 > Disk->Media.LastBlock) {
    return EFI_INVALID_PARAMETER;
  }

  while (Blocks) {
    UINT32  N = (UINT32)(Blocks > CHUNK_BLOCKS ? CHUNK_BLOCKS : Blocks);
    UINT32  Rc = 1;
    UINTN   Try;

    for (Try = 0; Try < 3 && Rc; Try++) {
      Rc = ReadBlocksRaw (Disk->Lun, (UINT32)Lba, N, mData);
    }

    if (Rc) {
      DEBUG ((DEBUG_ERROR, "SeaUfs: read lba 0x%lx x%u failed (%u)\n", Lba, N, Rc));
      return EFI_DEVICE_ERROR;
    }

    CopyMem (Out, mData, (UINTN)N * BLOCK);
    Out    += (UINTN)N * BLOCK;
    Lba    += N;
    Blocks -= N;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SeaUfsWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  return EFI_WRITE_PROTECTED;       // read-only by design
}

STATIC
EFI_STATUS
EFIAPI
SeaUfsFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
Alloc (VOID)
{
  EFI_PHYSICAL_ADDRESS  A = 0xFFFFFFFFULL;
  EFI_STATUS            S;

  // everything below 4 GiB (the HCI is used with 32-bit buffer addresses)
  S = gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesData, 1 + 1 + (CHUNK_BLOCKS), &A);
  if (EFI_ERROR (S)) {
    return FALSE;
  }

  ZeroMem ((VOID *)(UINTN)A, EFI_PAGES_TO_SIZE (2 + CHUNK_BLOCKS));
  mUtrl = (UTRD *)(UINTN)A;                              // page 0: 32 slots, 1 KiB aligned
  mUcd  = (UCD *)(UINTN)(A + EFI_PAGE_SIZE);             // page 1: 128 B aligned
  mData = (UINT8 *)(UINTN)(A + 2 * EFI_PAGE_SIZE);       // then 256 KiB
  return TRUE;
}

EFI_STATUS
EFIAPI
SeaUfsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32           Ver, Hce, Hcs, Ocs, Lun, Rc, I;
  UINT64           List;
  SEA_UFS_DISK     *Disk;
  SEA_UFS_DEVPATH  *Path;
  EFI_HANDLE       Handle = NULL;
  EFI_STATUS       Status;
  UINT8            Ssu[6]   = { 0x1b, 0, 0, 0, 0x10, 0 };
  UINT8            Tur[6]   = { 0 };
  UINT8            Cap10[10] = { 0x25 };
  UINT8            Rd2[10]  = { 0x28, 0, 0, 0, 0, 0, 0, 0, 2, 0 };

  UfsClocks ();
  Ver = Rd (REG_VER);
  Hce = Rd (REG_HCE);
  Hcs = Rd (REG_HCS);
  DEBUG ((DEBUG_ERROR, "SeaUfs: ver=0x%x hce=0x%x hcs=0x%x\n", Ver, Hce, Hcs));
  if ((Ver == 0) || (Ver == 0xffffffffu) || !(Hce & 1) || !(Hcs & HCS_DP) || !(Hcs & HCS_UTRLRDY) ||
      Rd (REG_UTRLDBR)) {
    DEBUG ((DEBUG_ERROR, "SeaUfs: host not ready, no disk\n"));
    return EFI_NOT_FOUND;
  }

  if (!Alloc ()) {
    return EFI_OUT_OF_RESOURCES;
  }

  // take over the transfer request list: stop, point at ours, run
  List = (UINT64)(UINTN)mUtrl;
  Wr (REG_UTRLRSR, 0);
  Wr (REG_UTRLBA, (UINT32)List);
  Wr (REG_UTRLBAU, (UINT32)(List >> 32));
  Wr (REG_UTRLRSR, 1);
  if ((Rd (REG_UTRLBA) != (UINT32)List) || (Rd (REG_UTRLBAU) != (UINT32)(List >> 32)) || !(Rd (REG_UTRLRSR) & 1)) {
    DEBUG ((DEBUG_ERROR, "SeaUfs: list base not taken\n"));
    return EFI_DEVICE_ERROR;
  }

  // LK leaves the device in UFS-PowerDown: START STOP UNIT (Active) on the UFS Device W-LUN (UPIU LUN 0xD0)
  Rc = 1;
  for (I = 0; I < 3 && Rc; I++) {
    Rc = Scsi (0xd0, Ssu, sizeof (Ssu), NULL, 0);
  }

  DEBUG ((DEBUG_ERROR, "SeaUfs: SSU rc=%u\n", Rc));
  MicroSecondDelay (100000);

  for (Lun = 0; Lun < 4; Lun++) {
    Rc = 1;
    for (I = 0; I < 5 && Rc; I++) {
      Rc = Scsi ((UINT8)Lun, Tur, sizeof (Tur), NULL, 0);
      if (Rc) {
        MicroSecondDelay (20000);
      }
    }

    if (Rc) {
      continue;
    }

    if (Scsi ((UINT8)Lun, Cap10, sizeof (Cap10), mData, 8)) {
      continue;
    }

    Ocs = Be32 (mData);                         // last LBA
    if ((Be32 (mData + 4) != BLOCK) ||
        Scsi ((UINT8)Lun, Rd2, sizeof (Rd2), mData, 2 * BLOCK) ||
        (CompareMem (mData + BLOCK, "EFI PART", 8) != 0))
    {
      continue;                                 // not the GPT LUN
    }

    DEBUG ((DEBUG_ERROR, "SeaUfs: LUN%u GPT, last lba 0x%x\n", Lun, Ocs));
    Disk = AllocateZeroPool (sizeof (*Disk));
    Path = AllocateZeroPool (sizeof (*Path));
    if ((Disk == NULL) || (Path == NULL)) {
      return EFI_OUT_OF_RESOURCES;
    }

    Disk->Lun                        = (UINT8)Lun;
    Disk->Media.MediaId              = 1;
    Disk->Media.MediaPresent         = TRUE;
    Disk->Media.LogicalPartition     = FALSE;
    Disk->Media.ReadOnly             = TRUE;
    Disk->Media.WriteCaching         = FALSE;
    Disk->Media.BlockSize            = BLOCK;
    Disk->Media.IoAlign              = 4;
    Disk->Media.LastBlock            = Ocs;
    Disk->BlockIo.Revision           = EFI_BLOCK_IO_PROTOCOL_REVISION;
    Disk->BlockIo.Media              = &Disk->Media;
    Disk->BlockIo.Reset              = SeaUfsReset;
    Disk->BlockIo.ReadBlocks         = SeaUfsReadBlocks;
    Disk->BlockIo.WriteBlocks        = SeaUfsWriteBlocks;
    Disk->BlockIo.FlushBlocks        = SeaUfsFlushBlocks;

    Path->Vendor.Header.Type         = HARDWARE_DEVICE_PATH;
    Path->Vendor.Header.SubType      = HW_VENDOR_DP;
    SetDevicePathNodeLength (&Path->Vendor.Header, sizeof (VENDOR_DEVICE_PATH) + 1);
    CopyGuid (&Path->Vendor.Guid, &mSeaUfsDevGuid);
    Path->Lun                        = (UINT8)Lun;
    SetDevicePathEndNode (&Path->End);

    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Handle,
                    &gEfiDevicePathProtocolGuid, Path,
                    &gEfiBlockIoProtocolGuid, &Disk->BlockIo,
                    NULL
                    );
    return Status;
  }

  DEBUG ((DEBUG_ERROR, "SeaUfs: no GPT LUN found\n"));
  return EFI_NOT_FOUND;
}
