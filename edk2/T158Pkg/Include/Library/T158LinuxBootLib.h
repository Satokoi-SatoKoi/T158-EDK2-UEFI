/** @file
 *  T158LinuxBootLib —— UEFI 直接链式启动 Android/Linux kernel
 *
 *  流程:
 *    1. 按分区名找 GPT 分区 (boot_b / recovery / ...) 的 DiskIo
 *    2. 解析 Android boot.img (v3/v4, magic "ANDROID!"), 取 kernel + ramdisk
 *       - T158 的 kernel 是未压缩 arm64 Image (MZ + "ARMd", 可任意放置)
 *    3. 拼接完整 initrd: boot ramdisk + init_boot_x ramdisk + vendor_boot_x vendor_ramdisk
 *    4. 用 U-Boot 传下来的 DTB (经 GUID HOB), fdt_open_into 后往 /chosen 写
 *       linux,initrd-start / linux,initrd-end
 *    5. GetMemoryMap -> ExitBootServices -> 跳 kernel (x0=DTB, x1-x3=0)
 *
 *  两种 kernel 来源:
 *    A. T158LinuxBootFromPartition   —— 标准路径: kernel 在 boot.img 的 kernel 段
 *       (UEFI 临时运行时 boot_a 是原厂安卓; 或安卓 boot.img 备份在 boot_b;
 *        recovery 分区的 TWRP 同理)
 *    B. T158LinuxBootFromPayload     —— 自包含路径: UEFI payload 常驻 boot_a,
 *       打包时把原厂安卓 kernel 塞进了 payload 的 ramdisk 段 (mkpayload.py -k),
 *       UEFI 从自己所在分区的 ramdisk 段读出安卓 kernel。完全不依赖 boot_b。
 *
 *  加载布局 (2MB 对齐, 都在 DRAM 低段, 与 UEFI FD @0xA9000000 不冲突):
 *    0x80200000        kernel Image (46MB 级)
 *    kernel_end 对齐 2MB -> initrd (boot+init_boot+vendor_boot 拼接, ~90MB 级)
 *    U-Boot DTB        原位 (SEC 已保留 1MB NVS)
 *
 *  成功时本库不返回; 失败返回 EFI_STATUS, 调用方回菜单 (可尝试下一候选)。
 **/

#ifndef T158_LINUX_BOOT_LIB_H_
#define T158_LINUX_BOOT_LIB_H_

#include <Uefi.h>

/**
  标准路径: 从指定 GPT 分区读 boot.img (v3/v4), 解析 kernel 并链式启动。
  成功则不返回。

  @param[in] BootPartitionName  boot 分区名, 如 L"boot_b" (安卓) 或 L"recovery" (TWRP)

  @retval EFI_NOT_FOUND         分区不存在 / 无有效 boot.img / kernel 段不是 ARMd
  @retval EFI_UNSUPPORTED       header_version 不是 v3/v4 (其它错误同理返回)
**/
EFI_STATUS
EFIAPI
T158LinuxBootFromPartition (
  IN CONST CHAR16  *BootPartitionName
  );

/**
  自包含路径: UEFI payload 常驻 SelfPartition (boot_a) 时, 安卓 kernel 打包在
  payload 的 ramdisk 段 (mkpayload.py bootimg -k <原厂boot.img>)。本函数读该分区
  的 boot.img header, 取 ramdisk 段作为安卓 kernel 并链式启动。成功则不返回。

  @param[in] SelfPartitionName  UEFI payload 所在分区名, 通常 L"boot_a"

  @retval EFI_NOT_FOUND         分区不存在 / ramdisk 段为空 / 不是 ARMd
                                (调用方应回退到 T158LinuxBootFromPartition)
**/
EFI_STATUS
EFIAPI
T158LinuxBootFromPayload (
  IN CONST CHAR16  *SelfPartitionName
  );

/**
  ESP 路径: 从 FAT32 分区 (如 espboot) 读文件引导。需要 DSC 里启用
  FatPkg/EnhancedFatDxe (FAT32 -> SimpleFileSystem)。

  分区根目录期望的文件:
    \kernel   arm64 未压缩 Image (MZ + "ARMd"), 必须
    \ramdisk  initrd (任意 cpio/拼接形态), 必须
    \dtb      设备树, 可选 (缺省用 U-Boot 传下来的 DTB)

  本路径不读 init_boot_x / vendor_boot_x —— ramdisk 由用户在文件里自行组合。
  成功则不返回。

  @param[in] EspPartitionName   FAT32 分区名, 如 L"espboot"

  @retval EFI_NOT_FOUND         分区不存在 / 缺 \kernel 或 \ramdisk / 不是 ARMd
  @retval EFI_UNSUPPORTED       无 FAT 文件系统 (Fat 驱动未启用)
**/
EFI_STATUS
EFIAPI
T158LinuxBootFromEsp (
  IN CONST CHAR16  *EspPartitionName
  );

#endif /* T158_LINUX_BOOT_LIB_H_ */
