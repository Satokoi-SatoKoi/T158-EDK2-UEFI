# 02 · T158 UEFI 固件设计

## 1. 为什么是「内核载荷」而不是「替换 u-boot」

| 方案 | 可行性 | 说明 |
|---|---|---|
| A. 全量 EDK2 替换 SPL+u-boot | ✗ | BootROM / DDR 训练 / PMIC / ADI 全部在闭源 SPL 与 u-boot 里，无 TRM、无源码，等于重写 SoC bring-up |
| B. 给原厂 u-boot 回编 efi_loader | ✗（本工程） | 需要展锐 IDH 源码树（`u-boot15`/`u-boot2021`），本仓库只有二进制 |
| C. 让 u-boot 启动 UEFI 应用 (`BOOTAA64.EFI`) | ✗ | 实测原厂 u-boot **没有** efi_loader（见 01 报告 §4） |
| **D. EDK2 固件伪装成 arm64 内核，被原厂 u-boot 加载** | **✓ 采用** | 复用原厂全部初始化（DDR/时钟/PMIC/USB/UFS 已在 u-boot 就绪），固件只负责建立 UEFI 环境 |

方案 D 的启动链：

```
BootROM → splloader(SPL) → u-boot_a  ──(AVB 校验 boot_a)──▶  Linux kernel
                              │
                              └──换成──▶ T158 UEFI 载荷 (EDK2 SEC→DXE) → UEFI Shell
```

固件被加载时的状态（arm64 Linux 启动协议）：
`x0 = device tree 物理地址（可能为 0）`、MMU/数据 Cache 关闭、装载地址通常 ≠ 链接地址。

---

## 2. 固件内部流程

```
[0x00] ARM64 镜像头 (64B, magic "ARMd", 首指令 b → SEC 入口)     ← mkpayload.py 生成
[0x40] FD/FV (24MB: SEC + DXE Core + 驱动 + 内嵌 Shell)

  ① PayloadEntry.S   : 计算 装载地址/链接地址 差值 → 自搬整个 FD 到 0xA9000000 → 建栈 → 跳 C
  ② SecMain.c        : 清 .bss → 探测 DRAM/串口(优先用 x0 的 DT) → 起串口
  ③ SecMain.c        : 构造 HOB(内存资源/内存分配/FV/CPU) → ArmConfigureMmu() 开 MMU+Cache
  ④ SecMain.c        : 在 FV 里找出 DXE Core 的 PE32 → PeCoff 装载 → 跳转
  ⑤ DXE              : CpuDxe / GicDxe / TimerDxe / SerialDxe / TerminalDxe / PcdDxe / BdsDxe
  ⑥ BDS              : T158PlatformBootManagerLib 直接从 FV 取出内嵌 Shell 并 StartImage
  ⑦ UEFI Shell       : 串口 (UART1 @0x20210000, 115200-8-N-1) 上的交互界面
```

### 2.1 为什么要自搬迁

U-Boot 把内核（也就是本固件）放在 DRAM 低端（约 `DRAM_BASE + 0x8000`），而 EDK2 的 SEC/DXE Core
是按固定地址链接的 XIP 映像。因此用一段 30 行的汇编 stub（`Sec/AArch64/PayloadEntry.S`）在
Cache 关闭状态下把整个 FD 搬到 `PcdFdBaseAddress`，再按链接地址继续运行。

### 2.2 为什么 Shell 内嵌在 FV

T158 还没有 EDK2 的 UFS 驱动，Shell 无法从磁盘加载。`T158PlatformBootManagerLib`
用 `GetSectionFromFv()` 直接把 FV 中的 Shell PE32 段 `LoadImage` + `StartImage`，
不依赖变量服务、不依赖设备路径、不依赖文件系统 —— **上电即进 Shell**。
UFS（`sprd,ufshc-ums9621` @0x22000000）的 `UfsHc` 驱动是后续工作（见 §4）。

---

