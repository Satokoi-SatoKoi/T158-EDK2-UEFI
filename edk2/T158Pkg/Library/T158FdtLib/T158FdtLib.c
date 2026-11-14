/** @file
 *  极简 FDT (libfdt 子集) 实现, 只做固件启动需要的三件事:
 *    1) 校验 x0 传进来的是不是 device tree
 *    2) 从 /memory@xxx 的 reg 拿到 DRAM 基址与长度
 *    3) 从 /chosen{stdout-path} + /aliases{serialN} 解析到串口节点, 并经 ranges 翻译成物理地址
 *
 *  之所以自己实现: SEC 阶段没有 libc/动态内存, 且只需要只读访问,
 *  用不到完整 libfdt。所有解析都在调用者的缓冲区上原地完成。
 **/

#include <Base.h>
#include <Uefi/UefiBaseType.h>
#include <Library/BaseLib.h>
#include <T158Platform.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/T158FdtLib.h>

#define FDT_MAGIC        0xD00DFEED
#define FDT_BEGIN_NODE   1
#define FDT_END_NODE     2
#define FDT_PROP         3
#define FDT_NOP          4
#define FDT_END          9

STATIC
UINT32
Fdt32 (
  IN CONST UINT8  *Buf,
  IN UINT32       Off
  )
{
  return ((UINT32)Buf[Off] << 24) | ((UINT32)Buf[Off + 1] << 16) |
         ((UINT32)Buf[Off + 2] << 8) | (UINT32)Buf[Off + 3];
}

STATIC
UINT64
Fdt64 (
  IN CONST UINT8  *Buf,
  IN UINT32       Off
  )
{
  return ((UINT64)Fdt32 (Buf, Off) << 32) | Fdt32 (Buf, Off + 4);
}

/**
  校验并填充上下文。
**/
BOOLEAN
EFIAPI
T158FdtOpen (
  IN  UINT64      Address,
  OUT T158_FDT_CTX  *Ctx
  )
{
  CONST UINT8  *Buf;
  UINT32       Total;

  if ((Address == 0) || (Ctx == NULL)) {
    return FALSE;
  }

  Buf = (CONST UINT8 *)(UINTN)Address;
  if (Fdt32 (Buf, 0) != FDT_MAGIC) {
    return FALSE;
  }

  Total = Fdt32 (Buf, 4);
  if ((Total < 64) || (Total > T158_FDT_MAX_SIZE)) {
    return FALSE;
  }

  Ctx->Base       = Buf;
  Ctx->TotalSize  = Total;
  Ctx->OffStruct  = Fdt32 (Buf, 8);
  Ctx->OffStrings = Fdt32 (Buf, 12);
  if ((Ctx->OffStruct >= Total) || (Ctx->OffStrings >= Total)) {
    return FALSE;
  }

  return TRUE;
}

STATIC
CONST CHAR8 *
FdtString (
  IN CONST T158_FDT_CTX  *Ctx,
  IN UINT32              Off
  )
{
  return (CONST CHAR8 *)(Ctx->Base + Ctx->OffStrings + Off);
}

STATIC
INTN
StrCmp8 (
  IN CONST CHAR8  *A,
  IN CONST CHAR8  *B
  )
{
  while (*A && (*A == *B)) {
    A++;
    B++;
  }

  return *A - *B;
}

STATIC
UINTN
StrLen8 (
  IN CONST CHAR8  *A
  )
{
  UINTN  N;

  N = 0;
  while (A[N] != '\0') {
    N++;
  }

  return N;
}

