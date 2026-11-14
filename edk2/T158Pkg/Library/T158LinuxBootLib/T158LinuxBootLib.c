/** @file
 *  T158LinuxBootLib —— UEFI 直接链式启动 Android/Linux kernel
 *
 *  结构与流程见 Include/Library/T158LinuxBootLib.h。三种 kernel 来源:
 *    A. T158LinuxBootFromPartition: kernel 在 boot.img 的 kernel 段
 *    B. T158LinuxBootFromPayload:   kernel 打包在 UEFI payload 的 ramdisk 段,
 *       UEFI 从自己所在分区读出 (自包含, 不依赖 boot_b)
 *    C. T158LinuxBootFromEsp:       kernel/ramdisk/dtb 是 FAT32 分区上的文件,
 *       经 Fat 驱动 (SimpleFileSystem) 读出
 *  三者最终都走 LinuxBootCommon (initrd 拼接 -> DTB -> EBS -> 跳 kernel)。
 *
 *  boot.img v3/v4 header (44 字节固定头, page0 共 4096):
 *    0x00 magic[8]="ANDROID!"  0x08 kernel_size  0x0C ramdisk_size
 *    0x10 os_version           0x14 header_size  0x18 reserved[4]
 *    0x28 header_version (3/4)   page_size 固定 4096 (v3/v4 规范)
 *    数据区: 4096 起 kernel, ALIGN(4096+kernel_size,4096) 起 ramdisk
 *
 *  vendor_boot v3/v4 header:
 *    0x00 magic[8]="VNDRBOOT"  0x08 header_version  0x0C page_size
 *    0x10 kernel_addr          0x14 ramdisk_addr    0x18 vendor_ramdisk_size
 *    数据区: page_size(4096) 起是 vendor_ramdisk (本库整块取用)
 *
 *  取证: 本机 boot_a kernel=46.7MB 未压缩 MZ/ARMd (flags 0x0A 可任意放置),
 *        ramdisk=0; init_boot_a ramdisk=2MB; vendor_boot_a vendor_ramdisk=80MB。
 **/

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Guid/HobList.h>
#include <libfdt.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/T158LinuxBootLib.h>

extern EFI_GUID  gT158FdtHobGuid;

/* kernel 入口汇编 (AArch64/EnterKernel.S): x0=Entry, x1=DTB, 不返回 */
VOID
EFIAPI
T158EnterKernel (
  IN UINT64  Entry,
  IN UINT64  DtBase
  );

/* ----------------------------------------------------------- 常量/结构 */

#define T158_KERNEL_LOAD_BASE   0x80200000ULL  /* 2MB 对齐 */
#define T158_LINUX_ALIGN        SIZE_2MB
#define T158_BOOT_MAGIC         "ANDROID!"
#define T158_VNDR_MAGIC         "VNDRBOOT"
#define T158_MAX_KERNEL_SIZE    (160U * SIZE_1MB)
#define T158_MAX_INITRD_SIZE    (256U * SIZE_1MB)

/* ESP 路径的文件名 (FAT 大小写不敏感) */
#define T158_ESP_KERNEL   L"kernel"
#define T158_ESP_RAMDISK  L"ramdisk"
#define T158_ESP_DTB      L"dtb"

#pragma pack(1)
typedef struct {
  CHAR8   Magic[8];
  UINT32  KernelSize;                      /* 0x08 */
  UINT32  RamdiskSize;                     /* 0x0C */
  UINT32  OsVersion;                       /* 0x10 */
  UINT32  HeaderSize;                      /* 0x14 = 1580 */
  UINT32  Reserved[4];                     /* 0x18 */
  UINT32  HeaderVersion;                   /* 0x28 = 3 或 4 */
  UINT8   Tail[4096 - 0x2C];               /* cmdline 等, 凑满一页 */
} T158_BOOT_HDR;

typedef struct {
  CHAR8   Magic[8];
  UINT32  HeaderVersion;                   /* 0x08 */
  UINT32  Page;                            /* 0x0C */
  UINT32  KernelAddr;                      /* 0x10 */
  UINT32  RamdiskAddr;                     /* 0x14 */
  UINT32  VendorRamdiskSize;               /* 0x18 */
  UINT8   Tail[4096 - 0x1C];
} T158_VNDR_HDR;
#pragma pack()

