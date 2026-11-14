/** @file
 *  T158BootControlLib —— 改写 misc 分区控制原厂 U-Boot 的启动目标
 *
 *  结构定义与取证依据见 Include/Library/T158BootControlLib.h。
 **/

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/PartitionInfo.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/T158BootControlLib.h>

/* ------------------------------------------------ 展锐 A/B 元数据 (32 字节) */
#define T158_AB_MAGIC        "BCAB"
#define T158_AB_STRUCT_SIZE  32U
#define T158_AB_CRC_OFFSET   (T158_AB_STRUCT_SIZE - 4)   /* 0x1C */

#pragma pack(1)
typedef struct {
  CHAR8   SlotSuffix[4];    /* "_a\0\0" / "_b\0\0" */
  CHAR8   Magic[4];         /* "BCAB"              */
  UINT8   Version[2];       /* 1, 2                */
  UINT8   Reserved[18];
  UINT32  Crc32;            /* 覆盖前 28 字节      */
} T158_AB_DATA;
#pragma pack()

/* ------------------------------------------ 标准 Android BCB (偏移 0) */
#define T158_BCB_COMMAND_SIZE  32U
#define T158_BCB_SIZE          2048U

#pragma pack(1)
typedef struct {
  CHAR8  Command[T158_BCB_COMMAND_SIZE];   /* "boot-recovery" */
  CHAR8  Status[32];
  CHAR8  Recovery[768];                   /* 传给 recovery 的命令行 */
  CHAR8  Stage[32];
  CHAR8  Reserved[1184];
} T158_BCB_DATA;
#pragma pack()

/* --------------------------------------------------------- 内部辅助 */

/**
  找到 misc 分区所在的设备句柄, 并取得其 DiskIo/BlockIo。

  依赖 PartitionDxe 已为 GPT 分区表里的 misc 分区创建了子设备句柄
  (misc 是裸分区, 没有文件系统, 所以不会有 SimpleFileSystem)。
**/
STATIC
EFI_STATUS
FindMiscPartition (
  OUT EFI_DISK_IO_PROTOCOL  **DiskIo,
  OUT EFI_BLOCK_IO_PROTOCOL **BlockIo
  )
{
  EFI_STATUS                    Status;
  UINTN                         HandleCount;
  EFI_HANDLE                    *Handles;
  UINTN                         Index;
  EFI_PARTITION_INFO_PROTOCOL   *PartInfo;
  EFI_DISK_IO_PROTOCOL          *Io;
  EFI_BLOCK_IO_PROTOCOL         *Bio;

  *DiskIo  = NULL;
  *BlockIo = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    DEBUG ((DEBUG_WARN, "BootCtrl: 没有任何 BlockIo (UFS 驱动未就绪)\n"));
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiPartitionInfoProtocolGuid,
                    (VOID **)&PartInfo
                    );
    if (EFI_ERROR (Status)) {
      continue;                       /* 不是分区子设备 (整盘设备), 跳过 */
    }

    /* 只看 GPT 分区, 按名字找 "misc" */
    if (PartInfo->Type != PARTITION_TYPE_GPT) {
      continue;
    }
    if (StrnCmp (PartInfo->Info.Gpt.PartitionName, L"misc", 4) != 0) {
      continue;
    }

    Status = gBS->HandleProtocol (Handles[Index], &gEfiDiskIoProtocolGuid, (VOID **)&Io);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Status = gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
    if (EFI_ERROR (Status)) {
      continue;
    }

    DEBUG ((DEBUG_INFO, "BootCtrl: 找到 misc 分区 (MediaId=%d)\n", Bio->Media->MediaId));
    *DiskIo  = Io;
    *BlockIo = Bio;
    FreePool (Handles);
    return EFI_SUCCESS;
  }

  FreePool (Handles);
  DEBUG ((DEBUG_WARN, "BootCtrl: 未找到 misc 分区\n"));
  return EFI_NOT_FOUND;
}

