/** @file
 *  T158 串口控制台输入驱动
 *
 *  把一个 SerialIo 包装成 SimpleTextInput / SimpleTextInputEx。
 *  只装输入、不装输出, 于是:
 *
 *      ConIn  = 串口   (电脑端串口终端敲命令)
 *      ConOut = 屏幕   (帧缓冲 GOP + GraphicsConsoleDxe)
 *
 *  这样屏幕上的 Shell 可以铺满全屏, 不受串口终端 80x25 的拖累。
 **/

#include <PiDxe.h>
#include <Uefi.h>

#include <Protocol/SerialIo.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/DevicePath.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define SERIAL_CONIN_SIGNATURE   SIGNATURE_32 ('T', '1', '5', 'K')

#define T158_CONIN_VENDOR_GUID \
  { 0x7B3D5A24, 0x91C8, 0x4F6E, { 0x8E, 0x10, 0x2D, 0x5A, 0x9C, 0x4B, 0x7F, 0x33 } }

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH         Vendor;
  EFI_DEVICE_PATH_PROTOCOL   End;
} T158_CONIN_DEVICE_PATH;
#pragma pack()

STATIC T158_CONIN_DEVICE_PATH  mConInDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    T158_CONIN_VENDOR_GUID
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

typedef struct {
  UINTN                             Signature;
  EFI_HANDLE                        Handle;
  EFI_SERIAL_IO_PROTOCOL            *SerialIo;

  EFI_SIMPLE_TEXT_INPUT_PROTOCOL    ConIn;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL ConInEx;

  EFI_EVENT                         WaitKey;
  EFI_EVENT                         WaitKeyEx;

  EFI_KEY_DATA                      Pending;     /* 预取到的一个按键 */
  BOOLEAN                           PendingValid;
} SERIAL_CONIN_DEV;

#define SERIAL_CONIN_FROM_CONIN(a)    CR (a, SERIAL_CONIN_DEV, ConIn, SERIAL_CONIN_SIGNATURE)
#define SERIAL_CONIN_FROM_CONIN_EX(a) CR (a, SERIAL_CONIN_DEV, ConInEx, SERIAL_CONIN_SIGNATURE)

/**
  从串口取一个字节。取不到返回 FALSE。
**/
STATIC
BOOLEAN
SerialConInGetByte (
  IN  SERIAL_CONIN_DEV  *Dev,
  OUT CHAR8             *Byte
  )
{
  EFI_STATUS  Status;
  UINTN       Size;

  Size   = 1;
  Status = Dev->SerialIo->Read (Dev->SerialIo, &Size, Byte);

  if (EFI_ERROR (Status) && (Status != EFI_TIMEOUT)) {
    return FALSE;
  }

  return (Size == 1);
}

/**
  把串口字节翻译成 EFI_KEY_DATA。
**/
STATIC
VOID
SerialConInTranslate (
  IN  CHAR8          Byte,
  OUT EFI_KEY_DATA   *KeyData
  )
{
  ZeroMem (KeyData, sizeof (*KeyData));

  KeyData->Key.ScanCode    = SCAN_NULL;
  KeyData->Key.UnicodeChar  = (CHAR16)(UINT8)Byte;

  switch (Byte) {
    case 0x0D:                    /* CR */
    case 0x0A:                    /* LF */
      KeyData->Key.UnicodeChar = CHAR_CARRIAGE_RETURN;
      break;

    case 0x08:                    /* BS */
      KeyData->Key.UnicodeChar = CHAR_BACKSPACE;
      break;

    case 0x7F:                    /* DEL */
      KeyData->Key.UnicodeChar = CHAR_BACKSPACE;
      break;

    case 0x09:                    /* TAB */
      KeyData->Key.UnicodeChar = 0x09;
      break;

    default:
      if ((UINT8)Byte < 0x20) {
        /* 其余控制字符原样给出 */
        KeyData->Key.UnicodeChar = (CHAR16)(UINT8)Byte;
      }

      break;
  }

  KeyData->KeyState.KeyShiftState  = 0;
  KeyData->KeyState.KeyToggleState = 0;
}

/**
  WaitForKey / WaitForKeyEx 的通知函数: 在有人等待按键时轮询串口。
**/
STATIC
VOID
EFIAPI
SerialConInWaitNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  SERIAL_CONIN_DEV  *Dev;
  CHAR8             Byte;

  Dev = (SERIAL_CONIN_DEV *)Context;

  if (Dev->PendingValid) {
    gBS->SignalEvent (Event);
    return;
  }

  if (SerialConInGetByte (Dev, &Byte)) {
    SerialConInTranslate (Byte, &Dev->Pending);
    Dev->PendingValid = TRUE;
    gBS->SignalEvent (Event);
  }
}

/* ------------------------------------------------ SimpleTextInputEx 接口 */

