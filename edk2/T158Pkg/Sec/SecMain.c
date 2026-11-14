/** @file
 *  T158 SEC 闃舵 (PEI-less)銆?
 *
 *  鐢?AArch64/PayloadEntry.S 璺宠浆杩涙潵, x0 = device tree 鍦板潃銆?
 *  鑱岃矗:
 *    1) 娓?.bss銆佹帰娴嬪钩鍙般€佽捣涓插彛
 *    2) 寤虹珛 HOB 閾捐〃 (鍐呭瓨璧勬簮 / 鍐呭瓨鍒嗛厤 / FV / CPU / 浜ゆ帴淇℃伅)
 *    3) 鎵撳紑 MMU 涓?Cache
 *    4) 浠?FV 涓壘鍒?DXE Core 鐨?PE32 娈? 瑁呰浇骞舵妸鎺у埗鏉冧氦缁欏畠
 **/

#include <Base.h>
#include <Uefi/UefiBaseType.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <Pi/PiFirmwareVolume.h>
#include <Pi/PiFirmwareFile.h>
#include <Protocol/DevicePath.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
// 注意: 不要 include HobLib.h —— 本文件有自己的 mini HOB 构建器 (AppendHob 系列),
// 与 HobLib.h 的公开声明 (非 static) 冲突。GUID HOB 用本文件的 BuildGuidHob64 手拼。
// SetDevicePathNodeLength() 是 DevicePathLib.h 里的宏, 必须显式包含
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/PcdLib.h>
#include <Library/PeCoffLib.h>
#include <Library/SerialPortLib.h>
#include <Library/T158PlatformLib.h>
#include <T158Platform.h>

/* --------------------------------------------------------------- .bss 娓呴浂 */
/* 鐢ㄥ急绗﹀彿: 鑻ラ摼鎺ヨ剼鏈湭鎻愪緵杩欎袱涓鍙? 鍦板潃涓?NULL, 涓嬮潰鐨勬竻鐞嗕細琚嚜鍔ㄨ烦杩?*/
__attribute__ ((weak)) extern UINT8  __bss_start__[];
__attribute__ ((weak)) extern UINT8  __bss_end__[];

/* ----------------------------------------- 渚?PayloadEntry.S 璇诲彇鐨勫叏灞€鍙橀噺 */
volatile UINT64  gT158FdBaseAddress = FixedPcdGet64 (PcdFdBaseAddress);
volatile UINT64  gT158FdSize        = FixedPcdGet32 (PcdFdSize);
volatile UINT64  gT158StackTop      = FixedPcdGet64 (PcdPrePiStackBase) + FixedPcdGet32 (PcdPrePiStackSize);

/* HOB 鏋勯€犳父鏍?*/
STATIC EFI_HOB_HANDOFF_INFO_TABLE  *mPhitHob = NULL;
STATIC UINT8                       *mHobEnd  = NULL;
STATIC UINT8                       *mHobLimit = NULL;

/**
  杩藉姞涓€涓?HOB, 杩斿洖鍏舵暟鎹尯鎸囬拡銆?
**/
STATIC
VOID *
AppendHob (
  IN UINT16  Type,
  IN UINT16  Length
  )
{
  EFI_HOB_GENERIC_HEADER  *Hdr;

  Length = (UINT16)ALIGN_VALUE (Length, 8);
  if (((UINTN)mHobEnd + Length) > (UINTN)mHobLimit) {
    return NULL;
  }

  Hdr           = (EFI_HOB_GENERIC_HEADER *)mHobEnd;
  Hdr->HobType  = Type;
  Hdr->HobLength = Length;
  Hdr->Reserved = 0;

  mHobEnd += Length;
  return (VOID *)Hdr;
}

/**
  手拼一个带 UINT64 数据的 GUID HOB (EFI_HOB_TYPE_GUID_EXTENSION)。
  DXE 侧用 HobLib 的 GetFirstGuidHob/GET_GUID_HOB_DATA 读取, 布局兼容。

  用途: 把 U-Boot 传进来的 DTB 物理地址交给 DXE (gT158FdtHobGuid)。
**/
STATIC
VOID
BuildGuidHob64 (
  IN CONST EFI_GUID  *Guid,
  IN UINT64          Data
  )
{
  EFI_HOB_GUID_TYPE  *Hob;

  Hob = (EFI_HOB_GUID_TYPE *)AppendHob (
          EFI_HOB_TYPE_GUID_EXTENSION,
          (UINT16)(sizeof (EFI_HOB_GUID_TYPE) + sizeof (UINT64))
          );
  if (Hob == NULL) {
    return;
  }

  CopyMem (&Hob->Name, Guid, sizeof (EFI_GUID));
  *(UINT64 *)(Hob + 1) = Data;
}