/**
  读取 misc 偏移 Offset 处 Length 字节。
**/
STATIC
EFI_STATUS
MiscRead (
  IN  UINTN  Offset,
  IN  UINTN  Length,
  OUT VOID   *Buffer
  )
{
  EFI_STATUS               Status;
  EFI_DISK_IO_PROTOCOL     *DiskIo;
  EFI_BLOCK_IO_PROTOCOL    *BlockIo;

  Status = FindMiscPartition (&DiskIo, &BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return DiskIo->ReadDisk (DiskIo, BlockIo->Media->MediaId, Offset, Length, Buffer);
}

/**
  写 misc 偏移 Offset 处 Length 字节。
**/
STATIC
EFI_STATUS
MiscWrite (
  IN UINTN  Offset,
  IN UINTN  Length,
  IN VOID   *Buffer
  )
{
  EFI_STATUS               Status;
  EFI_DISK_IO_PROTOCOL     *DiskIo;
  EFI_BLOCK_IO_PROTOCOL    *BlockIo;

  Status = FindMiscPartition (&DiskIo, &BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (BlockIo->Media->ReadOnly) {
    DEBUG ((DEBUG_ERROR, "BootCtrl: misc 分区只读\n"));
    return EFI_ACCESS_DENIED;
  }

  return DiskIo->WriteDisk (DiskIo, BlockIo->Media->MediaId, Offset, Length, Buffer);
}

/* ----------------------------------------------------------- 对外接口 */

EFI_STATUS
EFIAPI
T158BootCtrlGetSlot (
  OUT T158_SLOT  *Slot     OPTIONAL,
  OUT CHAR8      Suffix[4] OPTIONAL
  )
{
  EFI_STATUS    Status;
  T158_AB_DATA  Ab;
  UINT32        Crc;
  UINTN         CrcSize;

  Status = MiscRead (T158_MISC_AB_OFFSET, sizeof (Ab), &Ab);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem (Ab.Magic, T158_AB_MAGIC, 4) != 0) {
    DEBUG ((DEBUG_ERROR, "BootCtrl: misc@2048 magic 不是 BCAB (%c%c%c%c)\n",
            Ab.Magic[0], Ab.Magic[1], Ab.Magic[2], Ab.Magic[3]));
    return EFI_NOT_FOUND;
  }

  CrcSize = T158_AB_CRC_OFFSET;
  Status  = gBS->CalculateCrc32 (&Ab, CrcSize, &Crc);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Crc != Ab.Crc32) {
    DEBUG ((DEBUG_ERROR, "BootCtrl: A/B 元数据 CRC 不符 (算得 0x%08X, 分区里 0x%08X)\n",
            Crc, Ab.Crc32));
    return EFI_CRC_ERROR;
  }

  if (Suffix != NULL) {
    CopyMem (Suffix, Ab.SlotSuffix, 4);
  }
  if (Slot != NULL) {
    *Slot = (Ab.SlotSuffix[1] == 'b') ? T158SlotB : T158SlotA;
  }

  DEBUG ((DEBUG_INFO, "BootCtrl: 当前槽位 = %a\n", (CONST CHAR8 *)Ab.SlotSuffix));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
T158BootCtrlSetSlot (
  IN T158_SLOT  Slot
  )
{
  EFI_STATUS    Status;
  T158_AB_DATA  Ab;
  UINTN         CrcSize;

  Status = MiscRead (T158_MISC_AB_OFFSET, sizeof (Ab), &Ab);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem (Ab.Magic, T158_AB_MAGIC, 4) != 0) {
    DEBUG ((DEBUG_ERROR, "BootCtrl: misc@2048 magic 不是 BCAB\n"));
    return EFI_NOT_FOUND;
  }

  /* 只改 slot_suffix, 其余字段原样保留 */
  Ab.SlotSuffix[0] = '_';
  Ab.SlotSuffix[1] = (Slot == T158SlotB) ? 'b' : 'a';
  Ab.SlotSuffix[2] = 0;
  Ab.SlotSuffix[3] = 0;

  CrcSize = T158_AB_CRC_OFFSET;
  Status  = gBS->CalculateCrc32 (&Ab, CrcSize, &Ab.Crc32);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "BootCtrl: 切到 %a (CRC=0x%08X)\n",
          (CONST CHAR8 *)Ab.SlotSuffix, Ab.Crc32));

  return MiscWrite (T158_MISC_AB_OFFSET, sizeof (Ab), &Ab);
}

EFI_STATUS
EFIAPI
T158BootCtrlSetRecoveryBoot (
  IN BOOLEAN  Enable
  )
{
  EFI_STATUS      Status;
  T158_BCB_DATA   Bcb;

  Status = MiscRead (T158_MISC_BCB_OFFSET, sizeof (Bcb), &Bcb);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (Bcb.Command, sizeof (Bcb.Command));
  if (Enable) {
    AsciiStrCpyS (Bcb.Command, sizeof (Bcb.Command), "boot-recovery");
    /* U-Boot 会打印 recovery 命令行, 这里给个可读的来源标记 */
    AsciiStrCpyS (Bcb.Recovery, sizeof (Bcb.Recovery), "recovery\n--from_uefi=1\n");
  } else {
    ZeroMem (Bcb.Recovery, sizeof (Bcb.Recovery));
  }

  DEBUG ((DEBUG_INFO, "BootCtrl: BCB command = %a\n", Bcb.Command));

  return MiscWrite (T158_MISC_BCB_OFFSET, sizeof (Bcb), &Bcb);
}
