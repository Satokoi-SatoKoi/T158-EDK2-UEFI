/** @file
 *  T158 / UMS9621 骞冲彴 Boot Manager 绛栫暐銆?
 *
 *  鐩爣: 涓婄數 -> 涓插彛鎺у埗鍙板氨缁?-> 鐩存帴浠?FV 閲屾妸鍐呭祵鐨?Shell 璺戣捣鏉ャ€?
 *  涔嬫墍浠ヤ笉璧?"Boot#### 鍙橀噺 + Fv 璁惧璺緞" 閭ｅ: 鏈钩鍙版殏鏃犲彲鐢ㄧ殑闈炴槗澶卞彉閲忓瓨鍌?
 *  (娌℃湁 NorFlash/ RPMB 椹卞姩), BootOrder 鏃犳硶鎸佷箙鍖? 鐩存帴浠?FV 鍙?PE32 娈佃杞芥渶绋冲Ε,
 *  涔熸槸鍚勭被鎵嬫満 UEFI 绉绘鐨勯€氳鍋氭硶銆?
 **/

#include <Base.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Protocol/SerialIo.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/GraphicsOutput.h>
#include <T158Platform.h>

#define T158_SHELL_DESCRIPTION  L"T158 Internal UEFI Shell"

/**
  浠?FV 涓彇鍑?Shell 鐨?PE32 鏄犲儚骞跺惎鍔ㄣ€?
**/
STATIC
EFI_STATUS
StartBootMenu (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *ShellImage;
  UINTN       ShellImageSize;
  EFI_HANDLE  ShellHandle;
  UINTN       ExitDataSize;
  CHAR16      *ExitData;

  ShellImage     = NULL;
  ShellImageSize = 0;

  Status = GetSectionFromFv (
             &gT158BootMenuFileGuid,
             EFI_SECTION_PE32,
             0,
             &ShellImage,
             &ShellImageSize
             );
  if (EFI_ERROR (Status)) {
    /* 鏈変簺宸ュ叿閾句笅 Shell 鍙兘琚墦鍖呮垚 TE 娈? 鍏煎涓€娆?*/
    Status = GetSectionFromFv (
               &gT158BootMenuFileGuid,
               EFI_SECTION_TE,
               0,
               &ShellImage,
               &ShellImageSize
               );
  }

  if (EFI_ERROR (Status) || (ShellImage == NULL)) {
    DEBUG ((DEBUG_ERROR, "T158 BDS: 鏈兘鍦?FV 涓壘鍒板唴宓?Shell (GUID=%g) Status=%r\n",
            &gT158BootMenuFileGuid, Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "T158 BDS: 瑁呰浇鍐呭祵 Shell (0x%X 瀛楄妭)\n", ShellImageSize));

  Status = gBS->LoadImage (
                  FALSE,
                  gImageHandle,
                  NULL,
                  ShellImage,
                  ShellImageSize,
                  &ShellHandle
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "T158 BDS: LoadImage(Shell) 澶辫触 %r\n", Status));
    return Status;
  }

  ExitData     = NULL;
  ExitDataSize = 0;
  Status       = gBS->StartImage (ShellHandle, &ExitDataSize, &ExitData);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "T158 BDS: Shell 閫€鍑? 鐘舵€?%r\n", Status));
  }

  return Status;
}

/**
  鎺у埗鍙板缓绔嬩箣鍓? 杩炴帴鎵€鏈夎澶? 璁?SerialDxe/TerminalDxe 浜х敓涓插彛鎺у埗鍙般€?
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  //
  // 鏈钩鍙版病鏈?PCI / 澶嶆潅鎬荤嚎, 鐩存帴 ConnectAll 鍗冲彲鏋氫妇鍑轰覆鍙ｇ粓绔€?
  //
  EfiBootManagerConnectAll ();
}

/**
  鎺у埗鍙板缓绔嬩箣鍚? 鎵撳嵃妯箙骞惰繘鍏?Shell銆?
**/
VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  )
{
  EFI_STATUS                     Status;
  UINTN                          Index;
  EFI_HANDLE                     *Handles;
  UINTN                          HandleCount;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;

  Print (L"\n");
  Print (L"===============================================================\n");
  Print (L"  Unisoc T158 / UMS9621 (sprd,ums9621-1h10)  UEFI Payload\n");
  Print (L"  鎺у埗鍙? UART1 @0x%08X  115200-8-N-1\n", T158_UART1_BASE);
  Print (L"  DDR   : 0x%08X  UFS: 0x%08X  GICD: 0x%08X\n",
         FixedPcdGet64(PcdSystemMemoryBase), T158_UFS_BASE, T158_GICD_BASE);

  //
  // 屏幕: 帧缓冲 GOP (logobuffer) 是否已接入图形控制台
  //
  Gop = NULL;
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && (Gop != NULL) && (Gop->Mode != NULL) && (Gop->Mode->Info != NULL)) {
    Print (L"  屏幕  : GOP %dx%d @0x%lX  图形控制台已就绪(Shell 已显示在屏上)\n",
           Gop->Mode->Info->HorizontalResolution,
           Gop->Mode->Info->VerticalResolution,
           Gop->Mode->FrameBufferBase);
  } else {
    Print (L"  屏幕  : 未检测到 GOP, 输出仅走串口\n");
  }

  Print (L"===============================================================\n\n");

  //
  // 鍒楀嚭宸插氨缁殑涓插彛, 渚夸簬鐜板満纭纭欢杩炴帴
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSerialIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < HandleCount; Index++) {
      EFI_SERIAL_IO_PROTOCOL  *SerialIo;

      Status = gBS->HandleProtocol (
                      Handles[Index],
                      &gEfiSerialIoProtocolGuid,
                      (VOID **)&SerialIo
                      );
      if (!EFI_ERROR (Status)) {
        Print (L"  SerialIo[%d]: %d bps, FIFO %d\n",
               Index, SerialIo->Mode->BaudRate, SerialIo->Mode->ReceiveFifoDepth);
      }
    }

    FreePool (Handles);
  }

  if (!FixedPcdGetBool(PcdBootToShellFirst)) {
    return;
  }

  //
  // 寮€鏈哄嵆杩?Shell
  //
  Status = StartBootMenu ();
  if (EFI_ERROR (Status)) {
    Print (L"  鏈壘鍒版垨鏃犳硶鍚姩鍐呭祵 Shell, 宸茶繘鍏ラ粯璁?BDS 娴佺▼ (%r)\n", Status);
  }
}

/**
  鏃犱换浣曞彲鍚姩椤规椂 (Shell 閫€鍑?/ 鍚姩椤瑰叏澶辫触) 鍐嶆鎷夎捣 Shell銆?
**/
VOID
EFIAPI
PlatformBootManagerUnableToBoot (
  VOID
  )
{
  Print (L"鏃犲彲鍚姩璁惧, 閲嶆柊鎷夎捣鍐呭祵 Shell...\n");
  StartBootMenu ();
}

/**
  鍚姩鍊掕鏃跺洖璋冦€?
**/
VOID
EFIAPI
PlatformBootManagerWaitCallback (
  IN UINT16  TimeoutRemain
  )
{
  if (TimeoutRemain == 0) {
    return;
  }

  Print (L"鎸変换鎰忛敭杩涘叆鍛戒护琛?(%d 绉?...\n", TimeoutRemain);
}
