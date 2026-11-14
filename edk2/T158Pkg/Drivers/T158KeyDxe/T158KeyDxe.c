/** @file
 *  T158 按键输入驱动 (音量上/下 + 电源键 -> SimpleTextInput/Ex)
 *
 *  按键接线 (dtb/ums9621-1h10.dtb 取证):
 *    Volume Up   -> PMIC EIC (0x400000 ADI 桥 + EIC 0x280) 第 4 脚, 按下 = 高
 *    Power       -> PMIC EIC 第 1 脚, 按下 = 低
 *    Volume Down -> AON GPIO @0x170000 第 127 脚 (bank7 bit15), 按下 = 低
 *
 *  AON GPIO 寄存器 (drivers/gpio/gpio-sprd.c):
 *    每 bank 0x80 字节 / 16 脚, GPIO n -> bank n/16, bit n%16
 *    DATA@0x00 DMSK@0x04 DIR@0x08 INEN@0x28
 *
 *  PMIC EIC 寄存器 (drivers/gpio/gpio-pmic-eic-sprd.c):
 *    单 bank 16 脚, DATA@0x00 DMSK@0x04
 *
 *  输出: 音量上 = SCAN_UP, 音量下 = SCAN_DOWN, 电源 = 回车。
 *  沿触发: 按一次只报一次, 长按不重复, 适合菜单。
 **/

#include <PiDxe.h>
#include <Uefi.h>

#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/DevicePath.h>

#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>

#define T158_KEY_SIGNATURE   SIGNATURE_32 ('T', '1', '5', 'K')

#define T158_CONIN_VENDOR_GUID \
  { 0x2C8F4A61, 0x5D97, 0x4B03, { 0x9E, 0x7A, 0x6F, 0x1C, 0x8D, 0x2B, 0x4E, 0x55 } }

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH         Vendor;
  EFI_DEVICE_PATH_PROTOCOL   End;
} T158_CONIN_DEVICE_PATH;
#pragma pack()

STATIC T158_CONIN_DEVICE_PATH  mKeyDevicePath = {
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
  UINTN                              Signature;
  EFI_HANDLE                         Handle;

  EFI_SIMPLE_TEXT_INPUT_PROTOCOL     ConIn;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  ConInEx;
  EFI_EVENT                          WaitKey;
  EFI_EVENT                          WaitKeyEx;

  EFI_KEY_DATA                       Pending;
  BOOLEAN                            PendingValid;

  /* 按键状态 (消抖后的电平) */
  BOOLEAN                            VolUpDown;    /* TRUE = 按下 */
  BOOLEAN                            VolDownDown;
  BOOLEAN                            PowerDown;
} T158_KEY_DEV;

#define T158_KEY_FROM_CONIN(a)    CR (a, T158_KEY_DEV, ConIn, T158_KEY_SIGNATURE)
#define T158_KEY_FROM_CONIN_EX(a) CR (a, T158_KEY_DEV, ConInEx, T158_KEY_SIGNATURE)

/* ------------------------------------------------------- 硬件寄存器访问 */

/**
  读 AON GPIO 电平 (sprd,sc9860-gpio)。
**/
STATIC
BOOLEAN
ReadAonGpio (
  IN UINTN  Gpio
  )
{
  UINTN  BankBase;
  UINT32 Bit;

  BankBase = (UINTN)FixedPcdGet64 (PcdKeyAonGpioBase) + (Gpio / 16) * 0x80;
  Bit      = Gpio % 16;

  return ((MmioRead32 (BankBase + 0x00) >> Bit) & 1) != 0;   /* DATA */
}

/**
  初始化 AON GPIO 为输入 (DMSK=1, DIR=0, INEN=1)。
**/
STATIC
VOID
InitAonGpio (
  IN UINTN  Gpio
  )
{
  UINTN  BankBase;
  UINT32 Bit;
  UINT32 Val;

  BankBase = (UINTN)FixedPcdGet64 (PcdKeyAonGpioBase) + (Gpio / 16) * 0x80;
  Bit      = Gpio % 16;

  Val = MmioRead32 (BankBase + 0x04);   /* DMSK */
  MmioWrite32 (BankBase + 0x04, Val | (1U << Bit));

  Val = MmioRead32 (BankBase + 0x08);   /* DIR */
  MmioWrite32 (BankBase + 0x08, Val & ~(1U << Bit));

  Val = MmioRead32 (BankBase + 0x28);   /* INEN */
  MmioWrite32 (BankBase + 0x28, Val | (1U << Bit));
}