STATIC
EFI_STATUS
EFIAPI
SerialConInResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  SERIAL_CONIN_DEV  *Dev;

  Dev = SERIAL_CONIN_FROM_CONIN_EX (This);
  Dev->PendingValid = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SerialConInReadKeyStrokeEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  SERIAL_CONIN_DEV  *Dev;
  CHAR8             Byte;

  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = SERIAL_CONIN_FROM_CONIN_EX (This);

  if (Dev->PendingValid) {
    CopyMem (KeyData, &Dev->Pending, sizeof (*KeyData));
    Dev->PendingValid = FALSE;
    return EFI_SUCCESS;
  }

  if (!SerialConInGetByte (Dev, &Byte)) {
    return EFI_NOT_READY;
  }

  SerialConInTranslate (Byte, KeyData);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SerialConInSetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SerialConInRegisterKeyNotify (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN  EFI_KEY_DATA                       *KeyData,
  IN  EFI_KEY_NOTIFY_FUNCTION            KeyNotificationFunction,
  OUT VOID                               **NotifyHandle
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
SerialConInUnregisterKeyNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  return EFI_UNSUPPORTED;
}

/* ---------------------------------------------------- SimpleTextIn 接口 */

STATIC
EFI_STATUS
EFIAPI
SerialConInReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  SERIAL_CONIN_DEV  *Dev;

  Dev = SERIAL_CONIN_FROM_CONIN (This);
  Dev->PendingValid = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SerialConInReadKeyStroke (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  SERIAL_CONIN_DEV  *Dev;
  EFI_KEY_DATA      KeyData;
  EFI_STATUS        Status;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = SERIAL_CONIN_FROM_CONIN (This);

  Status = SerialConInReadKeyStrokeEx (&Dev->ConInEx, &KeyData);
  if (!EFI_ERROR (Status)) {
    CopyMem (Key, &KeyData.Key, sizeof (*Key));
  }

  return Status;
}

/**
  为一个 SerialIo 句柄创建输入设备。
**/
STATIC
EFI_STATUS
SerialConInCreate (
  IN EFI_SERIAL_IO_PROTOCOL  *SerialIo
  )
{
  EFI_STATUS         Status;
  SERIAL_CONIN_DEV   *Dev;

  Dev = AllocateZeroPool (sizeof (SERIAL_CONIN_DEV));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature   = SERIAL_CONIN_SIGNATURE;
  Dev->SerialIo    = SerialIo;
  Dev->PendingValid = FALSE;

  Dev->ConIn.Reset          = SerialConInReset;
  Dev->ConIn.ReadKeyStroke  = SerialConInReadKeyStroke;

  Dev->ConInEx.Reset                 = SerialConInResetEx;
  Dev->ConInEx.ReadKeyStrokeEx       = SerialConInReadKeyStrokeEx;
  Dev->ConInEx.SetState              = SerialConInSetState;
  Dev->ConInEx.RegisterKeyNotify     = SerialConInRegisterKeyNotify;
  Dev->ConInEx.UnregisterKeyNotify   = SerialConInUnregisterKeyNotify;

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_WAIT,
                  TPL_NOTIFY,
                  SerialConInWaitNotify,
                  Dev,
                  &Dev->WaitKey
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Dev);
    return Status;
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_WAIT,
                  TPL_NOTIFY,
                  SerialConInWaitNotify,
                  Dev,
                  &Dev->WaitKeyEx
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Dev->WaitKey);
    FreePool (Dev);
    return Status;
  }

  Dev->ConIn.WaitForKey    = Dev->WaitKey;
  Dev->ConInEx.WaitForKeyEx = Dev->WaitKeyEx;

  Dev->Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Dev->Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mConInDevicePath,
                  &gEfiSimpleTextInProtocolGuid,
                  &Dev->ConIn,
                  &gEfiSimpleTextInputExProtocolGuid,
                  &Dev->ConInEx,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Dev->WaitKey);
    gBS->CloseEvent (Dev->WaitKeyEx);
    FreePool (Dev);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SerialConIn: 串口输入设备已安装 @%d bps (仅 ConIn)\n",
          SerialIo->Mode->BaudRate));

  return EFI_SUCCESS;
}

/**
  驱动入口: 给每个 SerialIo 句柄装上输入协议。
**/
EFI_STATUS
EFIAPI
SprdSerialConInInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                  Status;
  UINTN                       HandleCount;
  EFI_HANDLE                  *Handles;
  UINTN                       Index;
  EFI_SERIAL_IO_PROTOCOL      *SerialIo;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSerialIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "SerialConIn: 没有找到 SerialIo, 串口输入不可用\n"));
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiSerialIoProtocolGuid,
                    (VOID **)&SerialIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    SerialConInCreate (SerialIo);
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}
