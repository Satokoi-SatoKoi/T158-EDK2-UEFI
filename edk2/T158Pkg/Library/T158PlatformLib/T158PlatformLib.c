/** @file
 *  T158 骞冲彴搴? DRAM/UART 鎺㈡祴 (浼樺厛鐢?x0 浼犲叆鐨?device tree, 澶辫触鍥為€€闈欐€?PCD)
 *  涓?MMU 鏄犲皠琛ㄧ敓鎴愩€?
 **/

#include <Base.h>
#include <Uefi/UefiBaseType.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/SerialPortLib.h>
#include <Library/T158FdtLib.h>
#include <T158Platform.h>

/* --------------------------------------------------- 骞冲彴鍏ㄥ眬鐘舵€?(SEC 闃舵) */
STATIC UINT64  mSystemMemoryBase = 0;
STATIC UINT64  mSystemMemorySize = 0;
STATIC UINT64  mUartBase         = 0;
STATIC UINT32  mUartBaud         = T158_UART_DEFAULT_BAUD;
STATIC UINT64  mDeviceTreeBase   = 0;

/* MMU 鏄犲皠琛? DRAM(cache) + 澶栬(Device) + FD(cache) + End */
STATIC ARM_MEMORY_REGION_DESCRIPTOR  mMemoryTable[] = {
  /* PhysicalBase, VirtualBase, Length, Attributes */
  { 0,                  0, 0, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },  /* DRAM        */
  { T158_AP_APB_BASE,   T158_AP_APB_BASE,  T158_AP_APB_SIZE, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  { T158_AP_AHB_BASE,   T158_AP_AHB_BASE,  T158_AP_AHB_SIZE, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  { T158_GICD_BASE,     T158_GICD_BASE,    T158_GICD_LENGTH, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  { T158_GICR_BASE,     T158_GICR_BASE,    T158_GICR_LENGTH, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  { T158_AON_BASE,      T158_AON_BASE,     0x01000000,       ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  { T158_PUB_BASE,      T158_PUB_BASE,     0x01000000,       ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  /* AON 低速外设段: T158KeyDxe 轮询的 AON GPIO (0x170000, Volume Down)
   * 和 PMIC ADI 桥 (0x400000, Volume Up/Power) 都落在这里, 不映射会 data abort */
  { 0x00100000U,        0x00100000U,       0x00400000U,      ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },
  /* U-Boot 预留的显存 (logobuffer): GOP 直接写入, 必须非缓存,
   * 否则 DPU 扫描不到 CPU 写入的帧 (Cache 未回写) */
  { FixedPcdGet64 (PcdFrameBufferBase),
    FixedPcdGet64 (PcdFrameBufferBase),
    FixedPcdGet32 (PcdFrameBufferSize),
    ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },
  { 0,                  0, 0, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },  /* FD/FV 鍖哄煙  */
  { 0,                  0, 0, 0                                        }  /* 缁撴潫鏍囪    */
};

/**
  鏃╂湡骞冲彴鍒濆鍖? 鎺㈡祴 DRAM / 涓插彛, 鐒跺悗鍒濆鍖栦覆鍙ｃ€?
  DtBase 涓哄師鍘?U-Boot 閫氳繃 x0 浼犲叆鐨?device tree 鐗╃悊鍦板潃 (鍙负 0)銆?
**/
VOID
EFIAPI
T158PlatformEarlyInit (
  IN UINT64  DtBase
  )
{
  T158_FDT_CTX  Ctx;
  UINT64        Base;
  UINT64        Size;
  UINT32        Baud;
  EFI_STATUS    Status;

  /* 1) 鍏堢敤闈欐€?PCD 鍏滃簳 */
  mSystemMemoryBase = FixedPcdGet64(PcdSystemMemoryBase);
  mSystemMemorySize = FixedPcdGet64(PcdSystemMemorySize);
  mUartBase         = FixedPcdGet64(PcdUartBase);
  mUartBaud         = (UINT32)FixedPcdGet64(PcdUartBaudRate);

  /* 2) 璁惧鏍戜紭鍏?*/
  if (FixedPcdGetBool(PcdUseDeviceTree) && (DtBase != 0)) {
    if (T158FdtOpen (DtBase, &Ctx)) {
      mDeviceTreeBase = DtBase;

      Status = T158FdtGetMemory (&Ctx, &Base, &Size);
      if (!EFI_ERROR (Status) && (Size != 0)) {
        mSystemMemoryBase = Base;
        /* 鍙悜 UEFI 鏆撮湶鍓?PcdSystemMemorySize 瀛楄妭, 閬垮厤 8GB 椤佃〃寮€閿€ */
        if (Size > FixedPcdGet64(PcdSystemMemorySize)) {
          Size = FixedPcdGet64(PcdSystemMemorySize);
        }

        mSystemMemorySize = Size;
      }

      Baud = 0;
      Status = T158FdtGetConsoleUart (&Ctx, &Base, &Baud);
      if (!EFI_ERROR (Status) && (Base != 0)) {
        mUartBase = Base;
        if (Baud != 0) {
          mUartBaud = Baud;
        }
      }
    }
  }

  /* 3) 璧蜂覆鍙?(SEC 闃舵鐨?DEBUG 杈撳嚭渚濊禆瀹? */
  SerialPortInitialize ();
}

/**
  鍙栧钩鍙板唴瀛樹俊鎭€?
**/
VOID
EFIAPI
T158PlatformGetMemoryInfo (
  OUT UINT64  *Base,
  OUT UINT64  *Size
  )
{
  if (Base != NULL) {
    *Base = mSystemMemoryBase;
  }

  if (Size != NULL) {
    *Size = mSystemMemorySize;
  }
}

/**
  鍙栨帶鍒跺彴涓插彛淇℃伅銆?
**/
VOID
EFIAPI
T158PlatformGetUartInfo (
  OUT UINT64  *Base,
  OUT UINT32  *BaudRate
  )
{
  if (Base != NULL) {
    *Base = mUartBase;
  }

  if (BaudRate != NULL) {
    *BaudRate = mUartBaud;
  }
}

UINT64
EFIAPI
T158PlatformGetDeviceTreeBase (
  VOID
  )
{
  return mDeviceTreeBase;
}

/**
  MMU 鏄犲皠琛? 琛ㄩ」椤哄簭鍗充紭鍏堢骇, 鏈€鍚庝竴椤?Length=0 琛ㄧず缁撴潫銆?
**/
ARM_MEMORY_REGION_DESCRIPTOR *
EFIAPI
T158PlatformGetVirtualMemoryMap (
  VOID
  )
{
  mMemoryTable[0].PhysicalBase = mSystemMemoryBase;
  mMemoryTable[0].VirtualBase  = mSystemMemoryBase;
  mMemoryTable[0].Length       = mSystemMemorySize;

  mMemoryTable[ARRAY_SIZE (mMemoryTable) - 2].PhysicalBase = FixedPcdGet64(PcdFdBaseAddress);
  mMemoryTable[ARRAY_SIZE (mMemoryTable) - 2].VirtualBase  = FixedPcdGet64(PcdFdBaseAddress);
  mMemoryTable[ARRAY_SIZE (mMemoryTable) - 2].Length       = FixedPcdGet32(PcdFdSize);

  return mMemoryTable;
}
