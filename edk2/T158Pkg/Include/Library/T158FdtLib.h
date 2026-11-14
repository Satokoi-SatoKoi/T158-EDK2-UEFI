/** @file
 *  T158FdtLib —— 最小 device tree 只读解析
 **/

#ifndef T158_FDT_LIB_H_
#define T158_FDT_LIB_H_

#define T158_FDT_MAX_SIZE  0x00200000    /* 2MB, 足以覆盖本平台 DTB (实测 0x25C7C) */

typedef struct {
  CONST UINT8  *Base;
  UINT32       TotalSize;
  UINT32       OffStruct;
  UINT32       OffStrings;
} T158_FDT_CTX;

/**
  校验并打开位于 Address 的 device tree。
**/
BOOLEAN
EFIAPI
T158FdtOpen (
  IN  UINT64         Address,
  OUT T158_FDT_CTX  *Ctx
  );

/**
  读取节点属性。
**/
CONST UINT8 *
EFIAPI
T158FdtGetProp (
  IN CONST T158_FDT_CTX  *Ctx,
  IN CONST CHAR8         *NodePath,
  IN CONST CHAR8         *PropName,
  OUT UINT32             *Length
  );

/**
  经 ranges 把子节点地址翻译为 CPU 物理地址。
**/
EFI_STATUS
EFIAPI
T158FdtTranslate (
  IN CONST T158_FDT_CTX  *Ctx,
  IN CONST CHAR8         *NodePath,
  IN UINT64              ChildAddress,
  OUT UINT64             *Physical
  );

/**
  取 DRAM 基址/长度 (取容量最大的 memory 节点)。
**/
EFI_STATUS
EFIAPI
T158FdtGetMemory (
  IN CONST T158_FDT_CTX  *Ctx,
  OUT UINT64             *Base,
  OUT UINT64             *Size
  );

/**
  取控制台串口物理基址与波特率。
**/
EFI_STATUS
EFIAPI
T158FdtGetConsoleUart (
  IN CONST T158_FDT_CTX  *Ctx,
  OUT UINT64             *UartBase,
  OUT UINT32             *BaudRate
  );

#endif
