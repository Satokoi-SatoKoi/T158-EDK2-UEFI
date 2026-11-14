/** @file
 *  SprdUfsHcDxe —— 展锐 UMS9621 UFS Host Controller (MMIO @0x22000000)
 *
 *  作用: 为 MdeModulePkg/Bus/Ufs/UfsPassThruDxe 提供 EDKII_UFS_HOST_CONTROLLER_PROTOCOL。
 *        UfsPassThruDxe 里的 UIC/UTP/UTRD 编程是标准 JEDEC UFSHCI 代码, 直接复用;
 *        202405 的 edk2 只有 PCI 版 HC 驱动 (UfsPciHcDxe), 展锐是 MMIO 控制器,
 *        所以自写这个薄壳。
 *
 *  前提: U-Boot 已经初始化过 UFS 硬件 (它就是从 UFS 读的 boot 分区),
 *        时钟/复位/PHY 一般保持使能; 本驱动不做 vendor 初始化, 直接依赖 HC 寄存器。
 *
 *  DMA 说明: 展锐 UFSHCI 使用 32 位 DMA 地址, 而 UEFI 的 MMU 是 1:1 映射,
 *            所以 DeviceAddress == HostAddress, Map/Unmap 只需做 Cache 维护。
 **/

#include <Uefi.h>
#include <Protocol/DevicePath.h>
#include <Protocol/UfsHostController.h>
#include <Library/BaseLib.h>
// InvalidateDataCacheRange/WriteBackDataCacheRange 声明在 CacheMaintenanceLib.h
// (实现在 ArmPkg/Library/ArmCacheMaintenanceLib, 由 DSC 的 CacheMaintenanceLib 映射提供)
#include <Library/CacheMaintenanceLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#define SPRD_UFS_HC_SIGNATURE  SIGNATURE_32 ('S', 'U', 'F', 'S')

typedef struct {
  UINT32                              Signature;
  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  UfsHc;
  UINT64                              HcBase;
} SPRD_UFS_HC_PRIVATE;

#define SPRD_UFS_HC_PRIVATE_FROM_THIS(a) \
  CR (a, SPRD_UFS_HC_PRIVATE, UfsHc, SPRD_UFS_HC_SIGNATURE)