/* --------------------------------------------------- 分区查找/读取 */

/**
  按 GPT 分区名找 DiskIo/BlockIo。
**/
STATIC
EFI_STATUS
FindPartitionByName (
  IN  CONST CHAR16           *Name,
  OUT EFI_DISK_IO_PROTOCOL   **DiskIo,
  OUT EFI_BLOCK_IO_PROTOCOL  **BlockIo
  )
{
  EFI_STATUS                   Status;
  UINTN                        HandleCount;
  EFI_HANDLE                   *Handles;
  UINTN                        Index;
  EFI_PARTITION_INFO_PROTOCOL  *PartInfo;
  UINTN                        NameLen;

  NameLen = StrLen (Name);

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    DEBUG ((DEBUG_WARN, "LinuxBoot: 无 BlockIo 设备 (UFS 驱动未就绪)\n"));
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPartitionInfoProtocolGuid, (VOID **)&PartInfo);
    if (EFI_ERROR (Status) || (PartInfo->Type != PARTITION_TYPE_GPT)) {
      continue;
    }
    if (StrnCmp (PartInfo->Info.Gpt.PartitionName, Name, NameLen + 1) != 0) {
      continue;
    }
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiDiskIoProtocolGuid, (VOID **)DiskIo)) &&
        !EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid, (VOID **)BlockIo)))
    {
      DEBUG ((DEBUG_INFO, "LinuxBoot: 找到分区 %s\n", Name));
      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);
  DEBUG ((DEBUG_WARN, "LinuxBoot: 分区 %s 不存在\n", Name));
  return EFI_NOT_FOUND;
}

/**
  DiskIo 读取任意偏移 (绕开块对齐要求)。
**/
STATIC
EFI_STATUS
PartRead (
  IN EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN UINT64                 Offset,
  IN UINTN                  Size,
  OUT VOID                  *Buffer
  )
{
  return DiskIo->ReadDisk (DiskIo, BlockIo->Media->MediaId, Offset, Size, Buffer);
}

/**
  校验内存中的 arm64 未压缩 Image: "MZ" + 0x38 处 "ARMd"。
**/
STATIC
BOOLEAN
IsArm64Image (
  IN UINT64  LoadBase
  )
{
  return (BOOLEAN)((((UINT32 *)(UINTN)LoadBase)[0] == 0x644D5A4D) &&
                   (CompareMem ((VOID *)(UINTN)(LoadBase + 0x38), "ARMd", 4) == 0));
}

/* --------------------------------------------------- initrd 拼接 */

typedef struct {
  UINT8   *Base;      /* 当前写入位置 */
  UINT8   *Limit;
  UINTN   Total;      /* 累计字节数 */
} T158_INITRD_WRITER;

/**
  读一个分区的某段数据追加进 initrd。
**/
STATIC
EFI_STATUS
AppendPartRange (
  IN T158_INITRD_WRITER  *W,
  IN CONST CHAR16        *PartName,
  IN UINT64              Offset,
  IN UINTN               Size
  )
{
  EFI_STATUS             Status;
  EFI_DISK_IO_PROTOCOL   *DiskIo;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;

  if ((W->Base + Size) > W->Limit) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: initrd 空间不足 (%dKB)\n", (UINT32)(Size >> 10)));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = FindPartitionByName (PartName, &DiskIo, &BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PartRead (DiskIo, BlockIo, Offset, Size, W->Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  W->Base  += Size;
  W->Total += Size;
  DEBUG ((DEBUG_INFO, "LinuxBoot: initrd += %s @0x%lX +%dKB (累计 %dKB)\n",
          PartName, Offset, (UINT32)(Size >> 10), (UINT32)(W->Total >> 10)));
  return EFI_SUCCESS;
}

