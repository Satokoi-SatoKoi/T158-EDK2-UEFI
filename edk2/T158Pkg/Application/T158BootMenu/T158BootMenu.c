/** @file
 *  T158 GRUB 风格启动菜单 (仿 UniXNU 截图)
 *
 *  ┌────────────────────────────┐
 *  │       T158 Boot Menu       │   标题居中, 白字
 *  │        version 0.1.0       │   副标题, 浅灰
 *  │   built-in configuration   │
 *  │  ┌──┐                      │
 *  │  │1 │ EFI loader (WinPE)   │   选中项: 青底黑字 (高亮块)
 *  │  │  │ efi                  │   未选中: 蓝底浅灰
 *  │  └──┘                      │
 *  │  ...                       │
 *  │       Select an entry      │
 *  │  Up/Down: select Enter:boot│
 *  │  1-4: boot  Esc: stop timer│
 *  └────────────────────────────┘
 *
 *  输入来自 ConIn (ConSplitter 合并了 音量键/电源键 和 串口)。
 **/

#include <Uefi.h>
#include <Pi/PiFirmwareFile.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/T158BootControlLib.h>
#include <Library/T158LinuxBootLib.h>

#define MENU_VERSION_STRING     L"version 0.3.0"
#define MENU_CONFIG_STRING      L"built-in configuration"
#define MENU_TITLE_STRING       L"T158 Boot Menu"
#define MENU_TIMER_SECONDS      3

/* 菜单项编号 */
#define MENU_ENTRY_WINPE        0
#define MENU_ENTRY_SHELL        1
#define MENU_ENTRY_ANDROID      2
#define MENU_ENTRY_RECOVERY     3
#define MENU_ENTRY_RESTART      4
#define MENU_ENTRY_POWEROFF     5
#define MENU_ENTRY_MAX          6

#define ATTR_NORMAL   EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BACKGROUND_BLUE)
#define ATTR_TITLE    EFI_TEXT_ATTR (EFI_WHITE,      EFI_BACKGROUND_BLUE)
#define ATTR_SUB      EFI_TEXT_ATTR (EFI_LIGHTGRAY,  EFI_BACKGROUND_BLUE)
#define ATTR_SEL      EFI_TEXT_ATTR (EFI_BLACK,      EFI_BACKGROUND_CYAN)
#define ATTR_HINT     EFI_TEXT_ATTR (EFI_LIGHTGRAY,  EFI_BACKGROUND_BLUE)
#define ATTR_STATUS   EFI_TEXT_ATTR (EFI_YELLOW,     EFI_BACKGROUND_BLUE)

typedef struct {
  CONST CHAR16  *Title;      /* 主标题 */
  CONST CHAR16  *Sub;        /* 副标题 (efi / builtin / action) */
} MENU_ENTRY;

STATIC CONST MENU_ENTRY  mEntries[MENU_ENTRY_MAX] = {
  { L"EFI loader (WinPE)",    L"efi"     },
  { L"UEFI Shell",            L"builtin" },
  { L"Boot Android",          L"action"  },
  { L"Boot Recovery (TWRP)",  L"action"  },
  { L"Restart",               L"action"  },
  { L"Power off",             L"action"  },
};

STATIC UINTN  mCols = 80;
STATIC UINTN  mRows = 25;

/* ------------------------------------------------------------- 绘制辅助 */

STATIC
VOID
PutPad (
  IN UINTN  Width
  )
{
  UINTN  I;

  for (I = 0; I < Width; I++) {
    Print (L" ");
  }
}

/**
  居中打印一行文本 (不换行)。
**/
STATIC
VOID
PrintCentered (
  IN CONST CHAR16  *String
  )
{
  UINTN  Len;
  UINTN  Pad;

  Len = StrLen (String);
  Pad = (mCols > Len) ? ((mCols - Len) / 2) : 0;

  gST->ConOut->SetCursorPosition (gST->ConOut, 0, gST->ConOut->Mode->CursorRow);
  PutPad (Pad);
  Print (String);
  PutPad (mCols - Pad - Len);
}