STATIC
VOID
BuildResourceHob (
  IN EFI_RESOURCE_TYPE      ResourceType,
  IN EFI_RESOURCE_ATTRIBUTE_TYPE  Attributes,
  IN EFI_PHYSICAL_ADDRESS   Base,
  IN UINT64                 Length
  )
{
  EFI_HOB_RESOURCE_DESCRIPTOR  *Hob;

  Hob = (EFI_HOB_RESOURCE_DESCRIPTOR *)AppendHob (
          EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,
          sizeof (EFI_HOB_RESOURCE_DESCRIPTOR)
          );
  if (Hob == NULL) {
    return;
  }

  Hob->ResourceType      = ResourceType;
  Hob->ResourceAttribute = Attributes;
  Hob->PhysicalStart     = Base;
  Hob->ResourceLength    = Length;
  Hob->Owner             = gEfiCallerIdGuid;
}

STATIC
VOID
BuildMemoryAllocationHob (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                Length,
  IN EFI_MEMORY_TYPE       MemoryType
  )
{
  EFI_HOB_MEMORY_ALLOCATION  *Hob;
  EFI_DEVICE_PATH_PROTOCOL   *DevPath;

  Hob = (EFI_HOB_MEMORY_ALLOCATION *)AppendHob (
          EFI_HOB_TYPE_MEMORY_ALLOCATION,
          (UINT16)(sizeof (EFI_HOB_MEMORY_ALLOCATION) + sizeof (EFI_DEVICE_PATH_PROTOCOL))
          );
  if (Hob == NULL) {
    return;
  }

  ZeroMem (&Hob->AllocDescriptor, sizeof (EFI_HOB_MEMORY_ALLOCATION_HEADER));
  Hob->AllocDescriptor.MemoryBaseAddress = Base;
  Hob->AllocDescriptor.MemoryLength      = Length;
  Hob->AllocDescriptor.MemoryType        = MemoryType;

  /* 鍒嗛厤鍨?HOB 鍚庨潰蹇呴』璺熶竴涓澶囪矾寰?(杩欓噷鐢?End 鑺傜偣) */
  DevPath                 = (EFI_DEVICE_PATH_PROTOCOL *)(Hob + 1);
  DevPath->Type           = END_DEVICE_PATH_TYPE;
  DevPath->SubType        = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  SetDevicePathNodeLength (DevPath, sizeof (EFI_DEVICE_PATH_PROTOCOL));
}

STATIC
VOID
BuildFvHob (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                Length
  )
{
  EFI_HOB_FIRMWARE_VOLUME  *Hob;

  Hob = (EFI_HOB_FIRMWARE_VOLUME *)AppendHob (
          EFI_HOB_TYPE_FV,
          sizeof (EFI_HOB_FIRMWARE_VOLUME)
          );
  if (Hob == NULL) {
    return;
  }

  Hob->BaseAddress = Base;
  Hob->Length      = Length;
}

STATIC
VOID
BuildCpuHob (
  IN UINT8  SizeOfMemorySpace,
  IN UINT8  SizeOfIoSpace
  )
{
  EFI_HOB_CPU  *Hob;

  Hob = (EFI_HOB_CPU *)AppendHob (EFI_HOB_TYPE_CPU, sizeof (EFI_HOB_CPU));
  if (Hob == NULL) {
    return;
  }

  Hob->SizeOfMemorySpace = SizeOfMemorySpace;
  Hob->SizeOfIoSpace     = SizeOfIoSpace;
  ZeroMem (Hob->Reserved, sizeof (Hob->Reserved));
}