## 3. 内存布局（全部由 PCD 控制，见 `T158Pkg.dec`）

```
0x80000000  ┌──────────────────────────────────────────────┐
            │  DRAM (DT 报告 8GB, 默认向 UEFI 暴露 2GB)      │
0x87800000  │  ...保留区: sipc / modem / iq ... (见 01 §2.1)│
0xA9000000  ├──────────────────────────────────────────────┤
            │  FD / FV   24MB   (SEC + DXE Core + Shell)    │  PcdFdBaseAddress
0xAA800000  ├──────────────────────────────────────────────┤
            │  SEC 栈     256KB                             │  PcdPrePiStackBase
0xAA840000  ├──────────────────────────────────────────────┤
            │  HOB 池 1MB + DXE 可用内存 32MB                │  PcdUefiMemoryRegionBase
0xAC840000  └──────────────────────────────────────────────┘
```

> 若你的机型 DRAM 保留区不同，改这 3 个 PCD 即可（务必避开 01 报告 §2.1 的区域）。

---

## 4. 模块清单

| 目录 | 模块 | 作用 |
|---|---|---|
| `Sec/` | `T158Sec` | 载荷入口：自搬迁、HOB、MMU、装载 DXE Core |
| `Library/T158PlatformLib/` | `T158PlatformLib` | DRAM/串口探测（DT 优先、PCD 兜底）+ MMU 映射表 |
| `Library/T158FdtLib/` | `T158FdtLib` | 只读 FDT 子集：memory / stdout-path / ranges 地址翻译 |
| `Library/SprdSerialPortLib/` | `SprdSerialPortLib` | 展锐 UART（`sprd,sc9836-uart`）轮询驱动 |
| `Library/T158PlatformBootManagerLib/` | — | 从 FV 拉起内嵌 Shell；打印平台信息 |
| `Library/T158PrePiHobListPointerLib/` | — | HOB 链表指针（双命名，兼容不同 edk2 版本） |

### 4.1 后续可扩展（当前未实现）

* **UFS 驱动**：`MdeModulePkg/Bus/Ufs/UfsPassThruDxe` + 自写 `SprdUfsHcDxe`（MMIO UFSHCI @0x22000000）→ 得到 `BlockIo` → 才能 `FS0:` 访问 `super`/ESP。
* **显示输出**：`logobuffer@0xBE000000` 是 u-boot 预留的 framebuffer；加一个 GOP 驱动即可在屏幕上看到 Shell。
* **USB**：展锐 musb/dwc3 的 XHCI 驱动 → USB 键盘。

---

## 5. 与 edk2 版本相关的注意事项（编译摩擦速查）

本包只依赖稳定接口，但下列位置在不同 edk2 tag 上可能略有差异：

| 位置 | 可能的问题 | 处理 |
|---|---|---|
| `ArmConfigureMmu()` 签名 | 有的版本第 2/3 参数为 `IN` | 按 `ArmPlatformPkg/MemoryInitPei` 中的调用方式改一行 |
| `ARM_MEMORY_REGION_ATTRIBUTE_*` | 新版本改名成 `..._DEVICE_nGnRE` 等 | 用新枚举名替换 |
| `MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf` | 旧版本为 `VariableDxe.inf` / `EmuVariableDxe.inf` | 换成对应路径；或整组删除（Shell 仍可运行） |
| `ArmPkg/Drivers/TimerDxe` | 新版本可能改为 `ArmGenericTimerDxe` | `find . -name "*Timer*Dxe.inf"` |
| `gT158ShellFileGuid` | 必须与 `ShellPkg/Application/Shell/Shell.inf` 的 `FILE_GUID` 一致 | 默认已填 edk2 的 `7C04A583-...`；不符则改 DEC |
| `ArmMmuLib` 是否随 edk2 提供 | 缺失时改用 `ArmPkg/Library/ArmMmuLib` 的替代实现 | — |

推荐固定 `edk2-stable202405`，可减少上述大部分差异。