/**
  画一个菜单项块 (2 行): 标题行 + 副标题行, Sel 时整块高亮。
**/
STATIC
VOID
DrawEntry (
  IN UINTN   Row,
  IN UINTN   Index,
  IN BOOLEAN Selected
  )
{
  CONST MENU_ENTRY  *Entry;
  CHAR16            NumStr[4];
  UINTN             Attr;

  Entry = &mEntries[Index];
  Attr  = Selected ? ATTR_SEL : ATTR_NORMAL;

  gST->ConOut->SetAttribute (gST->ConOut, Attr);

  UnicodeSPrint (NumStr, sizeof (NumStr), L"[%d]", Index + 1);

  /* 行 1:  [n] Title */
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, Row);
  PutPad (3);
  Print (L"%s  %s", NumStr, Entry->Title);
  PutPad ((mCols > 5 + StrLen (NumStr) + StrLen (Entry->Title))
          ? mCols - 5 - StrLen (NumStr) - StrLen (Entry->Title) : 1);

  /* 行 2:      Sub */
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, Row + 1);
  PutPad (7);
  Print (L"%s", Entry->Sub);
  PutPad ((mCols > 7 + StrLen (Entry->Sub))
          ? mCols - 7 - StrLen (Entry->Sub) : 1);

  gST->ConOut->SetAttribute (gST->ConOut, ATTR_NORMAL);
}

/**
  重绘整个菜单。
**/
STATIC
VOID
DrawMenu (
  IN UINTN         Selected,
  IN CONST CHAR16  *StatusLine
  )
{
  UINTN  Row;
  UINTN  I;

  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  gST->ConOut->SetAttribute (gST->ConOut, ATTR_NORMAL);
  gST->ConOut->ClearScreen (gST->ConOut);

  /* 标题 */
  gST->ConOut->SetAttribute (gST->ConOut, ATTR_TITLE);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 1);
  PrintCentered (MENU_TITLE_STRING);

  gST->ConOut->SetAttribute (gST->ConOut, ATTR_SUB);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 3);
  PrintCentered (MENU_VERSION_STRING);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 4);
  PrintCentered (MENU_CONFIG_STRING);

  /* 菜单项块 */
  Row = 7;
  for (I = 0; I < MENU_ENTRY_MAX; I++) {
    DrawEntry (Row, I, I == Selected);
    Row += 3;
  }

  /* 底部提示 */
  gST->ConOut->SetAttribute (gST->ConOut, ATTR_TITLE);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, mRows - 6);
  PrintCentered (L"Select an entry");

  gST->ConOut->SetAttribute (gST->ConOut, ATTR_HINT);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, mRows - 4);
  PrintCentered (L"Up/Down: select        Enter: boot");
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, mRows - 3);
  PrintCentered (L"1-4: boot entry        Esc: stop timer");

  /* 状态行 (倒计时 / 执行结果) */
  gST->ConOut->SetAttribute (gST->ConOut, ATTR_STATUS);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, mRows - 1);
  PrintCentered ((StatusLine != NULL) ? StatusLine : L"");
}

/**
  读一个按键 (阻塞, 带超时秒数; 返回 0 表示超时无键)。
  TimeoutSeconds < 0 表示无限等待。
**/
STATIC
BOOLEAN
ReadKey (
  IN  INTN           TimeoutSeconds,
  OUT EFI_INPUT_KEY  *Key
  )
{
  EFI_STATUS    Status;
  EFI_EVENT     TimerEvent;
  EFI_EVENT     WaitList[2];
  UINTN         Index;

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  WaitList[0] = gST->ConIn->WaitForKey;
  WaitList[1] = TimerEvent;

  if (TimeoutSeconds >= 0) {
    gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)TimeoutSeconds * 10000000ULL);
    Status = gBS->WaitForEvent (2, WaitList, &Index);
  } else {
    Status = gBS->WaitForEvent (1, WaitList, &Index);
  }

  gBS->CloseEvent (TimerEvent);

  if (EFI_ERROR (Status) || (Index != 0)) {
    return FALSE;
  }

  Status = gST->ConIn->ReadKeyStroke (gST->ConIn, Key);
  return !EFI_ERROR (Status);
}