/**
  浠?FV 涓壘鍒?DXE Core 骞惰杞姐€?
**/
STATIC
EFI_STATUS
LoadDxeCoreFromFv (
  OUT VOID  **EntryPoint
  )
{
  EFI_FIRMWARE_VOLUME_HEADER  *FvHeader;
  UINT64                      FvBase;
  UINT64                      FvLength;
  UINTN                       FileOffset;
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  EFI_STATUS                  Status;
  VOID                        *ImageBuffer;
  UINTN                       ImageSize;

  FvBase   = FixedPcdGet64(PcdFvBaseAddress);
  FvLength = FixedPcdGet32(PcdFvSize);
  FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)(UINTN)FvBase;

  DEBUG ((DEBUG_INFO, "SEC: FV @0x%lX 绛惧悕=0x%08X 闀垮害=0x%lX\n",
          FvBase, FvHeader->Signature, FvLength));
  if (FvHeader->Signature != EFI_FVH_SIGNATURE) {
    return EFI_NOT_FOUND;
  }

  FileOffset = (UINTN)FvHeader->HeaderLength;
  Status     = EFI_NOT_FOUND;
  ImageBuffer = NULL;

  while ((FileOffset + sizeof (EFI_FFS_FILE_HEADER)) <= FvLength) {
    EFI_FFS_FILE_HEADER  *File;
    UINTN                FileSize;
    UINTN                HeaderSize;
    UINTN                SectionOffset;

    File = (EFI_FFS_FILE_HEADER *)(UINTN)(FvBase + FileOffset);
    if (IS_FFS_FILE2 (File)) {
      FileSize   = FFS_FILE2_SIZE (File);
      HeaderSize = sizeof (EFI_FFS_FILE_HEADER2);
    } else {
      FileSize   = FFS_FILE_SIZE (File);
      HeaderSize = sizeof (EFI_FFS_FILE_HEADER);
    }

    if ((FileSize == 0) || (FileSize > (FvLength - FileOffset))) {
      break;
    }

    if ((File->Type == EFI_FV_FILETYPE_DXE_CORE) &&
        ((File->State & EFI_FILE_HEADER_INVALID) == 0)) {
      SectionOffset = HeaderSize;
      while ((SectionOffset + sizeof (EFI_COMMON_SECTION_HEADER)) < FileSize) {
        EFI_COMMON_SECTION_HEADER  *Section;
        UINTN                      SectionSize;
        UINTN                      SectionHeaderSize;

        Section = (EFI_COMMON_SECTION_HEADER *)(UINTN)(FvBase + FileOffset + SectionOffset);
        if (IS_SECTION2 (Section)) {
          SectionSize = SECTION2_SIZE (Section);
          SectionHeaderSize = sizeof (EFI_COMMON_SECTION_HEADER2);
        } else {
          SectionSize = SECTION_SIZE (Section);
          SectionHeaderSize = sizeof (EFI_COMMON_SECTION_HEADER);
        }

        if (SectionSize == 0) {
          break;
        }

        if ((Section->Type == EFI_SECTION_PE32) || (Section->Type == EFI_SECTION_TE)) {
          ImageBuffer = (VOID *)(UINTN)(FvBase + FileOffset + SectionOffset + SectionHeaderSize);
          ImageSize   = SectionSize - SectionHeaderSize;
          Status      = EFI_SUCCESS;
          break;
        }

        SectionOffset += ALIGN_VALUE (SectionSize, 4);
      }
    }

    if (!EFI_ERROR (Status)) {
      break;
    }

    FileOffset += ALIGN_VALUE (FileSize, 8);
  }

  if (EFI_ERROR (Status) || (ImageBuffer == NULL)) {
    DEBUG ((DEBUG_ERROR, "SEC: FV 涓湭鎵惧埌 DXE Core (%r)\n", Status));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "SEC: DXE Core 鏄犲儚 @0x%lX 澶у皬 0x%X\n",
          (UINT64)(UINTN)ImageBuffer, ImageSize));

  ZeroMem (&ImageContext, sizeof (ImageContext));
  ImageContext.Handle    = ImageBuffer;
  ImageContext.ImageRead = PeCoffLoaderImageReadFromMemory;

  Status = PeCoffLoaderGetImageInfo (&ImageContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SEC: PeCoffLoaderGetImageInfo %r\n", Status));
    return Status;
  }

  ImageContext.DestinationAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)ImageBuffer;
  ImageContext.ImageAddress       = (EFI_PHYSICAL_ADDRESS)(UINTN)ImageBuffer;

  Status = PeCoffLoaderLoadImage (&ImageContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SEC: PeCoffLoaderLoadImage %r\n", Status));
    return Status;
  }

  Status = PeCoffLoaderRelocateImage (&ImageContext);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SEC: PeCoffLoaderRelocateImage %r\n", Status));
    return Status;
  }

  InvalidateInstructionCacheRange (ImageBuffer, ImageContext.ImageSize);

  *EntryPoint = (VOID *)(UINTN)ImageContext.EntryPoint;
  return EFI_SUCCESS;
}