/**
  追加 init_boot_{Slot} 的 ramdisk (回退 init_boot_a)。
  缺分区不算错误 (kernel 自带 rootfs 的镜像没有 init_boot)。
  SlotName 为 NULL 表示不拼 (ESP 路径, ramdisk 用户已自行组合)。
**/
STATIC
VOID
AppendInitBoot (
  IN T158_INITRD_WRITER  *W,
  IN CONST CHAR16        *SlotName
  )
{
  EFI_STATUS             Status;
  T158_BOOT_HDR          Hdr;
  EFI_DISK_IO_PROTOCOL   *DiskIo;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  CHAR16                 PartName[20];

  if (SlotName == NULL) {
    return;
  }

  UnicodeSPrint (PartName, sizeof (PartName), L"init_boot_%s", SlotName);
  Status = FindPartitionByName (PartName, &DiskIo, &BlockIo);
  if (EFI_ERROR (Status) && (SlotName[0] != L'a')) {
    StrCpyS (PartName, sizeof (PartName) / sizeof (CHAR16), L"init_boot_a");
    Status = FindPartitionByName (PartName, &DiskIo, &BlockIo);
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "LinuxBoot: 没有 init_boot, 跳过 GKI ramdisk\n"));
    return;
  }

  Status = PartRead (DiskIo, BlockIo, 0, sizeof (Hdr), &Hdr);
  if (EFI_ERROR (Status) || (CompareMem (Hdr.Magic, T158_BOOT_MAGIC, 8) != 0)) {
    DEBUG ((DEBUG_WARN, "LinuxBoot: %s 不是 ANDROID!\n", PartName));
    return;
  }
  if ((Hdr.RamdiskSize == 0) || (Hdr.RamdiskSize > T158_MAX_INITRD_SIZE)) {
    return;
  }

  /* init_boot kernel_size=0, ramdisk 从 4096 起 */
  AppendPartRange (W, PartName, 4096, Hdr.RamdiskSize);
}

/**
  追加 vendor_boot_{Slot} (回退 vendor_boot_a) 的 vendor_ramdisk。
  SlotName 为 NULL 表示不拼 (同上)。
**/
STATIC
VOID
AppendVendorBoot (
  IN T158_INITRD_WRITER  *W,
  IN CONST CHAR16        *SlotName
  )
{
  EFI_STATUS             Status;
  T158_VNDR_HDR          Hdr;
  EFI_DISK_IO_PROTOCOL   *DiskIo;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  CHAR16                 PartName[20];

  if (SlotName == NULL) {
    return;
  }

  UnicodeSPrint (PartName, sizeof (PartName), L"vendor_boot_%s", SlotName);
  Status = FindPartitionByName (PartName, &DiskIo, &BlockIo);
  if (EFI_ERROR (Status) && (SlotName[0] != L'a')) {
    StrCpyS (PartName, sizeof (PartName) / sizeof (CHAR16), L"vendor_boot_a");
    Status = FindPartitionByName (PartName, &DiskIo, &BlockIo);
  }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "LinuxBoot: 没有 vendor_boot, initrd 缺 vendor ramdisk\n"));
    return;
  }

  Status = PartRead (DiskIo, BlockIo, 0, sizeof (Hdr), &Hdr);
  if (EFI_ERROR (Status) || (CompareMem (Hdr.Magic, T158_VNDR_MAGIC, 8) != 0)) {
    DEBUG ((DEBUG_WARN, "LinuxBoot: %s 不是 VNDRBOOT\n", PartName));
    return;
  }
  if ((Hdr.VendorRamdiskSize == 0) || (Hdr.VendorRamdiskSize > T158_MAX_INITRD_SIZE)) {
    DEBUG ((DEBUG_WARN, "LinuxBoot: %s vendor_ramdisk_size=%d 异常\n", PartName, Hdr.VendorRamdiskSize));
    return;
  }

  AppendPartRange (W, PartName, 4096, Hdr.VendorRamdiskSize);
}

/* --------------------------------------------------- DTB 处理 */