/**
  取得某节点的属性; 节点路径形如 "/soc/ap-apb/serial@10000" 或 "/" 。
  返回属性数据指针, 长度写入 *Length; 失败返回 NULL。
**/
CONST UINT8 *
EFIAPI
T158FdtGetProp (
  IN CONST T158_FDT_CTX  *Ctx,
  IN CONST CHAR8         *NodePath,
  IN CONST CHAR8         *PropName,
  OUT UINT32             *Length
  )
{
  UINT32       Pos;
  UINT32       Depth;
  CHAR8        Path[256];
  UINTN        PathLen;
  CONST UINT8  *Result;

  if ((Ctx == NULL) || (NodePath == NULL) || (PropName == NULL)) {
    return NULL;
  }

  Result  = NULL;
  if (Length != NULL) {
    *Length = 0;
  }

  Pos   = Ctx->OffStruct;
  Depth = 0;
  Path[0] = '\0';

  while ((Pos + 4) <= Ctx->TotalSize) {
    UINT32  Tag;

    Tag = Fdt32 (Ctx->Base, Pos);
    if (Tag == FDT_BEGIN_NODE) {
      CONST CHAR8  *Name;
      UINTN        NameLen;
      UINT32       Start;

      Name    = (CONST CHAR8 *)(Ctx->Base + Pos + 4);
      Start   = Pos + 4;
      while ((Start < Ctx->TotalSize) && (Ctx->Base[Start] != '\0')) {
        Start++;
      }

      NameLen = Start - (Pos + 4);
      if (Depth == 0) {
        Path[0] = '\0';
      }

      /* 追加 "/" + name */
      PathLen = StrLen8 (Path);
      if (PathLen + NameLen + 2 >= sizeof (Path)) {
        return NULL;
      }

      Path[PathLen] = '/';
      CopyMem (Path + PathLen + 1, Name, NameLen + 1);

      Depth++;
      Pos = (Start + 4) & ~3;
    } else if (Tag == FDT_END_NODE) {
      /* 弹回父路径 */
      PathLen = StrLen8 (Path);
      while ((PathLen > 0) && (Path[PathLen - 1] != '/')) {
        PathLen--;
      }

      if (PathLen > 0) {
        Path[PathLen - 1] = '\0';
      }

      if (Path[0] == '\0') {
        Path[0] = '\0';
      }

      Depth--;
      Pos += 4;
    } else if (Tag == FDT_PROP) {
      UINT32       PropLen;
      UINT32       NameOff;
      CONST CHAR8  *PropNameStr;

      PropLen     = Fdt32 (Ctx->Base, Pos + 4);
      NameOff     = Fdt32 (Ctx->Base, Pos + 8);
      PropNameStr = FdtString (Ctx, NameOff);

      if ((StrCmp8 (Path, NodePath) == 0) && (StrCmp8 (PropNameStr, PropName) == 0)) {
        Result = Ctx->Base + Pos + 12;
        if (Length != NULL) {
          *Length = PropLen;
        }

        return Result;
      }

      Pos = (Pos + 12 + PropLen + 3) & ~3;
    } else if (Tag == FDT_NOP) {
      Pos += 4;
    } else {
      break;
    }
  }

  return Result;
}

STATIC
UINT32
GetCells (
  IN CONST T158_FDT_CTX  *Ctx,
  IN CONST CHAR8         *NodePath,
  IN CONST CHAR8         *CellsName,
  IN UINT32              Default
  )
{
  CONST UINT8  *Data;
  UINT32       Len;

  Data = T158FdtGetProp (Ctx, NodePath, CellsName, &Len);
  if ((Data == NULL) || (Len < 4)) {
    return Default;
  }

  return Fdt32 (Data, 0);
}