/**
  HOB 閾捐〃鏋勯€犮€?
**/
STATIC
VOID *
BuildHobList (
  IN UINT64  DeviceTreeBase
  )
{
  UINT64    UefiBase;
  UINT64    UefiSize;
  UINT64    HobBase;
  UINT64    HobSize;
  UINT64    MemBase;
  UINT64    MemSize;
  UINT64    FdBase;
  UINT32    FdSize;
  UINT64    StackBase;
  UINT32    StackSize;

  FdBase    = FixedPcdGet64(PcdFdBaseAddress);
  FdSize    = FixedPcdGet32(PcdFdSize);
  StackBase = FixedPcdGet64(PcdPrePiStackBase);
  StackSize = FixedPcdGet32(PcdPrePiStackSize);
  UefiBase  = FixedPcdGet64(PcdUefiMemoryRegionBase);
  UefiSize  = FixedPcdGet32(PcdUefiMemoryRegionSize);

  T158PlatformGetMemoryInfo (&MemBase, &MemSize);

  HobBase  = UefiBase;
  HobSize  = SIZE_1MB;                     /* HOB 姹? 1MB */
  mHobEnd  = (UINT8 *)(UINTN)(HobBase + sizeof (EFI_HOB_HANDOFF_INFO_TABLE));
  mHobLimit = (UINT8 *)(UINTN)(HobBase + HobSize);

  /* 1) PHIT */
  mPhitHob = (EFI_HOB_HANDOFF_INFO_TABLE *)(UINTN)HobBase;
  mPhitHob->Header.HobType   = EFI_HOB_TYPE_HANDOFF;
  mPhitHob->Header.HobLength = sizeof (EFI_HOB_HANDOFF_INFO_TABLE);
  mPhitHob->Header.Reserved  = 0;
  mPhitHob->Version          = EFI_HOB_HANDOFF_TABLE_VERSION;
  mPhitHob->BootMode         = BOOT_WITH_FULL_CONFIGURATION;
  mPhitHob->EfiMemoryTop        = UefiBase + UefiSize;
  mPhitHob->EfiMemoryBottom     = UefiBase;
  mPhitHob->EfiFreeMemoryTop    = UefiBase + UefiSize;
  mPhitHob->EfiFreeMemoryBottom = 0;   /* 鏈€鍚庣粺涓€鍥炲～ */
  mPhitHob->EfiEndOfHobList     = 0;

  /* 2) 绯荤粺鍐呭瓨璧勬簮 (鏁存 DRAM) */
  BuildResourceHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    (EFI_RESOURCE_ATTRIBUTE_PRESENT    |
     EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
     EFI_RESOURCE_ATTRIBUTE_TESTED      |
     EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
     EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
     EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
     EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE),
    MemBase,
    MemSize
    );

  /* 3) 宸茬粡琚崰鐢ㄧ殑鍖哄煙 (涓嶅啀浣滀负鍙敤鍐呭瓨鍒嗛厤鍑哄幓) */
  BuildMemoryAllocationHob (HobBase, HobSize, EfiBootServicesData);
  BuildMemoryAllocationHob (StackBase, StackSize, EfiBootServicesData);
  BuildMemoryAllocationHob (FdBase, FdSize, EfiReservedMemoryType);
  if (DeviceTreeBase != 0) {
    BuildMemoryAllocationHob (DeviceTreeBase, SIZE_1MB, EfiACPIMemoryNVS);
    /* 把 DTB 物理地址通过 GUID HOB 交给 DXE (T158LinuxBootLib 用它做安卓 kernel 的 DTB) */
    BuildGuidHob64 (&gT158FdtHobGuid, DeviceTreeBase);
  }

  /* 3b) 帧缓冲 (logobuffer): 保留给 GOP 驱动, 不能被 DXE 当普通内存分配走 */
  if ((FixedPcdGet64 (PcdFrameBufferBase) != 0) && (FixedPcdGet32 (PcdFrameBufferSize) != 0)) {
    BuildMemoryAllocationHob (
      FixedPcdGet64 (PcdFrameBufferBase),
      ALIGN_VALUE (FixedPcdGet32 (PcdFrameBufferSize), SIZE_4KB),
      EfiReservedMemoryType
      );
  }

  /* 4) FV: DXE Core 涓庡叾浣欓┍鍔ㄩ兘浠庤繖閲屽彂鐜?*/
  BuildFvHob (FixedPcdGet64(PcdFvBaseAddress), FixedPcdGet32(PcdFvSize));

  /* 5) CPU */
  BuildCpuHob (40, 16);

  /* 6) 缁撴潫 HOB */
  {
    EFI_HOB_GENERIC_HEADER  *EndHob;

    EndHob = (EFI_HOB_GENERIC_HEADER *)AppendHob (EFI_HOB_TYPE_END_OF_HOB_LIST, sizeof (EFI_HOB_GENERIC_HEADER));
    if (EndHob != NULL) {
      EndHob->HobType   = EFI_HOB_TYPE_END_OF_HOB_LIST;
      EndHob->HobLength = sizeof (EFI_HOB_GENERIC_HEADER);
      EndHob->Reserved  = 0;
    }
  }

  mPhitHob->EfiEndOfHobList     = (EFI_PHYSICAL_ADDRESS)(UINTN)mHobEnd;
  mPhitHob->EfiFreeMemoryBottom = (EFI_PHYSICAL_ADDRESS)(UINTN)mHobEnd;

  return (VOID *)mPhitHob;
}