/**
  准备交给 kernel 的 DTB: 以 DtSrcBase (可为 0, 则从 GUID HOB 拿 SEC 保存的
  U-Boot DTB) 为源, fdt_open_into 到新 buffer (需要 spare 空间),
  往 /chosen 写 initrd 属性。
**/
STATIC
EFI_STATUS
PrepareDeviceTree (
  IN  UINT64  DtSrcBase,      /* 0 = 用 GUID HOB 里的 */
  IN  UINT64  InitrdBase,
  IN  UINT64  InitrdEnd,
  OUT UINT64  *DtPhys,
  OUT UINTN   *DtSize
  )
{
  EFI_HOB_GUID_TYPE  *Hob;
  VOID               *NewFdt;
  UINTN              NewFdtSize;
  UINT64             SrcBase;
  INT32              Node;
  INT32              Ret;

  if (DtSrcBase != 0) {
    SrcBase = DtSrcBase;
  } else {
    Hob = (EFI_HOB_GUID_TYPE *)GetFirstGuidHob (&gT158FdtHobGuid);
    if (Hob == NULL) {
      DEBUG ((DEBUG_ERROR, "LinuxBoot: 没有 DTB HOB (SEC 未传?)\n"));
      return EFI_NOT_FOUND;
    }
    if (GET_GUID_HOB_DATA_SIZE (Hob) != sizeof (UINT64)) {
      return EFI_NOT_FOUND;
    }
    SrcBase = *(UINT64 *)GET_GUID_HOB_DATA (Hob);
  }
  DEBUG ((DEBUG_INFO, "LinuxBoot: DTB @0x%lX (totalsize=%d)\n",
          SrcBase, fdt_totalsize ((VOID *)(UINTN)SrcBase)));

  /* 新 buffer: 原大小 + 64KB spare, 供 fdt_setprop 追加属性 */
  NewFdtSize = ALIGN_VALUE (fdt_totalsize ((VOID *)(UINTN)SrcBase) + SIZE_64KB, SIZE_4KB);
  NewFdt     = AllocatePages (EFI_SIZE_TO_PAGES (NewFdtSize));
  if (NewFdt == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Ret = fdt_open_into ((VOID *)(UINTN)SrcBase, NewFdt, NewFdtSize);
  if (Ret != 0) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: fdt_open_into 失败 (%d)\n", Ret));
    return EFI_INVALID_PARAMETER;
  }

  Node = fdt_path_offset (NewFdt, "/chosen");
  if (Node < 0) {
    Node = fdt_add_subnode (NewFdt, 0, "chosen");
  }
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: /chosen 节点失败 (%d)\n", Node));
    return EFI_INVALID_PARAMETER;
  }

  fdt_setprop_u64 (NewFdt, Node, "linux,initrd-start", InitrdBase);
  fdt_setprop_u64 (NewFdt, Node, "linux,initrd-end", InitrdEnd);
  DEBUG ((DEBUG_INFO, "LinuxBoot: /chosen initrd [0x%lX, 0x%lX)\n", InitrdBase, InitrdEnd));

  *DtPhys = (UINT64)(UINTN)NewFdt;
  *DtSize = NewFdtSize;
  return EFI_SUCCESS;
}

/* --------------------------------------------------- 公共启动尾段 */

