/** @file
 *  T158 / UMS9621 帧缓冲 GOP 驱动 (SprdGopDxe)
 *
 *  原理: 原厂 U-Boot 在跳转前已经点亮 DSI/DPU, 显存就是 DT 里
 *        /reserved-memory/logobuffer@be000000 (0xBE000000, 0x9E4000 字节)。
 *        进入 UEFI 后屏幕仍在持续扫描这块显存, 所以这里不需要重新初始化
 *        显示控制器, 只把它发布成 EFI_GRAPHICS_OUTPUT_PROTOCOL 即可。
 *
 *  关于缩放: UEFI 图形控制台的字模是 8x19 像素。手机屏是 1080x2400 级别,
 *        直接 1:1 显示每个字只有 2mm 高, 根本看不清。所以这里向 UEFI 报告
 *        "逻辑分辨率 = 物理分辨率 / PcdGopScale", 所有 Blt 先画在 shadow
 *        缓冲里, 再按 PcdGopScale 倍最近邻放大刷到物理显存。
 *        scale=2 -> 540x1200 -> 67 列 x 63 行, 字高 38 物理像素
 *        scale=3 -> 360x800  -> 45 列 x 42 行, 字高 57 物理像素
 **/

#include <PiDxe.h>
#include <Uefi.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/FrameBufferBltLib.h>

#define T158_GOP_VENDOR_GUID \
  { 0x4A2F1E9C, 0x3B5D, 0x884E, { 0xA7, 0x21, 0x0F, 0x6D, 0x8B, 0x3E, 0x2C, 0x51 } }

#define GOP_CONFIG_POOL_SIZE   0x20000      /* FrameBufferBltConfigure 工作区 */
#define GOP_BYTES_PER_PIXEL    4

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH         Vendor;
  EFI_DEVICE_PATH_PROTOCOL   End;
} T158_GOP_DEVICE_PATH;
#pragma pack()

STATIC T158_GOP_DEVICE_PATH  mGopDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    T158_GOP_VENDOR_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(sizeof (EFI_DEVICE_PATH_PROTOCOL)),
      (UINT8)((sizeof (EFI_DEVICE_PATH_PROTOCOL)) >> 8)
    }
  }
};

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mGopModeInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     mGopMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL          mGop;
STATIC FRAME_BUFFER_CONFIGURE                *mBltConfig = NULL;
STATIC EFI_HANDLE                            mGopHandle  = NULL;

/* 物理显存 (U-Boot 正在扫描的那块) */
STATIC UINT8                                 *mPhysFb    = NULL;
STATIC UINT32                                mPhysWidth;
STATIC UINT32                                mPhysHeight;
STATIC UINT32                                mPhysStride;   /* 字节 */

/* 逻辑帧缓冲 (UEFI 看到的那块, 由 PcdGopScale 缩放) */
STATIC UINT8                                 *mShadowFb   = NULL;
STATIC UINT32                                mScale;

/**
  把 shadow 缓冲里的 (X,Y,W,H) 按 mScale 倍最近邻放大, 刷到物理显存。
**/
STATIC
VOID
SprdGopFlush (
  IN UINTN  X,
  IN UINTN  Y,
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  UINT32   Row;
  UINT32   Col;
  UINT32   RepY;
  UINT32   RepX;
  UINT32   YEnd;
  UINT32   XEnd;
  UINT32   *Src;
  UINT32   *Dst;
  UINT32   Pixel;
  UINT32   PhysRow;

  if ((mScale == 0) || (mShadowFb == NULL) || (mPhysFb == NULL)) {
    return;
  }

  if (X >= mGopModeInfo.HorizontalResolution) {
    return;
  }

  if (Y >= mGopModeInfo.VerticalResolution) {
    return;
  }

  XEnd = (UINT32)MIN (X + Width, mGopModeInfo.HorizontalResolution);
  YEnd = (UINT32)MIN (Y + Height, mGopModeInfo.VerticalResolution);

  for (Row = (UINT32)Y; Row < YEnd; Row++) {
    Src = (UINT32 *)(mShadowFb + (Row * mGopModeInfo.PixelsPerScanLine * GOP_BYTES_PER_PIXEL));

    for (RepY = 0; RepY < mScale; RepY++) {
      PhysRow = Row * mScale + RepY;
      if (PhysRow >= mPhysHeight) {
        break;
      }

      Dst = (UINT32 *)(mPhysFb + (PhysRow * mPhysStride));

      for (Col = (UINT32)X; Col < XEnd; Col++) {
        Pixel = Src[Col];

        for (RepX = 0; RepX < mScale; RepX++) {
          if ((Col * mScale + RepX) >= mPhysWidth) {
            break;
          }

          Dst[Col * mScale + RepX] = Pixel;
        }
      }
    }
  }
}

