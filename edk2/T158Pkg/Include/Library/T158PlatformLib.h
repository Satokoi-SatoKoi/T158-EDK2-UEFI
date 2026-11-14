/** @file
 *  T158PlatformLib —— 平台探测与内存映射表 (SEC 阶段使用)
 **/

#ifndef T158_PLATFORM_LIB_H_
#define T158_PLATFORM_LIB_H_

#include <Library/ArmLib.h>

/**
  早期平台初始化: 解析 DT (可选) -> 确定 DRAM / 串口 -> 初始化串口。
  DtBase: 原厂 U-Boot 经 x0 传入的 device tree 物理地址, 可为 0。
**/
VOID
EFIAPI
T158PlatformEarlyInit (
  IN UINT64  DtBase
  );

VOID
EFIAPI
T158PlatformGetMemoryInfo (
  OUT UINT64  *Base,
  OUT UINT64  *Size
  );

VOID
EFIAPI
T158PlatformGetUartInfo (
  OUT UINT64  *Base,
  OUT UINT32  *BaudRate
  );

UINT64
EFIAPI
T158PlatformGetDeviceTreeBase (
  VOID
  );

/**
  返回 MMU 映射表 (以 Length == 0 的表项结束)。
**/
ARM_MEMORY_REGION_DESCRIPTOR *
EFIAPI
T158PlatformGetVirtualMemoryMap (
  VOID
  );

#endif