/**
  kernel 已加载到 KernelLoad 后的公共流程:
  拼 initrd -> DTB 塞 initrd 属性 -> cache 回写 -> ExitBootServices -> 跳 kernel。
  成功不返回。

  @param[in] KernelLoad        kernel 物理地址 (已校验 ARMd)
  @param[in] KernelSize        kernel 大小
  @param[in] BootPartName      boot 分区名 (读其自带 ramdisk 用; 可 NULL)
  @param[in] BootRamdiskOffset boot.img 内 ramdisk 段偏移 (BootRamdiskSize>0 时有效)
  @param[in] BootRamdiskSize   boot.img 自带 ramdisk 大小 (TWRP 有; 原厂为 0)
  @param[in] PreInitrdBase     已在内存里的 initrd (ESP 路径的 ramdisk 文件; 0=无)
  @param[in] PreInitrdSize     其大小
  @param[in] SlotName          L"a"/L"b" 决定 init_boot_x/vendor_boot_x; NULL=不拼
  @param[in] DtSrcBase         DTB 覆盖源 (ESP 的 dtb 文件; 0=用 GUID HOB)
**/
STATIC
EFI_STATUS
LinuxBootCommon (
  IN UINT64        KernelLoad,
  IN UINTN         KernelSize,
  IN CONST CHAR16  *BootPartName,
  IN UINT64        BootRamdiskOffset,
  IN UINTN         BootRamdiskSize,
  IN UINT64        PreInitrdBase,
  IN UINTN         PreInitrdSize,
  IN CONST CHAR16  *SlotName,
  IN UINT64        DtSrcBase
  )
{
  EFI_STATUS             Status;
  UINT64                 InitrdBase;
  UINT8                  *InitrdBuf;
  UINTN                  InitrdMax;
  UINT64                 InitrdEnd;
  T158_INITRD_WRITER     W;
  UINT64                 DtBase;
  UINTN                  DtSize;
  UINT64                 Entry;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINT64                 FreeBase;
  UINTN                  FreePages;

  /* 1. initrd: kernel 之上 2MB 对齐, 预留 256MB */
  InitrdBase = ALIGN_VALUE (KernelLoad + KernelSize, T158_LINUX_ALIGN);
  InitrdMax  = T158_MAX_INITRD_SIZE;
  Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES (InitrdMax), &InitrdBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: initrd 区 0x%lX 分配失败\n", InitrdBase));
    return Status;
  }
  InitrdBuf = (UINT8 *)(UINTN)InitrdBase;

  W.Base  = InitrdBuf;
  W.Limit = InitrdBuf + InitrdMax;
  W.Total = 0;

  /* 1a0. ESP 路径: ramdisk 文件已整读进内存, 拷进预留区 */
  if ((PreInitrdBase != 0) && (PreInitrdSize > 0)) {
    if ((W.Base + PreInitrdSize) > W.Limit) {
      DEBUG ((DEBUG_ERROR, "LinuxBoot: initrd 空间不足 (%dKB)\n", (UINT32)(PreInitrdSize >> 10)));
      return EFI_OUT_OF_RESOURCES;
    }
    CopyMem (W.Base, (VOID *)(UINTN)PreInitrdBase, PreInitrdSize);
    W.Base  += PreInitrdSize;
    W.Total += PreInitrdSize;
    gBS->FreePages (PreInitrdBase, EFI_SIZE_TO_PAGES (PreInitrdSize));
    DEBUG ((DEBUG_INFO, "LinuxBoot: initrd += 预载 %dKB (累计 %dKB)\n",
            (UINT32)(PreInitrdSize >> 10), (UINT32)(W.Total >> 10)));
  }

  /* 1a. boot 分区自带的 ramdisk (TWRP 有; 原厂 boot_x 为 0; payload 路径无) */
  if ((BootPartName != NULL) && (BootRamdiskSize > 0)) {
    Status = AppendPartRange (&W, BootPartName, BootRamdiskOffset, BootRamdiskSize);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  /* 1b. init_boot / vendor_boot 的 ramdisk (独立分区, 不依赖 boot 槽) */
  AppendInitBoot (&W, SlotName);
  AppendVendorBoot (&W, SlotName);

  if (W.Total == 0) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: initrd 为空, kernel 必 panic, 放弃\n"));
    return EFI_NOT_FOUND;
  }
  InitrdEnd = InitrdBase + W.Total;

  /* 退还 initrd 预留区里没用到的页 (失败不影响) */
  FreeBase  = ALIGN_VALUE (InitrdBase + W.Total, SIZE_4KB);
  FreePages = EFI_SIZE_TO_PAGES (InitrdMax) - (UINTN)((FreeBase - InitrdBase) >> 12);
  if (FreePages > 0) {
    gBS->FreePages (FreeBase, FreePages);
  }

  /* 2. DTB + initrd 属性 */
  Status = PrepareDeviceTree (DtSrcBase, InitrdBase, InitrdEnd, &DtBase, &DtSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Entry = KernelLoad;
  Print (L"\nBooting Linux kernel @0x%lX  initrd 0x%lX (%dKB)  DTB 0x%lX\n",
         Entry, InitrdBase, (UINT32)(W.Total >> 10), DtBase);
  gBS->Stall (2000000);           /* 给用户 2 秒看清 */

  /* 3. 把将交给 kernel 的内存回写+失效 (跳转汇编关 cache 后不再有回写机会) */
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Entry, KernelSize);
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)InitrdBase, W.Total);
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)DtBase, DtSize);

  /* 4. GetMemoryMap -> ExitBootServices (标准重试模板) */
  MapSize = SIZE_64KB;
  Map     = AllocatePool (MapSize);
  if (Map == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  while (TRUE) {
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      gBS->FreePool (Map);
      MapSize *= 2;
      Map      = AllocatePool (MapSize);
      if (Map == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }
      continue;
    }
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Status = gBS->ExitBootServices (gImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      break;
    }
    /* MapKey 过期: map 变了, 重拿一次 */
  }

  /* 5. 跳 kernel (x0=DTB, x1-x3=0)。不再返回。 */
  T158EnterKernel (Entry, DtBase);

  return EFI_DEVICE_ERROR;        /* 不可达 */
}