/* ------------------------------------------------------------- 启动动作 */

/**
  从任意 SimpleFileSystem 卷里找 bootmgfw.efi 并启动 (WinPE)。
**/
STATIC
EFI_STATUS
BootWinPe (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                 Status;
  UINTN                      HandleCount;
  UINTN                      Index;
  EFI_HANDLE                 *Handles;
  EFI_HANDLE                 Loaded;
  EFI_DEVICE_PATH_PROTOCOL   *FilePath;
  STATIC CONST CHAR16        *mCandidates[] = {
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
    L"\\EFI\\BOOT\\BOOTAA64.EFI",
    L"\\efi\\boot\\bootaa64.efi",
    NULL
  };
  UINTN                      C;
  UINTN                      ExitDataSize;
  CHAR16                     *ExitData;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status) || (HandleCount == 0)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    for (C = 0; mCandidates[C] != NULL; C++) {
      FilePath = FileDevicePath (Handles[Index], (CHAR16 *)mCandidates[C]);
      if (FilePath == NULL) {
        continue;
      }

      Loaded     = NULL;
      ExitData   = NULL;
      ExitDataSize = 0;

      Status = gBS->LoadImage (TRUE, ImageHandle, FilePath, NULL, 0, &Loaded);
      if (EFI_ERROR (Status)) {
        continue;
      }

      DrawMenu (MENU_ENTRY_WINPE, L"Booting EFI loader...");
      Status = gBS->StartImage (Loaded, &ExitDataSize, &ExitData);

      /* 返回 = 启动失败, 换下一个候选 */
    }
  }

  FreePool (Handles);
  return EFI_NOT_FOUND;
}

