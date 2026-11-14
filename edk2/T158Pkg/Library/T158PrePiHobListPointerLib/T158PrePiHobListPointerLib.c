/** @file
 *  HOB 链表指针保存在 .data 段中的全局变量 (SEC/PEI 无全局数据库, 只能这样传递)。
 **/

// 注意: 不能只 include <Base.h> —— 它只提供 RETURN_STATUS / PHYSICAL_ADDRESS 等基础类型,
// 而本文件用到 EFI_STATUS / EFI_GUID / EFI_PHYSICAL_ADDRESS, 这些定义在 UefiBaseType.h,
// 由 <PiPei.h> 引入 (PiHob.h 里的结构也依赖它们)。
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Pi/PiHob.h>

VOID  *gT158HobList = NULL;

VOID *
EFIAPI
PrePiGetHobList (
  VOID
  )
{
  return gT158HobList;
}

EFI_STATUS
EFIAPI
PrePiSetHobList (
  IN VOID  *HobList
  )
{
  gT158HobList = HobList;
  return EFI_SUCCESS;
}

/* 兼容另一套命名 */
VOID *
EFIAPI
GetHobList (
  VOID
  )
{
  return PrePiGetHobList ();
}

EFI_STATUS
EFIAPI
SetHobList (
  IN VOID  *HobList
  )
{
  return PrePiSetHobList (HobList);
}