/* --------------------------------------------------- 路径 A: 标准 boot.img */

EFI_STATUS
EFIAPI
T158LinuxBootFromPartition (
  IN CONST CHAR16  *BootPartitionName
  )
{
  EFI_STATUS             Status;
  EFI_DISK_IO_PROTOCOL   *DiskIo;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  T158_BOOT_HDR          Hdr;
  UINT64                 KernelLoad;
  UINT64                 RamdiskOffset;
  CONST CHAR16           *SlotName;

  KernelLoad = T158_KERNEL_LOAD_BASE;

  /* 1. 读 boot.img header */
  Status = FindPartitionByName (BootPartitionName, &DiskIo, &BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PartRead (DiskIo, BlockIo, 0, sizeof (Hdr), &Hdr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (CompareMem (Hdr.Magic, T158_BOOT_MAGIC, 8) != 0) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: %s 不是 ANDROID! boot.img\n", BootPartitionName));
    return EFI_NOT_FOUND;
  }
  if ((Hdr.HeaderVersion != 3) && (Hdr.HeaderVersion != 4)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: header_version=%d, 只支持 v3/v4\n", Hdr.HeaderVersion));
    return EFI_UNSUPPORTED;
  }
  if ((Hdr.KernelSize == 0) || (Hdr.KernelSize > T158_MAX_KERNEL_SIZE)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: kernel_size=%d 异常\n", Hdr.KernelSize));
    return EFI_NOT_FOUND;
  }
  DEBUG ((DEBUG_INFO, "LinuxBoot: %s v%d kernel=%dKB ramdisk=%dKB\n",
          BootPartitionName, Hdr.HeaderVersion, Hdr.KernelSize >> 10, Hdr.RamdiskSize >> 10));

  /* 2. kernel 加载到固定 2MB 对齐地址 (flags 0x0A: 可任意放置) */
  Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES ((UINTN)Hdr.KernelSize), &KernelLoad);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: kernel 地址 0x%lX 分配失败\n", KernelLoad));
    return Status;
  }
  Status = PartRead (DiskIo, BlockIo, 4096, Hdr.KernelSize, (VOID *)(UINTN)KernelLoad);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  /* 校验 arm64 Image: "MZ" (0x644D5A4D) + 0x38 处 "ARMd" (未压缩) */
  if (!IsArm64Image (KernelLoad)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: %s kernel 不是未压缩 ARMd Image\n", BootPartitionName));
    return EFI_NOT_FOUND;
  }

  RamdiskOffset = ALIGN_VALUE ((UINT64)4096 + Hdr.KernelSize, SIZE_4KB);
  SlotName      = (BootPartitionName[StrLen (BootPartitionName) - 1] == L'b') ? L"b" : L"a";

  /* 3. 公共尾段 (boot.img 自带 ramdisk 会在公共流程里追加, TWRP 需要) */
  return LinuxBootCommon (
           KernelLoad, Hdr.KernelSize,
           BootPartitionName, RamdiskOffset, Hdr.RamdiskSize,
           0, 0,
           SlotName,
           0
           );
}

/* --------------------------------------------------- 路径 B: payload 内嵌 */