/**
  读 PMIC EIC 电平 (经 ADI 桥, PMIC 寄存器号 * stride = MMIO 偏移)。
**/
STATIC
BOOLEAN
ReadPmicEic (
  IN UINT32  Bit
  )
{
  UINTN  DataReg;

  DataReg = (UINTN)FixedPcdGet64 (PcdKeyPmicAdiBase)
            + (UINTN)FixedPcdGet32 (PcdKeyPmicEicOffset) * FixedPcdGet32 (PcdKeyPmicRegStride)
            + 0x00;                                     /* DATA */

  return ((MmioRead32 (DataReg) >> (Bit & 15)) & 1) != 0;
}

/**
  使能 PMIC EIC (DMSK 置 1)。
**/
STATIC
VOID
InitPmicEic (
  IN UINT32  Bit
  )
{
  UINTN  MskReg;
  UINT32 Val;

  MskReg = (UINTN)FixedPcdGet64 (PcdKeyPmicAdiBase)
           + (UINTN)FixedPcdGet32 (PcdKeyPmicEicOffset) * FixedPcdGet32 (PcdKeyPmicRegStride)
           + 0x04;                                      /* DMSK */

  Val = MmioRead32 (MskReg);
  MmioWrite32 (MskReg, Val | (1U << (Bit & 15)));
}

/* ------------------------------------------------------- 按键状态采样 */

typedef enum {
  T158_KEY_NONE,
  T158_KEY_UP,
  T158_KEY_DOWN,
  T158_KEY_ENTER
} T158_KEY_EVENT;

/**
  采样三个按键, 返回本次检测到的"新按下"事件 (沿触发)。
**/
STATIC
T158_KEY_EVENT
SampleKeys (
  IN OUT T158_KEY_DEV  *Dev
  )
{
  BOOLEAN  VolUp;
  BOOLEAN  VolDown;
  BOOLEAN  Power;

  VolUp   =  ReadPmicEic (FixedPcdGet32 (PcdKeyVolUpBit));                     /* active high */
  Power   = !ReadPmicEic (FixedPcdGet32 (PcdKeyPowerBit));                     /* active low  */
  VolDown = !ReadAonGpio (FixedPcdGet32 (PcdKeyVolDownGpio));                  /* active low  */

  if ((VolUp != Dev->VolUpDown) || (VolDown != Dev->VolDownDown) ||
      (Power != Dev->PowerDown))
  {
    /* 电平变化: 不立即上报, 先更新状态, 由下一次采样确认 (软件消抖) */
    Dev->VolUpDown   = VolUp;
    Dev->VolDownDown = VolDown;
    Dev->PowerDown   = Power;
    return T158_KEY_NONE;
  }

  if (VolUp) {
    Dev->VolUpDown = FALSE;        /* 报一次, 松开前不再报 */
    return T158_KEY_UP;
  }

  if (VolDown) {
    Dev->VolDownDown = FALSE;
    return T158_KEY_DOWN;
  }

  if (Power) {
    Dev->PowerDown = FALSE;
    return T158_KEY_ENTER;
  }

  return T158_KEY_NONE;
}

STATIC
VOID
SetPendingKey (
  IN OUT T158_KEY_DEV  *Dev,
  IN     T158_KEY_EVENT  KeyEvent
  )
{
  ZeroMem (&Dev->Pending, sizeof (Dev->Pending));

  switch (KeyEvent) {
    case T158_KEY_UP:
      Dev->Pending.Key.ScanCode = SCAN_UP;
      break;

    case T158_KEY_DOWN:
      Dev->Pending.Key.ScanCode = SCAN_DOWN;
      break;

    case T158_KEY_ENTER:
      Dev->Pending.Key.UnicodeChar = CHAR_CARRIAGE_RETURN;
      break;

    default:
      return;
  }

  Dev->PendingValid = TRUE;
}

/**
  采样 + 填充 pending。
**/
STATIC
VOID
PollKeys (
  IN OUT T158_KEY_DEV  *Dev
  )
{
  T158_KEY_EVENT  KeyEvent;

  if (Dev->PendingValid) {
    return;
  }

  KeyEvent = SampleKeys (Dev);
  if (KeyEvent != T158_KEY_NONE) {
    SetPendingKey (Dev, KeyEvent);
  }
}

