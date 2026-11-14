 T158 UEFI —— 让展锐 T158 (UMS9621) 用上UEFI
感谢@hxzbaka大手子的指导
用ai写的代码,由于中考的原因所以没什么时间(
目前仅实现了uefi,由于设备被收+内存小没能启动WinPE,或许之后会有人做吧(笑)
基于荣悦e5(rongyue_e5)制作
以下内容由ai生成,刷之前记得备份

--------------------------------------
对某 UMS9621 设备（下称 T158）的原厂固件做了静态取证，并在此基础上用 **EDK2**
实现了一个能跑在原厂 U-Boot 之上的 UEFI 固件：固件接管后直接从 UFS 分区解析
Android boot.img、拼装 initrd、填充 DTB 并跳转 
BootROM → SPL → u-boot_a → 原本: 内核
                          ↓ 现在: 加载我们的载荷 (boot.img v4 封装的 EDK2)
                         EDK2 SEC → DXE → BDS → 图形菜单
                                                ├─ Boot Android  → UFS boot_x → kernel + initrd → 跳转
                                                ├─ Boot TWRP     → UFS recovery → 同上
                                                ├─ EFI loader (WinPE) / UEFI Shell
                                                └─ Restart / Power off
```

原理

原厂 u-boot没有 efi_loader，所以不能跑
`BOOTAA64.EFI`；本工程把 EDK2 固件伪装成 arm64 Linux 内核（沿用原厂实测的 `ARMd`
magic 与 boot.img v4 封装），让原厂 u-boot 照常加载它 —— 之后整条启动链由 UEFI 接管，
启动安卓不再经过 U-Boot（不再写 misc / reboot-mode / 切槽）

实测取证要点（详见 docs/01）

| 项 | 值 |
|---|---|
| SoC / 板 | UMS9621（T158），`sprd,ums9621-1h10` |
| DRAM | `0x8000_0000`（DT 报 8GB，默认暴露 2GB） |
| 控制台 UART | serial1 = **0x20210000**，115200-8-N-1（`stdout-path = serial1:115200n8`） |
| 屏幕 | `logobuffer@0xBE000000`（U-Boot 已点亮的显存），UEFI 直接当 GOP 用 |
| UFS | `sprd,ufshc-ums9621` @ **0x22000000**（MMIO，EDK2 只有 PCI 版 HC，自写了 MMIO 薄壳） |
| GICv3 | GICD 0x12000000 / GICR 0x12040000 |
| boot.img | header **v4**，kernel @0x1000 大小 0x2C96A00，magic **`ARMd`**（非标准 ARM64），未压缩 |
| initrd 组成 | `init_boot`（GKI ramdisk 2MB）+ `vendor_boot`（vendor_ramdisk 80MB），均独立分区 |
| 设备树 | 不在 `dtb_a`（全 0），而在 **vendor_boot_a @0x4CF0040**，长 0x25C7C（已含 overlay） |
| misc | BCB @0 + A/B slot metadata @2048（CRC32 已验证）—— 本工程不再使用，仅取证留档 |

## 功能状态

* ✅ **图形启动菜单**（GRUB 风格）：音量上/下选择、电源键确认（`T158KeyDxe` 直读按键硬件），
  串口方向键/回车同样有效；3 秒倒计时默认启动第 1 项
* ✅ **输出：手机屏幕**（帧缓冲 GOP + GraphicsConsoleDxe）；**输入：物理按键 + 串口**
* ✅ **UFS 存储栈**：`SprdUfsHcDxe`（MMIO UFSHCI 薄壳，依赖 U-Boot 已初始化硬件）
  + EDK2 标准 `UfsPassThruDxe` / `DiskIoDxe` / `PartitionDxe` → GPT 分区
* ✅ **Boot Android / Boot TWRP**：`T158LinuxBootLib` 直接链式启动 arm64 kernel
  （boot.img v4 解析 → initrd 三段拼接 → DTB 塞 initrd 属性 → ExitBootServices → 跳转）
* ✅ **自包含部署**：`mkpayload.py -k` 可把原厂安卓 kernel 打进 UEFI payload 的 ramdisk 段，
  只刷一个分区即可，不依赖 boot_b

启动安卓的三种部署（代码全部支持，自动回退）

| 部署 | 操作 | Boot Android 走哪 |
|---|---|---|
| **自包含（推荐）** | `fastboot flash boot_a payload.img`（payload 内嵌安卓 kernel） | 读 boot_a 自己的 ramdisk 段 |
| UEFI 常驻 + 安卓在 boot_b | 同上，另把原厂安卓 boot.img 备份刷到 boot_b | 读 boot_b |
| UEFI 不落盘 | `fastboot boot payload.img`（boot_a/b 保持原厂） | 回退读 boot_a（原厂安卓原位） |

目录结构

```
t158-uefi/
├── README.md
├── build.sh / envsetup.sh    # 一键构建（自动探测工具链）
├── docs/
│   ├── 01-分区镜像与平台取证报告.md
│   ├── 02-UEFI固件设计.md
│   └── 03-构建刷机与验证.md
├── tools/
│   ├── analyze_partitions.py # 分区镜像总览 / 魔数 / 熵 / 字符串取证
│   ├── probe_t158.py         # boot.img v4、ARM64 kernel 头、DTBO、u-boot 字符串
│   ├── find_dtb.py           # 全盘搜索 device tree
│   ├── dt_report.py          # 基设备树平台报告(内存/串口/UFS/保留区)
│   ├── dt_query.py           # 按条件查询 DT 属性
│   ├── sprd_dhtb.py          # 展锐 DHTB 镜像 info/verify/extract/repack
│   └── mkpayload.py          # EDK2 FD → boot.img v4 / 裸载荷 (-k 内嵌安卓 kernel)
└── edk2/T158Pkg/
    ├── T158Pkg.dec / .dsc / .fdf
    ├── Include/              # T158Platform.h (寄存器/地址常量) 与各库头文件
    ├── Sec/                  # PayloadEntry.S (自搬迁) + SecMain.c (HOB/MMU/DXE 装载/DTB 传递)
    ├── Drivers/SprdUfsHcDxe/ # 展锐 MMIO UFSHCI 主机控制器协议
    ├── Application/T158BootMenu/   # 图形启动菜单
    └── Library/              # PlatformLib / FdtLib / SprdSerialPortLib / KeyDxe 相关 /
                              # PlatformBootManagerLib / BootControlLib / LinuxBootLib
```

 快速开始

0.克隆 EDK2 (stable202405)，与工程目录平级或用 EDK2 环境变量指定
git clone --depth 1 -b edk2-stable202405 --recurse-submodules \
        --shallow-submodules https://github.com/tianocore/edk2.git
export EDK2=~/edk2

构建 (需要 aarch64-linux-gnu-gcc: sudo apt install gcc-aarch64-linux-gnu)
./build.sh DEBUG

1.自包含 payload (可选): -k 指定原厂安卓 boot.img 备份, 内嵌其 kernel
python3 tools/mkpayload.py bootimg -i ~/edk2/Build/T158/DEBUG_GCC5/FV/T158.fd \
        -k boot_a_backup.img -o payload.img

2.解除 AVB (FDL2 状态下输入 dis_avb

3.试运行,确认菜单/串口/按键正常
fastboot boot payload.img


使用教程

准备:

* **串口（强烈建议）**：UART1 `0x20210000`，115200-8-N-1，接类型-C 底部调试口。
  没有串口也能用（屏幕+按键），但排错全靠猜。
备 份任何刷写前先备份
解除AVB：FDL2 状态下输入 `dis_avb`
)构建与打包

bash
克隆 EDK2 (stable202405)，export EDK2=<路径> 后:
./build.sh DEBUG
产物: ~/edk2/Build/T158/DEBUG_GCC5/FV/T158.fd

打包成 boot.img 格式 payload (可 fastboot boot / flash boot_a):
python3 tools/mkpayload.py bootimg -i "$EDK2/Build/T158/DEBUG_GCC5/FV/T158.fd" \
        -o payload.img
可选: 自包含模式 (-k 内嵌原厂安卓 kernel, 只刷 boot_a 即可启动安卓)
python3 tools/mkpayload.py bootimg -i "$EDK2/Build/T158/DEBUG_GCC5/FV/T158.fd" \
        -k boot_a_backup.img -o payload.img

可选: 裸载荷 (供 Spd_dump等工具封装):
python3 tools/mkpayload.py raw -i "$EDK2/Build/T158/DEBUG_GCC5/FV/T158.fd" \
        -o uefi_payload.bin
```

### 2) 制作 espboot 内容（推荐方案：换内核不用重刷固件）

espboot 是一个 FAT32 分区，UEFI 从它根目录读三个文件引导：

| 文件 | 必需 | 说明 |
|---|---|---|
| `kernel` | 是 | 未压缩 arm64 Image（"MZ" + 0x38 处 `"ARMd"`） |
| `ramdisk` | 是 | initrd，任意 cpio/拼接形态，UEFI 原样整块读入 |
| `dtb` | 否 | 不放则用 U-Boot 传下来的设备树（通常更好，已含 overlay） |

一键生成（输入=电脑上的原厂分区镜像备份）：

```bash
python3 tools/mk_esp.py <镜像目录> -o espboot
# espboot/kernel   <- boot_a 的 kernel 段
# espboot/ramdisk  <- init_boot_a ramdisk + vendor_boot_a vendor_ramdisk (cpio 拼接)
# espboot/dtb      <- vendor_boot_a 里的设备树
```

想把内核换成自己编译的：直接用 `Image`（开启 `CONFIG_RD_GZIP` 就能放 gzip 的 initrd）
覆盖 `kernel`，`ramdisk` 换成你的 cpio —— **不用重新构建/刷写 UEFI。

### 3) 部署

```bash
# 固化 UEFI (务必已备份 boot_a):
fastboot flash boot_a payload.img

# 试运行 (不落盘, 断电即回原厂):
fastboot boot payload.img
```

espboot 分区拷文件的常用办法（任选）：

```bash
经 TWRP (推荐) —— fastboot boot payload.img → 菜单选 Boot TWRP
adb push espboot/kernel /espboot/   # TWRP 里挂载 espboot 后 push 三个文件

2: mtools 直接写块设备镜像/分区 (Linux):
sudo mcopy -i /dev/block/sda47:: espboot/kernel ::/kernel   # 分区号以 pgpt 为准
```

4) 菜单操作

* **音量上/下** 选择，**电源键** 确认；串口方向键/回车同样有效
* 3 秒倒计时自动启动第 1 项
* `Boot Android` 候选链（自动回退，前一级失败才试下一级）：
  `espboot (FAT32 文件)` → `boot_a 内嵌 payload (-k 打包才有)` → `boot_b` → `boot_a`
* `Boot TWRP`：直接链式启动 recovery 分区

5) 排错（串口日志前缀）

| 前缀 | 模块 |
|---|---|
| `SprdUfsHc` / `UfsPassThru` | UFS 控制器/SCSI 层 —— `map -b` 看不到分区先查这里 |
| `LinuxBoot:` | kernel 加载/initrd/DTB —— 每步有详细信息 |
| 菜单失败提示 | 直接写明原因（UFS 未就绪 / 分区缺失 / 格式不支持） |

免责声明
刷写分区有变砖风险。请务必先备份 
本工程的"未在真机验证"清单见 `docs/03` §9。
