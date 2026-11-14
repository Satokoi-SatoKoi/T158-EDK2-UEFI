/** @file
 *  T158BootControlLib —— 通过改写 misc 分区控制原厂 U-Boot 的启动目标
 *
 *  取证依据 (对备份镜像 misc / uboot_a 的实测, 见 tools/probe_misc.py):
 *
 *  1) U-Boot 的启动目标来自 misc 分区, 不是寄存器:
 *       "%s:Message in misc indicate the RECOVERY MODE"
 *       "Detect the recovery message in the misc partition"
 *       "ANDROID: Attempting slot %c, tries_remaining %d"
 *
 *  2) misc 偏移 0    : 标准 Android BCB (struct bootloader_message)
 *                      command[32] = "boot-recovery" 时 U-Boot 加载 recovery 分区
 *                      (recovery 分区不带 _a/_b 后缀, 所以进 TWRP 不需要切槽)
 *
 *  3) misc 偏移 2048 : 展锐自己的 A/B 元数据 (实测 32 字节):
 *
 *        00000800  5F 61 00 00 42 43 41 42 01 02 00 00 FF 00 0F 00
 *        00000810  00 00 00 00 00 00 00 00 00 00 00 00 96 B1 8D 84
 *
 *        +0x00 slot_suffix[4] = "_a\0\0"   <- 改这里即可切槽
 *        +0x04 magic[4]       = "BCAB"
 *        +0x08 version[2]     = 1, 2
 *        +0x0A reserved[18]
 *        +0x1C crc32          标准 CRC32 (IEEE, 与 zlib.crc32 一致, 小端),
 *                             覆盖 +0x00..+0x1B 共 28 字节 (已用 probe_crc.py 验证:
 *                             crc32(d[0x800:0x81C]) = 0x848DB196 == 分区中的值)
 *
 *  前置条件: 需要能访问 misc 分区, 即必须有 UFS 驱动 (BlockIo/DiskIo)。
 *            未就绪时本库所有函数返回 EFI_UNSUPPORTED, 调用方应优雅降级。
 *
 **/

#ifndef T158_BOOT_CONTROL_LIB_H_
#define T158_BOOT_CONTROL_LIB_H_

///
/// 存放 A/B 元数据的 misc 分区偏移
///
#define T158_MISC_BCB_OFFSET  0U      ///< struct bootloader_message
#define T158_MISC_AB_OFFSET   2048U   ///< struct sprd_boot_control

///
/// A/B 槽位
///
typedef enum {
  T158SlotA,
  T158SlotB
} T158_SLOT;

/**
  读取当前活动槽位。

  @param[out] Slot     当前槽位 (可选, 可为 NULL)
  @param[out] Suffix   4 字节槽名, 如 "_a\0\0" (可选, 可为 NULL)

  @retval EFI_SUCCESS          成功
  @retval EFI_UNSUPPORTED      misc 分区不可访问 (UFS 驱动未就绪)
  @retval EFI_NOT_FOUND        没找到 misc 分区
  @retval EFI_CRC_ERROR        元数据 CRC 校验失败
**/
EFI_STATUS
EFIAPI
T158BootCtrlGetSlot (
  OUT T158_SLOT  *Slot     OPTIONAL,
  OUT CHAR8      Suffix[4] OPTIONAL
  );

/**
  切换活动槽位 (相当于 fastboot set_active)。

  调用后需冷重启生效。

  @param[in] Slot      目标槽位

  @retval EFI_SUCCESS      成功
  @retval EFI_UNSUPPORTED  misc 分区不可访问
  @retval EFI_NOT_FOUND    没找到 misc 分区
**/
EFI_STATUS
EFIAPI
T158BootCtrlSetSlot (
  IN T158_SLOT  Slot
  );

/**
  设置 / 清除 "下次启动进 recovery" 标志 (写入 BCB 的 command 字段)。

  recovery 分区没有 _a/_b 后缀, 因此进 TWRP 不需要切槽。

  @param[in] Enable    TRUE = 写 "boot-recovery"; FALSE = 清空

  @retval EFI_SUCCESS      成功
  @retval EFI_UNSUPPORTED  misc 分区不可访问
  @retval EFI_NOT_FOUND    没找到 misc 分区
**/
EFI_STATUS
EFIAPI
T158BootCtrlSetRecoveryBoot (
  IN BOOLEAN  Enable
  );

#endif /* T158_BOOT_CONTROL_LIB_H_ */