/**
  查询显示模式。本驱动只有固定一个模式 (由 PCD 决定)。
**/
STATIC
EFI_STATUS
EFIAPI
SprdGopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL           *This,
  IN  UINT32                                 ModeNumber,
  OUT UINTN                                  *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   **Info
  )
{
  if ((This == NULL) || (Info == NULL) || (SizeOfInfo == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (ModeNumber >= mGopMode.MaxMode) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateCopyPool (sizeof (mGopModeInfo), &mGopModeInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (mGopModeInfo);
  return EFI_SUCCESS;
}

/**
  切换显示模式。只有模式 0 可用 (硬件已被 U-Boot 配好)。
**/
STATIC
EFI_STATUS
EFIAPI
SprdGopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  if ((This == NULL) || (ModeNumber >= mGopMode.MaxMode)) {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/**
  Blt: 画到 shadow 缓冲, 再放大刷到物理显存。
**/
STATIC
EFI_STATUS
EFIAPI
SprdGopBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer   OPTIONAL,
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation,
  IN  UINTN                              SourceX,
  IN  UINTN                              SourceY,
  IN  UINTN                              DestinationX,
  IN  UINTN                              DestinationY,
  IN  UINTN                              Width,
  IN  UINTN                              Height,
  IN  UINTN                              Delta        OPTIONAL
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || (mBltConfig == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BltOperation < EfiBltVideoFill) || (BltOperation >= EfiGraphicsOutputBltOperationMax)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FrameBufferBlt (
             mBltConfig,
             BltBuffer,
             BltOperation,
             SourceX,
             SourceY,
             DestinationX,
             DestinationY,
             Width,
             Height,
             Delta
             );

  if (!EFI_ERROR (Status) && (BltOperation != EfiBltVideoToBltBuffer)) {
    SprdGopFlush (DestinationX, DestinationY, Width, Height);
  }

  return Status;
}

/**
  把整屏填成黑色, 擦掉 U-Boot 的开机 logo。
**/
STATIC
VOID
SprdGopClearScreen (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;

  Black.Red      = 0x00;
  Black.Green    = 0x00;
  Black.Blue     = 0x00;
  Black.Reserved = 0x00;

  FrameBufferBlt (
    mBltConfig,
    &Black,
    EfiBltVideoFill,
    0,
    0,
    0,
    0,
    mGopModeInfo.HorizontalResolution,
    mGopModeInfo.VerticalResolution,
    0
    );

  SprdGopFlush (0, 0, mGopModeInfo.HorizontalResolution, mGopModeInfo.VerticalResolution);
}

/**
  驱动入口。
**/
EFI_STATUS
EFIAPI
SprdGopDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  FbAddr;
  UINTN                 FbSize;
  UINT32                PhysWidth;
  UINT32                PhysHeight;
  UINT32                LogWidth;
  UINT32                LogHeight;
  UINT32                Stride;
  UINT32                Format;
  UINTN                 ConfigSize;

  FbAddr     = FixedPcdGet64 (PcdFrameBufferBase);
  FbSize     = FixedPcdGet32 (PcdFrameBufferSize);
  PhysWidth  = FixedPcdGet32 (PcdScreenWidth);
  PhysHeight = FixedPcdGet32 (PcdScreenHeight);
  Stride     = FixedPcdGet32 (PcdScreenStride);
  Format     = FixedPcdGet32 (PcdScreenPixelFormat);
  mScale     = FixedPcdGet32 (PcdGopScale);

  if ((FbAddr == 0) || (FbSize == 0) || (PhysWidth == 0) || (PhysHeight == 0)) {
    DEBUG ((DEBUG_WARN, "SprdGop: PCD 未配置, 屏幕输出不可用\n"));
    return EFI_UNSUPPORTED;
  }

  if (mScale == 0) {
    mScale = 1;
  }

  switch (Format) {
    case 1:
      mGopModeInfo.PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
      break;
    case 2:
      mGopModeInfo.PixelFormat = PixelBltOnly;
      break;
    default:
      mGopModeInfo.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
      break;
  }

  if (Stride == 0) {
    Stride = PhysWidth * GOP_BYTES_PER_PIXEL;
  }

  //
  // 物理显存放不下就砍高度, 避免越界写坏相邻内存
  //
  if ((UINT64)Stride * PhysHeight > FbSize) {
    PhysHeight = (UINT32)(FbSize / Stride);
    DEBUG ((DEBUG_WARN, "SprdGop: 显存不足, 物理高度裁剪为 %d\n", PhysHeight));
  }

  if ((PhysWidth == 0) || (PhysHeight == 0)) {
    DEBUG ((DEBUG_ERROR, "SprdGop: 显存 0x%X 字节太小, GOP 未安装\n", FbSize));
    return EFI_UNSUPPORTED;
  }

  //
  // 1) 占位: 防止被其它驱动当普通内存分配走
  //    (SEC 通常已把它报成 EfiReservedMemoryType, 失败属预期)
  //
  FbAddr = FbAddr & ~(EFI_PAGE_MASK);
  Status = gBS->AllocatePages (
                  AllocateAddress,
                  EfiReservedMemoryType,
                  EFI_SIZE_TO_PAGES (FbSize),
                  &FbAddr
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_VERBOSE, "SprdGop: AllocatePages(0x%LX) 返回 %r (已保留则忽略)\n",
            FbAddr, Status));
  }

  //
  // 2) 改成 Uncacheable: DPU 是独立观察者, 走 cache 会看不到写入
  //
  Status = gDS->SetMemorySpaceAttributes (
                  FbAddr,
                  ALIGN_VALUE (FbSize, EFI_PAGE_SIZE),
                  EFI_MEMORY_UC
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SprdGop: SetMemorySpaceAttributes(UC) 失败 %r\n", Status));
  }

  mPhysFb     = (UINT8 *)(UINTN)FbAddr;
  mPhysWidth  = PhysWidth;
  mPhysHeight = PhysHeight;
  mPhysStride = Stride;

  //
  // 3) 逻辑分辨率 (上报给 UEFI 的, 会被 PcdGopScale 倍放大上屏)
  //
  LogWidth  = PhysWidth / mScale;
  LogHeight = PhysHeight / mScale;

  if ((LogWidth == 0) || (LogHeight == 0)) {
    mScale    = 1;
    LogWidth  = PhysWidth;
    LogHeight = PhysHeight;
  }

  mGopModeInfo.Version              = 0;
  mGopModeInfo.HorizontalResolution = LogWidth;
  mGopModeInfo.VerticalResolution   = LogHeight;
  mGopModeInfo.PixelsPerScanLine    = LogWidth;
  ZeroMem (&mGopModeInfo.PixelInformation, sizeof (mGopModeInfo.PixelInformation));

  //
  // 4) shadow 缓冲 + Blt 工作区
  //
  mShadowFb = AllocatePool ((UINTN)LogWidth * LogHeight * GOP_BYTES_PER_PIXEL);
  if (mShadowFb == NULL) {
    DEBUG ((DEBUG_ERROR, "SprdGop: shadow 缓冲分配失败 (%dx%d)\n", LogWidth, LogHeight));
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (mShadowFb, (UINTN)LogWidth * LogHeight * GOP_BYTES_PER_PIXEL);

  ConfigSize = GOP_CONFIG_POOL_SIZE;
  mBltConfig = AllocatePool (ConfigSize);
  if (mBltConfig == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = FrameBufferBltConfigure (mShadowFb, &mGopModeInfo, mBltConfig, &ConfigSize);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    FreePool (mBltConfig);
    mBltConfig = AllocatePool (ConfigSize);
    if (mBltConfig == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = FrameBufferBltConfigure (mShadowFb, &mGopModeInfo, mBltConfig, &ConfigSize);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SprdGop: FrameBufferBltConfigure 失败 %r\n", Status));
    FreePool (mBltConfig);
    mBltConfig = NULL;
    return Status;
  }

  mGopMode.MaxMode         = 1;
  mGopMode.Mode            = 0;
  mGopMode.Info            = &mGopModeInfo;
  mGopMode.SizeOfInfo      = sizeof (mGopModeInfo);
  mGopMode.FrameBufferBase = FbAddr;
  mGopMode.FrameBufferSize = ALIGN_VALUE (FbSize, EFI_PAGE_SIZE);

  mGop.QueryMode = SprdGopQueryMode;
  mGop.SetMode   = SprdGopSetMode;
  mGop.Blt       = SprdGopBlt;
  mGop.Mode      = &mGopMode;

  //
  // 5) 安装 GOP (带设备路径, GraphicsConsoleDxe 才能把它做成 ConOut)
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mGopHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mGopDevicePath,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SprdGop: 安装 GOP 失败 %r\n", Status));
    return Status;
  }

  SprdGopClearScreen ();

  DEBUG ((DEBUG_INFO, "SprdGop: GOP 已安装 物理 %dx%d -> 逻辑 %dx%d (x%d), FB=0x%LX\n",
          PhysWidth, PhysHeight, LogWidth, LogHeight, mScale, FbAddr));

  return EFI_SUCCESS;
}