/**
  把节点内的地址经父级 ranges 逐层翻译为 CPU 物理地址。
**/
EFI_STATUS
EFIAPI
T158FdtTranslate (
  IN CONST T158_FDT_CTX  *Ctx,
  IN CONST CHAR8         *NodePath,
  IN UINT64              ChildAddress,
  OUT UINT64             *Physical
  )
{
  CHAR8   Path[256];
  CHAR8   Parent[256];
  UINTN   Len;
  UINT64  Addr;

  if ((Ctx == NULL) || (NodePath == NULL) || (Physical == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Addr = ChildAddress;
  CopyMem (Path, NodePath, StrLen8 (NodePath) + 1);

  while (TRUE) {
    CONST UINT8  *Ranges;
    UINT32       RangesLen;
    UINT32       Ac, Sc, AcP;
    UINT32       Pos;

    /* 找到 Path 的父路径 */
    Len = StrLen8 (Path);
    while ((Len > 0) && (Path[Len - 1] != '/')) {
      Len--;
    }

    if (Len == 0) {
      break;                       /* 已到根 */
    }

    if (Len == 1) {
      CopyMem (Parent, "/", 2);
    } else {
      CopyMem (Parent, Path, Len - 1);
      Parent[Len - 1] = '\0';
    }

    Ranges = T158FdtGetProp (Ctx, Parent, "ranges", &RangesLen);
    if ((Ranges == NULL) || (RangesLen == 0)) {
      CopyMem (Path, Parent, StrLen8 (Parent) + 1);   /* 无 ranges: 恒等映射, 继续向上 */
      continue;
    }

    Ac   = GetCells (Ctx, Path, "#address-cells", 1);
    Sc   = GetCells (Ctx, Path, "#size-cells", 1);
    AcP  = GetCells (Ctx, Parent, "#address-cells", 2);

    for (Pos = 0; (Pos + ((Ac + AcP + Sc) * 4)) <= RangesLen; Pos += (Ac + AcP + Sc) * 4) {
      UINT64  Child, ParentAddr, Size;

      Child      = (Ac == 2) ? Fdt64 (Ranges, Pos) : Fdt32 (Ranges, Pos);
      ParentAddr = (AcP == 2) ? Fdt64 (Ranges, Pos + Ac * 4) : Fdt32 (Ranges, Pos + Ac * 4);
      Size       = (Sc == 2) ? Fdt64 (Ranges, Pos + (Ac + AcP) * 4) : Fdt32 (Ranges, Pos + (Ac + AcP) * 4);

      if ((Addr >= Child) && (Addr < (Child + Size))) {
        Addr = Addr - Child + ParentAddr;
        break;
      }
    }

    CopyMem (Path, Parent, StrLen8 (Parent) + 1);
  }

  *Physical = Addr;
  return EFI_SUCCESS;
}

/**
  读取 DRAM: 扫描根节点下所有 device_type = "memory" 的节点, 取容量最大的一个。
**/
EFI_STATUS
EFIAPI
T158FdtGetMemory (
  IN CONST T158_FDT_CTX  *Ctx,
  OUT UINT64             *Base,
  OUT UINT64             *Size
  )
{
  UINT32       Pos;
  CHAR8        Path[256];
  UINT32       Depth;
  UINT64       BestBase;
  UINT64       BestSize;

  if ((Ctx == NULL) || (Base == NULL) || (Size == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BestBase = 0;
  BestSize = 0;
  Pos      = Ctx->OffStruct;
  Depth    = 0;
  Path[0]  = '\0';

  while ((Pos + 4) <= Ctx->TotalSize) {
    UINT32  Tag;

    Tag = Fdt32 (Ctx->Base, Pos);
    if (Tag == FDT_BEGIN_NODE) {
      CONST CHAR8  *Name;
      UINT32       Start;
      UINTN        PathLen;

      Name  = (CONST CHAR8 *)(Ctx->Base + Pos + 4);
      Start = Pos + 4;
      while ((Start < Ctx->TotalSize) && (Ctx->Base[Start] != '\0')) {
        Start++;
      }

      PathLen = StrLen8 (Path);
      if (PathLen + (Start - (Pos + 4)) + 2 < sizeof (Path)) {
        Path[PathLen] = '/';
        CopyMem (Path + PathLen + 1, Name, Start - (Pos + 4) + 1);
      }

      Depth++;
      if (Depth == 1) {
        CONST UINT8  *Dt;
        UINT32       DtLen;

        Dt = T158FdtGetProp (Ctx, Path, "device_type", &DtLen);
        if ((Dt != NULL) && (DtLen >= 6) && (CompareMem (Dt, "memory", 6) == 0)) {
          CONST UINT8  *Reg;
          UINT32       RegLen;
          UINT32       Ac, Sc;

          Reg = T158FdtGetProp (Ctx, Path, "reg", &RegLen);
          Ac  = GetCells (Ctx, "/", "#address-cells", 2);
          Sc  = GetCells (Ctx, "/", "#size-cells", 2);
          if ((Reg != NULL) && (RegLen >= ((Ac + Sc) * 4))) {
            UINT64  B, S;

            B = (Ac == 2) ? Fdt64 (Reg, 0) : Fdt32 (Reg, 0);
            S = (Sc == 2) ? Fdt64 (Reg, Ac * 4) : Fdt32 (Reg, Ac * 4);
            if (S > BestSize) {
              BestSize = S;
              BestBase = B;
            }
          }
        }
      }

      Pos = (Start + 4) & ~3;
    } else if (Tag == FDT_END_NODE) {
      UINTN  PathLen;

      PathLen = StrLen8 (Path);
      while ((PathLen > 0) && (Path[PathLen - 1] != '/')) {
        PathLen--;
      }

      if (PathLen > 0) {
        Path[PathLen - 1] = '\0';
      } else {
        Path[0] = '\0';
      }

      Depth--;
      Pos += 4;
    } else if (Tag == FDT_PROP) {
      Pos = (Pos + 12 + Fdt32 (Ctx->Base, Pos + 4) + 3) & ~3;
    } else if (Tag == FDT_NOP) {
      Pos += 4;
    } else {
      break;
    }
  }

  if (BestSize == 0) {
    return EFI_NOT_FOUND;
  }

  *Base = BestBase;
  *Size = BestSize;
  return EFI_SUCCESS;
}

/**
  解析控制台串口: /chosen{stdout-path} -> "serial1:..." -> /aliases{serial1} -> 节点 -> reg -> 翻译
  解析失败时返回 EFI_NOT_FOUND, 调用者应回退到静态 PCD。
**/
EFI_STATUS
EFIAPI
T158FdtGetConsoleUart (
  IN CONST T158_FDT_CTX  *Ctx,
  OUT UINT64             *UartBase,
  OUT UINT32             *BaudRate
  )
{
  CONST UINT8  *Stdout;
  UINT32       StdoutLen;
  CHAR8        Alias[32];
  CONST UINT8  *Target;
  UINT32       TargetLen;
  CONST UINT8  *Reg;
  UINT32       RegLen;
  UINT64       Child;
  UINT32       Ac;
  UINTN        I;
  UINT64       Rate;

  if ((Ctx == NULL) || (UartBase == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Stdout = T158FdtGetProp (Ctx, "/chosen", "stdout-path", &StdoutLen);
  if ((Stdout == NULL) || (StdoutLen == 0)) {
    return EFI_NOT_FOUND;
  }

  /* stdout-path = "serial1:115200n8" */
  I = 0;
  while ((I < StdoutLen) && (Stdout[I] != '\0') && (Stdout[I] != ':') && (I < sizeof (Alias) - 1)) {
    Alias[I] = (CHAR8)Stdout[I];
    I++;
  }

  Alias[I] = '\0';

  Rate = 0;
  if ((I < StdoutLen) && (Stdout[I] == ':')) {
    UINTN  J;

    J = I + 1;
    while ((J < StdoutLen) && (Stdout[J] >= '0') && (Stdout[J] <= '9')) {
      Rate = Rate * 10 + (Stdout[J] - '0');
      J++;
    }
  }

  if (BaudRate != NULL) {
    *BaudRate = (Rate == 0) ? T158_UART_DEFAULT_BAUD : (UINT32)Rate;
  }

  /* 优先按 alias 查; alias 缺失时把 stdout-path 本身当节点路径 */
  Target = T158FdtGetProp (Ctx, "/aliases", Alias, &TargetLen);
  if ((Target == NULL) || (TargetLen == 0)) {
    Target     = Stdout;
    TargetLen  = (UINT32)I;
  }

  {
    CHAR8  NodePath[256];
    UINTN  TL;

    TL = 0;
    while ((TL < TargetLen) && (Target[TL] != '\0') && (TL < sizeof (NodePath) - 1)) {
      NodePath[TL] = (CHAR8)Target[TL];
      TL++;
    }

    NodePath[TL] = '\0';

    Reg = T158FdtGetProp (Ctx, NodePath, "reg", &RegLen);
    if ((Reg == NULL) || (RegLen < 4)) {
      return EFI_NOT_FOUND;
    }

    Ac    = GetCells (Ctx, NodePath, "#address-cells", 1);
    Child = (RegLen >= 8) ? Fdt64 (Reg, 0) : Fdt32 (Reg, 0);
    if (Ac == 1) {
      Child = Fdt32 (Reg, 0);
    }

    return T158FdtTranslate (Ctx, NodePath, Child, UartBase);
  }
}