EFI_STATUS
EFIAPI
T158LinuxBootFromPayload (
  IN CONST CHAR16  *SelfPartitionName
  )
{
  EFI_STATUS             Status;
  EFI_DISK_IO_PROTOCOL   *DiskIo;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  T158_BOOT_HDR          Hdr;
  UINT64                 KernelLoad;
  UINT64                 EmbOffset;
  CONST CHAR16           *SlotName;

  KernelLoad = T158_KERNEL_LOAD_BASE;

  /* 1. 读自身 payload 的 boot.img header */
  Status = FindPartitionByName (SelfPartitionName, &DiskIo, &BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PartRead (DiskIo, BlockIo, 0, sizeof (Hdr), &Hdr);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (CompareMem (Hdr.Magic, T158_BOOT_MAGIC, 8) != 0) {
    return EFI_NOT_FOUND;
  }
  if ((Hdr.HeaderVersion != 3) && (Hdr.HeaderVersion != 4)) {
    return EFI_UNSUPPORTED;
  }

  /* 2. payload 特征: kernel 段是 UEFI FD (无 ARMd), 安卓 kernel 在 ramdisk 段。
   *    打包工具 (mkpayload bootimg -k) 保证: kernel_size=UEFI, ramdisk_size=安卓 kernel。 */
  if ((Hdr.RamdiskSize == 0) || (Hdr.RamdiskSize > T158_MAX_KERNEL_SIZE)) {
    DEBUG ((DEBUG_INFO, "LinuxBoot: %s 无内嵌安卓 kernel (ramdisk_size=%d)\n",
            SelfPartitionName, Hdr.RamdiskSize));
    return EFI_NOT_FOUND;
  }
  EmbOffset = ALIGN_VALUE ((UINT64)4096 + Hdr.KernelSize, SIZE_4KB);

  /* 3. 安卓 kernel 加载到固定 2MB 对齐地址 */
  Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesData, EFI_SIZE_TO_PAGES ((UINTN)Hdr.RamdiskSize), &KernelLoad);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: kernel 地址 0x%lX 分配失败\n", KernelLoad));
    return Status;
  }
  Status = PartRead (DiskIo, BlockIo, EmbOffset, Hdr.RamdiskSize, (VOID *)(UINTN)KernelLoad);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  /* 校验是未压缩 ARMd Image —— 防止把任意数据当 kernel 跳过去 */
  if (!IsArm64Image (KernelLoad)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: %s ramdisk 段不是 ARMd Image\n", SelfPartitionName));
    return EFI_NOT_FOUND;
  }
  DEBUG ((DEBUG_INFO, "LinuxBoot: payload 内嵌 kernel %dKB @%s 0x%lX\n",
          Hdr.RamdiskSize >> 10, SelfPartitionName, EmbOffset));

  /* 4. 公共尾段。槽位跟随分区名尾字符。 */
  SlotName = (SelfPartitionName[StrLen (SelfPartitionName) - 1] == L'b') ? L"b" : L"a";

  return LinuxBootCommon (
           KernelLoad, Hdr.RamdiskSize,
           NULL, 0, 0,                /* 无 boot 分区自带 ramdisk */
           0, 0,
           SlotName,
           0
           );
}

/* --------------------------------------------------- 路径 C: FAT32 ESP 文件 */

/**
  按 GPT 分区名找 FAT 卷 (SimpleFileSystem) 并 OpenVolume。
**/
STATIC
EFI_STATUS
FindEspVolume (
  IN  CONST CHAR16                    *Name,
  OUT EFI_FILE_PROTOCOL               **Root
  )
{
  EFI_STATUS                       Status;
  UINTN                            HandleCount;
  EFI_HANDLE                       *Handles;
  UINTN                            Index;
  EFI_PARTITION_INFO_PROTOCOL      *PartInfo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Sfs;
  UINTN                            NameLen;

  NameLen = StrLen (Name);

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiPartitionInfoProtocolGuid, (VOID **)&PartInfo);
    if (EFI_ERROR (Status) || (PartInfo->Type != PARTITION_TYPE_GPT)) {
      continue;
    }
    if (StrnCmp (PartInfo->Info.Gpt.PartitionName, Name, NameLen + 1) != 0) {
      continue;
    }
    /* 找到了分区: 挂 FAT 卷 */
    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Sfs);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "LinuxBoot: %s 不是 FAT 文件系统 (Fat 驱动未启用?)\n", Name));
      FreePool (Handles);
      return EFI_UNSUPPORTED;
    }
    Status = Sfs->OpenVolume (Sfs, Root);
    FreePool (Handles);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    DEBUG ((DEBUG_INFO, "LinuxBoot: FAT 卷 %s 已挂载\n", Name));
    return EFI_SUCCESS;
  }

  FreePool (Handles);
  DEBUG ((DEBUG_WARN, "LinuxBoot: 分区 %s 不存在\n", Name));
  return EFI_NOT_FOUND;
}