/**
  C 鍏ュ彛 (鐢?PayloadEntry.S 璋冪敤, Arg1 = device tree 鍦板潃)銆?
**/
VOID
EFIAPI
T158SecStartup (
  IN UINT64  DeviceTreeBase
  )
{
  VOID                             *HobList;
  VOID                             *DxeCoreEntryPoint;
  EFI_STATUS                       Status;
  UINT64                           MemBase;
  UINT64                           MemSize;
  UINT64                           UartBase;
  UINT32                           UartBaud;

  /* 1) .bss */
  // 注意: __bss_start__/__bss_end__ 是数组类型, 不能直接和 NULL 或彼此比较
  //       (GCC 15 的 -Werror=array-compare 会报错), 必须先转成地址整数。
  if (((UINTN)__bss_start__ != 0) && ((UINTN)__bss_end__ != 0) && ((UINTN)__bss_end__ > (UINTN)__bss_start__)) {
    ZeroMem (__bss_start__, (UINTN)__bss_end__ - (UINTN)__bss_start__);
  }

  /* 2) 骞冲彴鎺㈡祴 + 涓插彛 */
  T158PlatformEarlyInit (DeviceTreeBase);
  T158PlatformGetMemoryInfo (&MemBase, &MemSize);
  T158PlatformGetUartInfo (&UartBase, &UartBaud);

  DEBUG ((DEBUG_INFO, "\n\n"));
  DEBUG ((DEBUG_INFO, "===============================================\n"));
  DEBUG ((DEBUG_INFO, " T158/UMS9621 UEFI Payload - SEC\n"));
  DEBUG ((DEBUG_INFO, " DT=0x%lX  DRAM=0x%lX+0x%lX  UART=0x%lX@%d\n",
          DeviceTreeBase, MemBase, MemSize, UartBase, UartBaud));
  DEBUG ((DEBUG_INFO, "===============================================\n\n"));

  /* 3) HOB */
  HobList = BuildHobList (DeviceTreeBase);
  DEBUG ((DEBUG_INFO, "SEC: HOB list @0x%lX\n", (UINT64)(UINTN)HobList));

  /* 4) MMU / Cache */
  Status = ArmConfigureMmu (T158PlatformGetVirtualMemoryMap (), NULL, NULL);
  DEBUG ((DEBUG_INFO, "SEC: ArmConfigureMmu = %r\n", Status));

  /* 5) 瑁呰浇骞惰繘鍏?DXE Core */
  Status = LoadDxeCoreFromFv (&DxeCoreEntryPoint);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SEC: 鏃犳硶瑁呰浇 DXE Core, 鍋滄満\n"));
    CpuDeadLoop ();
  }

  DEBUG ((DEBUG_INFO, "SEC: 杩涘叆 DXE Core @0x%lX\n", (UINT64)(UINTN)DxeCoreEntryPoint));

  //
  // DXE Core 鍏ュ彛: VOID EFIAPI DxeCoreMain (VOID *HobStart)
  //
  ((VOID (EFIAPI *)(VOID *))DxeCoreEntryPoint) (HobList);

  CpuDeadLoop ();
}