/* --------------------------------------------------------------- MMIO 访问 */

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcGetMmioBar (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  OUT UINTN                                  *MmioBar
  )
{
  SPRD_UFS_HC_PRIVATE  *Private;

  if ((This == NULL) || (MmioBar == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Private  = SPRD_UFS_HC_PRIVATE_FROM_THIS (This);
  *MmioBar = (UINTN)Private->HcBase;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcMmioRead (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer
  )
{
  SPRD_UFS_HC_PRIVATE  *Private;
  UINTN                Base;
  UINTN                Index;
  UINT8                *Byte;
  UINT16               *Word;
  UINT32               *Dword;

  if ((This == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SPRD_UFS_HC_PRIVATE_FROM_THIS (This);
  Base    = (UINTN)Private->HcBase + (UINTN)Offset;

  switch (Width) {
    case EfiUfsHcWidthUint8:
      Byte = (UINT8 *)Buffer;
      for (Index = 0; Index < Count; Index++) {
        Byte[Index] = MmioRead8 (Base + Index);
      }
      break;

    case EfiUfsHcWidthUint16:
      Word = (UINT16 *)Buffer;
      for (Index = 0; Index < Count; Index++) {
        Word[Index] = MmioRead16 (Base + Index * 2);
      }
      break;

    case EfiUfsHcWidthUint32:
      Dword = (UINT32 *)Buffer;
      for (Index = 0; Index < Count; Index++) {
        Dword[Index] = MmioRead32 (Base + Index * 4);
      }
      break;

    case EfiUfsHcWidthUint64:
      Dword = (UINT32 *)Buffer;
      for (Index = 0; Index < Count * 2; Index++) {
        Dword[Index] = MmioRead32 (Base + Index * 4);
      }
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcMmioWrite (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL        *This,
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL_WIDTH  Width,
  IN     UINT64                                    Offset,
  IN     UINTN                                     Count,
  IN OUT VOID                                      *Buffer
  )
{
  SPRD_UFS_HC_PRIVATE  *Private;
  UINTN                Base;
  UINTN                Index;
  UINT8                *Byte;
  UINT16               *Word;
  UINT32               *Dword;

  if ((This == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = SPRD_UFS_HC_PRIVATE_FROM_THIS (This);
  Base    = (UINTN)Private->HcBase + (UINTN)Offset;

  switch (Width) {
    case EfiUfsHcWidthUint8:
      Byte = (UINT8 *)Buffer;
      for (Index = 0; Index < Count; Index++) {
        MmioWrite8 (Base + Index, Byte[Index]);
      }
      break;

    case EfiUfsHcWidthUint16:
      Word = (UINT16 *)Buffer;
      for (Index = 0; Index < Count; Index++) {
        MmioWrite16 (Base + Index * 2, Word[Index]);
      }
      break;

    case EfiUfsHcWidthUint32:
      Dword = (UINT32 *)Buffer;
      for (Index = 0; Index < Count; Index++) {
        MmioWrite32 (Base + Index * 4, Dword[Index]);
      }
      break;

    case EfiUfsHcWidthUint64:
      Dword = (UINT32 *)Buffer;
      for (Index = 0; Index < Count * 2; Index++) {
        MmioWrite32 (Base + Index * 4, Dword[Index]);
      }
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/* ------------------------------------------- DMA Buffer (32 位一致内存) */

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcAllocateBuffer (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN     EFI_ALLOCATE_TYPE                   Type,
  IN     EFI_MEMORY_TYPE                     MemoryType,
  IN     UINTN                               Pages,
  OUT VOID                                   **HostAddress,
  IN     UINT64                              Attributes
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || (HostAddress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  /* UFSHCI 是 32 位 DMA, DRAM 从 0x80000000 起, 正常分配即可 */
  Status = gBS->AllocatePages (Type, MemoryType, Pages, (EFI_PHYSICAL_ADDRESS *)HostAddress);
  if (EFI_ERROR (Status)) {
    /* 顶层分配可能拿到 4GB 以上的页, 兜底限制到 4GB 内 */
    Status = gBS->AllocatePages (
                    AllocateMaxAddress,
                    MemoryType,
                    Pages,
                    (EFI_PHYSICAL_ADDRESS *)HostAddress
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  DEBUG ((DEBUG_INFO, "UfsHc: AllocateBuffer %d pages @0x%lX\n", (UINT32)Pages, (UINT64)(UINTN)*HostAddress));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcFreeBuffer (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN  UINTN                               Pages,
  IN  VOID                                *HostAddress
  )
{
  if ((This == NULL) || (HostAddress == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  return gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress, Pages);
}

/* ------------------------------------------------ DMA Map/Unmap (Cache) */

typedef struct {
  UINTN  HostAddress;
  UINTN  NumberOfBytes;
} SPRD_UFS_HC_MAPPING;

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcMap (
  IN     EDKII_UFS_HOST_CONTROLLER_PROTOCOL   *This,
  IN     EDKII_UFS_HOST_CONTROLLER_OPERATION  Operation,
  IN     VOID                                 *HostAddress,
  IN OUT UINTN                                *NumberOfBytes,
  OUT EFI_PHYSICAL_ADDRESS                    *DeviceAddress,
  OUT VOID                                    **Mapping
  )
{
  SPRD_UFS_HC_MAPPING  *MapInfo;

  if ((This == NULL) || (HostAddress == NULL) || (NumberOfBytes == NULL) ||
      (DeviceAddress == NULL) || (Mapping == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  /*
   * 1:1 页表 => 设备地址 = CPU 物理地址 = CPU 虚拟地址。
   * Cache 维护按操作方向:
   *   BusMasterRead  (设备写内存) : 先失效 CPU 缓存, 读时才能拿到新数据
   *   BusMasterWrite (设备读内存) : 先回写 CPU 缓存到内存
   *   CommonBuffer                : 回写 + 失效
   */
  switch (Operation) {
    case EdkiiUfsHcOperationBusMasterRead:
      InvalidateDataCacheRange (HostAddress, *NumberOfBytes);
      break;

    case EdkiiUfsHcOperationBusMasterWrite:
      WriteBackDataCacheRange (HostAddress, *NumberOfBytes);
      break;

    case EdkiiUfsHcOperationBusMasterCommonBuffer:
      WriteBackInvalidateDataCacheRange (HostAddress, *NumberOfBytes);
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  MapInfo               = AllocatePool (sizeof (SPRD_UFS_HC_MAPPING));
  if (MapInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  MapInfo->HostAddress   = (UINTN)HostAddress;
  MapInfo->NumberOfBytes = *NumberOfBytes;

  *DeviceAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress;
  *Mapping       = MapInfo;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcUnmap (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This,
  IN  VOID                                *Mapping
  )
{
  if ((This == NULL) || (Mapping == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  /* 设备写内存的场合, 让 CPU 之后能读到最新数据: 再失效一次 */
  InvalidateDataCacheRange (
    (VOID *)((SPRD_UFS_HC_MAPPING *)Mapping)->HostAddress,
    ((SPRD_UFS_HC_MAPPING *)Mapping)->NumberOfBytes
    );

  FreePool (Mapping);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SprdUfsHcFlush (
  IN  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *This
  )
{
  /* 展锐 MMIO HC 没有 posted write 概念, 空 */
  return EFI_SUCCESS;
}

/* -------------------------------------------------------------- 入口 */

/**
  UFS Host Controller 驱动入口: 创建 handle 并安装
  EDKII_UFS_HOST_CONTROLLER_PROTOCOL + MemMap DevicePath。
**/
EFI_STATUS
EFIAPI
SprdUfsHcDriverEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  SPRD_UFS_HC_PRIVATE                 *Private;
  EDKII_UFS_HOST_CONTROLLER_PROTOCOL  *UfsHc;
  EFI_STATUS                          Status;
  EFI_HANDLE                          Handle;
  EFI_DEVICE_PATH_PROTOCOL            *DevPath;
  MEMMAP_DEVICE_PATH                  MemMap;
  EFI_DEVICE_PATH_PROTOCOL            End;

  Private = AllocateZeroPool (sizeof (SPRD_UFS_HC_PRIVATE));
  if (Private == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Private->Signature = SPRD_UFS_HC_SIGNATURE;
  Private->HcBase    = FixedPcdGet64 (PcdUfsHcBase);

  UfsHc               = &Private->UfsHc;
  UfsHc->GetUfsHcMmioBar = SprdUfsHcGetMmioBar;
  UfsHc->AllocateBuffer  = SprdUfsHcAllocateBuffer;
  UfsHc->FreeBuffer      = SprdUfsHcFreeBuffer;
  UfsHc->Map             = SprdUfsHcMap;
  UfsHc->Unmap           = SprdUfsHcUnmap;
  UfsHc->Flush           = SprdUfsHcFlush;
  UfsHc->Read            = SprdUfsHcMmioRead;
  UfsHc->Write           = SprdUfsHcMmioWrite;

  /* 构造 MemMap DevicePath (UfsPassThru 用它给子设备命名) */
  ZeroMem (&MemMap, sizeof (MemMap));
  MemMap.Header.Type        = HARDWARE_DEVICE_PATH;
  MemMap.Header.SubType     = HW_MEMMAP_DP;
  MemMap.Header.Length[0]   = (UINT8)sizeof (MEMMAP_DEVICE_PATH);
  MemMap.Header.Length[1]   = (UINT8)(sizeof (MEMMAP_DEVICE_PATH) >> 8);
  MemMap.MemoryType         = EfiMemoryMappedIO;
  MemMap.StartingAddress    = Private->HcBase;
  MemMap.EndingAddress      = Private->HcBase + FixedPcdGet32 (PcdUfsHcSize) - 1;

  /* 手构 End 节点 (DevicePathLib 没有 EndDevicePath() 这个函数) */
  End.Type    = END_DEVICE_PATH_TYPE;
  End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  End.Length[0] = (UINT8)sizeof (EFI_DEVICE_PATH_PROTOCOL);
  End.Length[1] = 0;

  DevPath = AppendDevicePathNode ((EFI_DEVICE_PATH_PROTOCOL *)&MemMap, &End);
  if (DevPath == NULL) {
    FreePool (Private);
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  DevPath
                  );
  if (EFI_ERROR (Status)) {
    FreePool (DevPath);
    FreePool (Private);
    return Status;
  }

  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEdkiiUfsHostControllerProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  UfsHc
                  );
  if (EFI_ERROR (Status)) {
    gBS->UninstallProtocolInterface (Handle, &gEfiDevicePathProtocolGuid, DevPath);
    FreePool (DevPath);
    FreePool (Private);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SprdUfsHc: HC @0x%lX 已就绪 (等待 UfsPassThru 绑定)\n", Private->HcBase));
  return EFI_SUCCESS;
}