/**
  从 FAT 卷读一个文件。LoadBase=0 时自动分配页。
  成功后 *OutBase 为实际地址 (调用方负责 FreePages), *OutSize 为文件大小。
**/
STATIC
EFI_STATUS
EspReadFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  IN  CONST CHAR16       *Name,
  IN  UINT64             LoadBase,     /* 0 = 自动分配 */
  OUT UINT64             *OutBase,
  OUT UINTN              *OutSize
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  UINTN              ReadSize;

  Status = Root->Open (Root, &File, (CHAR16 *)Name, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "LinuxBoot: \\%s 不存在 (%r)\n", Name, Status));
    return Status;
  }

  /* 文件大小: GetInfo 两次调用模板 */
  InfoSize = 0;
  Status   = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    File->Close (File);
    return (Status == EFI_SUCCESS) ? EFI_INVALID_PARAMETER : Status;
  }
  Info = AllocatePool (InfoSize);
  if (Info == NULL) {
    File->Close (File);
    return EFI_OUT_OF_RESOURCES;
  }
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  *OutSize = (UINTN)Info->FileSize;
  FreePool (Info);
  if (EFI_ERROR (Status)) {
    File->Close (File);
    return Status;
  }

  if ((*OutSize == 0) || (*OutSize > T158_MAX_INITRD_SIZE)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: \\%s 大小 %d 异常\n", Name, *OutSize));
    File->Close (File);
    return EFI_BAD_BUFFER_SIZE;
  }

  if (LoadBase == 0) {
    Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData,
                                 EFI_SIZE_TO_PAGES (*OutSize), &LoadBase);
  } else {
    Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesData,
                                 EFI_SIZE_TO_PAGES (*OutSize), &LoadBase);
  }
  if (EFI_ERROR (Status)) {
    File->Close (File);
    return Status;
  }

  ReadSize = *OutSize;
  Status   = File->Read (File, &ReadSize, (VOID *)(UINTN)LoadBase);
  File->Close (File);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (ReadSize != *OutSize) {
    return EFI_DEVICE_ERROR;
  }

  *OutBase = LoadBase;
  DEBUG ((DEBUG_INFO, "LinuxBoot: \\%s = %dKB @0x%lX\n",
          Name, (UINT32)(*OutSize >> 10), LoadBase));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
T158LinuxBootFromEsp (
  IN CONST CHAR16  *EspPartitionName
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  UINT64             KernelLoad;
  UINTN              KernelSize;
  UINT64             RamBase;
  UINTN              RamSize;
  UINT64             DtBase;
  UINTN              DtSize;

  KernelLoad = T158_KERNEL_LOAD_BASE;
  RamBase    = 0;
  RamSize    = 0;
  DtBase     = 0;
  DtSize     = 0;

  Status = FindEspVolume (EspPartitionName, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* 1. \kernel (必须) */
  Status = EspReadFile (Root, T158_ESP_KERNEL, KernelLoad, &KernelLoad, &KernelSize);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return EFI_NOT_FOUND;
  }
  if (!IsArm64Image (KernelLoad)) {
    DEBUG ((DEBUG_ERROR, "LinuxBoot: \\%s 不是未压缩 ARMd Image\n", T158_ESP_KERNEL));
    Root->Close (Root);
    return EFI_NOT_FOUND;
  }

  /* 2. \ramdisk (必须) */
  Status = EspReadFile (Root, T158_ESP_RAMDISK, 0, &RamBase, &RamSize);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    gBS->FreePages (KernelLoad, EFI_SIZE_TO_PAGES (KernelSize));
    return EFI_NOT_FOUND;
  }

  /* 3. \dtb (可选: 缺省用 U-Boot 传下来的 DTB) */
  Status = EspReadFile (Root, T158_ESP_DTB, 0, &DtBase, &DtSize);
  if (EFI_ERROR (Status)) {
    DtBase = 0;
    DEBUG ((DEBUG_WARN, "LinuxBoot: 没有 \\%s, 用 U-Boot 的 DTB\n", T158_ESP_DTB));
  }

  Root->Close (Root);
  DEBUG ((DEBUG_INFO, "LinuxBoot: ESP 引导 kernel=%dKB ramdisk=%dKB dtb=%s\n",
          (UINT32)(KernelSize >> 10), (UINT32)(RamSize >> 10),
          (DtBase != 0) ? L"文件" : L"HOB"));

  /* 4. 公共尾段。槽位 NULL: ramdisk 用户已自行组合, 不再拼 init_boot/vendor_boot。 */
  return LinuxBootCommon (
           KernelLoad, KernelSize,
           NULL, 0, 0,
           RamBase, RamSize,
           NULL,
           DtBase
           );
}