/* ------------------------------------------------------- ConIn / ConInEx */

STATIC
EFI_STATUS
EFIAPI
T158KeyResetEx (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN BOOLEAN                            ExtendedVerification
  )
{
  T158_KEY_DEV  *Dev;

  Dev = T158_KEY_FROM_CONIN_EX (This);
  Dev->PendingValid = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
T158KeyReadKeyStrokeEx (
  IN  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  OUT EFI_KEY_DATA                       *KeyData
  )
{
  T158_KEY_DEV  *Dev;

  if (KeyData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = T158_KEY_FROM_CONIN_EX (This);
  PollKeys (Dev);

  if (!Dev->PendingValid) {
    return EFI_NOT_READY;
  }

  CopyMem (KeyData, &Dev->Pending, sizeof (*KeyData));
  Dev->PendingValid = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
T158KeySetState (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN EFI_KEY_TOGGLE_STATE               *KeyToggleState
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
T158KeyRegisterNotify (
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
T158KeyUnregisterNotify (
  IN EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *This,
  IN VOID                               *NotificationHandle
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
T158KeyReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  T158_KEY_DEV  *Dev;

  Dev = T158_KEY_FROM_CONIN (This);
  Dev->PendingValid = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
T158KeyReadKeyStroke (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                   *Key
  )
{
  T158_KEY_DEV  *Dev;
  EFI_KEY_DATA  KeyData;
  EFI_STATUS    Status;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = T158_KEY_FROM_CONIN (This);

  Status = T158KeyReadKeyStrokeEx (&Dev->ConInEx, &KeyData);
  if (!EFI_ERROR (Status)) {
    CopyMem (Key, &KeyData.Key, sizeof (*Key));
  }

  return Status;
}

/**
  WaitForKey / WaitForKeyEx 通知函数: 轮询按键, 有新键按下则 signal。
**/
STATIC
VOID
EFIAPI
T158KeyWaitNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  T158_KEY_DEV  *Dev;

  Dev = (T158_KEY_DEV *)Context;

  if (Dev->PendingValid) {
    gBS->SignalEvent (Event);
    return;
  }

  PollKeys (Dev);
  if (Dev->PendingValid) {
    gBS->SignalEvent (Event);
  }
}

/**
  驱动入口。
**/
EFI_STATUS
EFIAPI
T158KeyDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS     Status;
  T158_KEY_DEV   *Dev;

  InitPmicEic (FixedPcdGet32 (PcdKeyVolUpBit));
  InitPmicEic (FixedPcdGet32 (PcdKeyPowerBit));
  InitAonGpio (FixedPcdGet32 (PcdKeyVolDownGpio));

  /* 初始化按键状态, 避免开机瞬间误报 */
  Dev = AllocateZeroPool (sizeof (T158_KEY_DEV));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature = T158_KEY_SIGNATURE;
  SampleKeys (Dev);

  Dev->ConIn.Reset         = T158KeyReset;
  Dev->ConIn.ReadKeyStroke = T158KeyReadKeyStroke;

  Dev->ConInEx.Reset               = T158KeyResetEx;
  Dev->ConInEx.ReadKeyStrokeEx     = T158KeyReadKeyStrokeEx;
  Dev->ConInEx.SetState            = T158KeySetState;
  Dev->ConInEx.RegisterKeyNotify   = T158KeyRegisterNotify;
  Dev->ConInEx.UnregisterKeyNotify = T158KeyUnregisterNotify;

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, T158KeyWaitNotify, Dev, &Dev->WaitKey);
  if (EFI_ERROR (Status)) {
    FreePool (Dev);
    return Status;
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, T158KeyWaitNotify, Dev, &Dev->WaitKeyEx);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Dev->WaitKey);
    FreePool (Dev);
    return Status;
  }

  Dev->ConIn.WaitForKey     = Dev->WaitKey;
  Dev->ConInEx.WaitForKeyEx = Dev->WaitKeyEx;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Dev->Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mKeyDevicePath,
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

  DEBUG ((DEBUG_INFO, "T158Key: 音量上/下 + 电源键已接入 ConIn\n"));
  return EFI_SUCCESS;
}