/**
  启动内嵌 FV 的 UEFI Shell。
**/
STATIC
EFI_STATUS
BootShell (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;
  VOID        *ShellImage;
  UINTN       ShellImageSize;
  EFI_HANDLE  Loaded;
  UINTN       ExitDataSize;
  CHAR16      *ExitData;

  ShellImage     = NULL;
  ShellImageSize = 0;

  Status = GetSectionFromFv (&gT158ShellFileGuid, EFI_SECTION_PE32, 0, &ShellImage, &ShellImageSize);
  if (EFI_ERROR (Status)) {
    Status = GetSectionFromFv (&gT158ShellFileGuid, EFI_SECTION_TE, 0, &ShellImage, &ShellImageSize);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Loaded = NULL;
  Status = gBS->LoadImage (FALSE, ImageHandle, NULL, ShellImage, ShellImageSize, &Loaded);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ExitData     = NULL;
  ExitDataSize = 0;
  return gBS->StartImage (Loaded, &ExitDataSize, &ExitData);
}

/**
  执行菜单项, 返回后回菜单。返回 TRUE 表示需要退出 (理论上不会)。
**/
STATIC
VOID
ExecuteEntry (
  IN EFI_HANDLE  ImageHandle,
  IN UINTN       Index,
  OUT BOOLEAN    *RebootMenu
  )
{
  EFI_STATUS    Status;
  CONST CHAR16  *Msg;

  Msg = NULL;

  switch (Index) {
    case MENU_ENTRY_WINPE:
      Status = BootWinPe (ImageHandle);
      Msg    = L"EFI loader not found (storage driver not ready)";
      break;

    case MENU_ENTRY_SHELL:
      DrawMenu (Index, L"Starting UEFI Shell...");
      Status = BootShell (ImageHandle);
      Msg    = L"Shell exited";
      break;

    case MENU_ENTRY_ANDROID:
      /* UEFI 直接链式启动安卓 kernel (UEFI 本身就是引导)。候选顺序:
       *   1. espboot (FAT32): \kernel + \ramdisk + \dtb 文件引导 —— 完全自主,
       *      换 kernel 只需电脑拷文件, 无需重新打包固件
       *   2. payload 内嵌 (boot_a): 自包含部署 (mkpayload -k)
       *   3. boot_b / 4. boot_a: boot.img 分区候选 (旧部署/UEFI 临时运行)
       * 每个候选都校验 ARMd Image, 不匹配自动跳到下一个。成功则不返回。 */
      DrawMenu (Index, L"Booting Android...");
      Status = T158LinuxBootFromEsp (L"espboot");
      if (EFI_ERROR (Status)) {
        Status = T158LinuxBootFromPayload (L"boot_a");
      }
      if (EFI_ERROR (Status)) {
        Status = T158LinuxBootFromPartition (L"boot_b");
      }
      if (EFI_ERROR (Status)) {
        Status = T158LinuxBootFromPartition (L"boot_a");
      }
      Msg    = L"Boot Android failed (UFS / boot.img not ready)";
      break;

    case MENU_ENTRY_RECOVERY:
      /* 同上, 但读 recovery 分区 (TWRP 的 recovery.img, 自带 ramdisk)。 */
      DrawMenu (Index, L"Booting TWRP (recovery)...");
      Status = T158LinuxBootFromPartition (L"recovery");
      Msg    = L"Boot TWRP failed (UFS / recovery.img not ready)";
      break;

    case MENU_ENTRY_RESTART:
      gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
      Msg = L"Restart failed";
      break;

    case MENU_ENTRY_POWEROFF:
      gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
      Msg = L"Power off failed";
      break;

    default:
      Msg = L"Invalid entry";
      break;
  }

  if (Msg != NULL) {
    DrawMenu (Index, Msg);
    gBS->Stall (1500000);          /* 停 1.5 秒让人看到提示 */
  }

  *RebootMenu = TRUE;
}

/* ------------------------------------------------------------- 主循环 */

EFI_STATUS
EFIAPI
T158BootMenuEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS     Status;
  EFI_INPUT_KEY  Key;
  UINTN          Selected;
  BOOLEAN        Quit;
  INTN           Timer;
  CONST CHAR16   *StatusLine;
  CHAR16         TimerBuf[64];

  Status = gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &mCols, &mRows);
  if (EFI_ERROR (Status)) {
    mCols = 80;
    mRows = 25;
  }

  Selected = MENU_ENTRY_WINPE;
  Quit     = FALSE;

  while (!Quit) {
    BOOLEAN  Confirmed;
    BOOLEAN  TimerOn;
    BOOLEAN  Got;

    Confirmed = FALSE;
    TimerOn   = TRUE;
    Timer     = MENU_TIMER_SECONDS;
    StatusLine = L"";

    while (!Confirmed) {
      if (TimerOn) {
        if (Timer > 0) {
          UnicodeSPrint (TimerBuf, sizeof (TimerBuf), L"The default entry will be booted in %d s", Timer);
          StatusLine = TimerBuf;
        } else {
          StatusLine = L"Booting default entry...";
        }
      } else {
        StatusLine = L"";
      }

      DrawMenu (Selected, StatusLine);

      Got = FALSE;
      if (TimerOn) {
        Got = ReadKey (1, &Key);
        if (!Got) {
          Timer--;
          if (Timer < 0) {
            Confirmed = TRUE;        /* 倒计时结束, 启动默认项 */
          }
        }
      } else {
        Got = ReadKey (-1, &Key);
      }

      if (Got) {
        if (Key.ScanCode == SCAN_UP) {
          Selected = (Selected + MENU_ENTRY_MAX - 1) % MENU_ENTRY_MAX;
          TimerOn   = FALSE;
        } else if (Key.ScanCode == SCAN_DOWN) {
          Selected = (Selected + 1) % MENU_ENTRY_MAX;
          TimerOn   = FALSE;
        } else if (Key.ScanCode == SCAN_ESC) {
          TimerOn = FALSE;
        } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
          Confirmed = TRUE;
        } else if ((Key.UnicodeChar >= L'1') && (Key.UnicodeChar <= (L'0' + MENU_ENTRY_MAX))) {
          Selected = (UINTN)(Key.UnicodeChar - L'1');
          Confirmed = TRUE;
        }
      }
    }

    ExecuteEntry (ImageHandle, Selected, &Quit);
  }

  return EFI_SUCCESS;
}
