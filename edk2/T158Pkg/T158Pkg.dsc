## @file
#  T158Pkg DSC —— 紫光展锐 T158 (UMS9621) 最小 UEFI 固件 (可进入 Shell)
#
#  建议 edk2 版本: edk2-stable202405 (或 202311)
#  构建: build -a AARCH64 -t GCC5 -p T158Pkg/T158Pkg.dsc -b DEBUG
##

[Defines]
  PLATFORM_NAME                  = T158
  PLATFORM_GUID                  = 6D3B1E94-2C57-4F80-9A61-8E4D72C0B512
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/T158
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  FLASH_DEFINITION               = T158Pkg/T158Pkg.fdf

################################################################################
[LibraryClasses.common]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  # 注意: 不能用 MdePkg 的 BaseCacheMaintenanceLib (那是模拟器的空实现)!
  # UFS DMA 的 Map/Unmap 需要真正执行 DC CVAC/DC IVAC 指令。
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/BaseExtractGuidedSectionLib/BaseExtractGuidedSectionLib.inf
  LzmaCustomDecompressLib|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  PostCodeLib|MdePkg/Library/BasePostCodeLibDebug/BasePostCodeLibDebug.inf
  ArmLib|ArmPkg/Library/ArmLib/ArmBaseLib.inf
  ArmMmuLib|ArmPkg/Library/ArmMmuLib/ArmMmuBaseLib.inf
  ArmDisassemblerLib|ArmPkg/Library/ArmDisassemblerLib/ArmDisassemblerLib.inf
  ArmSmcLib|ArmPkg/Library/ArmSmcLib/ArmSmcLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerPhyCounterLib/ArmGenericTimerPhyCounterLib.inf
  ArmGicLib|ArmPkg/Drivers/ArmGic/ArmGicLib.inf
  ArmGicArchLib|ArmPkg/Library/ArmGicArchLib/ArmGicArchLib.inf
  SerialPortLib|T158Pkg/Library/SprdSerialPortLib/SprdSerialPortLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf
  T158FdtLib|T158Pkg/Library/T158FdtLib/T158FdtLib.inf
  # 完整 libfdt (fdt_open_into/fdt_setprop_u64), LinuxBootLib 用来给 DTB 塞 initrd 属性
  FdtLib|EmbeddedPkg/Library/FdtLib/FdtLib.inf
  T158PlatformLib|T158Pkg/Library/T158PlatformLib/T158PlatformLib.inf
  # 改写 misc 分区控制原厂 U-Boot 的启动目标 (切槽 / 进 recovery)
  T158BootControlLib|T158Pkg/Library/T158BootControlLib/T158BootControlLib.inf
  # UEFI 直接链式启动 Android/Linux kernel (boot.img v4 + ExitBootServices)
  T158LinuxBootLib|T158Pkg/Library/T158LinuxBootLib/T158LinuxBootLib.inf
  PrePiHobListPointerLib|T158Pkg/Library/T158PrePiHobListPointerLib/T158PrePiHobListPointerLib.inf

  # 控制台 / BDS
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLibDevicePathProtocol/UefiDevicePathLibDevicePathProtocol.inf
  UefiBootManagerLib|MdeModulePkg/Library/UefiBootManagerLib/UefiBootManagerLib.inf
  PlatformBootManagerLib|T158Pkg/Library/T158PlatformBootManagerLib/T158PlatformBootManagerLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf
  BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf
  ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
  HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf

  # ---- edk2-stable202405 必需的库类实例 (按 AARCH64 / 无 TPM 选择) ----
  # DXE 通用兜底 (SEC / DXE_CORE 段有更具体的覆盖)
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  DebugAgentLib|MdeModulePkg/Library/DebugAgentLibNull/DebugAgentLibNull.inf
  CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  CpuExceptionHandlerLib|ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  DxeCoreEntryPoint|MdePkg/Library/DxeCoreEntryPoint/DxeCoreEntryPoint.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  UefiDecompressLib|MdePkg/Library/BaseUefiDecompressLib/BaseUefiDecompressLib.inf
  OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf
  SafeIntLib|MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  ResetSystemLib|ArmPkg/Library/ArmSmcPsciResetSystemLib/ArmSmcPsciResetSystemLib.inf
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf
  SecurityManagementLib|MdeModulePkg/Library/DxeSecurityManagementLib/DxeSecurityManagementLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  AuthVariableLib|MdeModulePkg/Library/AuthVariableLibNull/AuthVariableLibNull.inf
  VarCheckLib|MdeModulePkg/Library/VarCheckLib/VarCheckLib.inf
  VariableFlashInfoLib|MdeModulePkg/Library/BaseVariableFlashInfoLib/BaseVariableFlashInfoLib.inf
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLib.inf
  VariablePolicyHelperLib|MdeModulePkg/Library/VariablePolicyHelperLib/VariablePolicyHelperLib.inf

[LibraryClasses.common.SEC]
  # SEC 阶段没有 PCD 数据库, 全部 PCD 都是编译期常量
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  # PrePi 阶段: HOB / 内存分配走 PrePi 版本 (配合 T158PrePiHobListPointerLib)
  HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
  MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
  PrePiLib|EmbeddedPkg/Library/PrePiLib/PrePiLib.inf

[LibraryClasses.common.DXE_CORE]
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  # DevicePathProtocol 版不支持 DXE_CORE, 核心用通用版
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  MemoryAllocationLib|MdeModulePkg/Library/DxeCoreMemoryAllocationLib/DxeCoreMemoryAllocationLib.inf
  HobLib|MdePkg/Library/DxeCoreHobLib/DxeCoreHobLib.inf

[LibraryClasses.common.DXE_DRIVER]
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  # 普通 VariablePolicyLib 不支持 RUNTIME, 运行时驱动用 RuntimeDxe 版
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLibRuntimeDxe.inf

[LibraryClasses.common.UEFI_APPLICATION]
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf

################################################################################
[PcdsFixedAtBuild.common]
  # ---------------- 平台 (来自实测取证) ----------------
  gT158TokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gT158TokenSpaceGuid.PcdSystemMemorySize|0x80000000
  gT158TokenSpaceGuid.PcdUartBase|0x20210000
  gT158TokenSpaceGuid.PcdUartClockHz|26000000
  gT158TokenSpaceGuid.PcdUartBaudRate|115200
  gT158TokenSpaceGuid.PcdUseDeviceTree|TRUE
  gT158TokenSpaceGuid.PcdBootToShellFirst|TRUE

  # ---------------- GIC / 定时器 ----------------
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x12000000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x12040000
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27

  # ---------------- 显示 (帧缓冲 GOP → 屏幕上的 Shell) ----------------
  # 实测: /reserved-memory/logobuffer@be000000, 0x9E4000 字节
  # 屏上输出: GOP + GraphicsConsoleDxe → ConOut
  # 输入    : 仍为串口 (电脑端串口终端敲命令)
  gT158TokenSpaceGuid.PcdFrameBufferBase|0xBE000000
  gT158TokenSpaceGuid.PcdFrameBufferSize|0x009E4000
  gT158TokenSpaceGuid.PcdScreenWidth|1080
  gT158TokenSpaceGuid.PcdScreenHeight|2400
  gT158TokenSpaceGuid.PcdScreenStride|0
  gT158TokenSpaceGuid.PcdScreenPixelFormat|0
  gT158TokenSpaceGuid.PcdGopScale|2

  # ---------------- reboot-mode (待取证: py -3 tools/probe_rebootmode.py) ----------------
  # 填 0 时 Boot Recovery 退化为普通冷重启
  gT158TokenSpaceGuid.PcdRebootModeRegBase|0x00000000
  gT158TokenSpaceGuid.PcdRebootModeRegStride|4
  gT158TokenSpaceGuid.PcdRebootModeRecoveryLo|0x00000000
  gT158TokenSpaceGuid.PcdRebootModeRecoveryHi|0x00000000
  gT158TokenSpaceGuid.PcdRebootModeNormalLo|0x00000000
  gT158TokenSpaceGuid.PcdRebootModeNormalHi|0x00000000

  # ---------------- 串口控制台 ----------------
  # 4 = VT100Plus (TerminalDxe)
  gEfiMdePkgTokenSpaceGuid.PcdDefaultTerminalType|4
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|115200
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultDataBits|8
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultStopBits|1
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultReceiveFifoDepth|16

  # ---------------- 启动菜单 ----------------
  # 注: 202405 起该 PCD 归属 MdePkg, 不再是 MdeModulePkg
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|0

  # ---------------- 调试 ----------------
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x27
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x8000004F

  # ---------------- 无 Flash: 变量放内存 ----------------
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize|0x2000
  gEfiMdeModulePkgTokenSpaceGuid.PcdVariableStoreSize|0x10000

  # ---------------- BDS ----------------
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|3
  gEfiMdeModulePkgTokenSpaceGuid.PcdConInConnectOnDemand|FALSE

# 以下 PCD 在 MdeModulePkg.dec 里不是 FixedAtBuild, 必须按 DEC 的类型放到对应段
[PcdsPatchableInModule.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoHorizontalResolution|1080
  gEfiMdeModulePkgTokenSpaceGuid.PcdVideoVerticalResolution|2400
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|0

[PcdsFeatureFlag.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdInstallAcpiSdtProtocol|FALSE

################################################################################
[Components.common]
  T158Pkg/Sec/Sec.inf

  #
  # 以下模块与 T158Pkg.fdf 中的 [FV.FVMAIN] 一一对应
  #
  MdeModulePkg/Core/Dxe/DxeMain.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  MdeModulePkg/Universal/Metronome/Metronome.inf
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  ArmPkg/Drivers/ArmGic/ArmGicDxe.inf
  ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
  # 注意: 这里刻意不使用 TerminalDxe。TerminalDxe 会同时提供 ConIn 和 ConOut,
  # 它的 80x25 一旦进入 ConOut, ConSplitter 取最小值会把屏幕也压成 80x25。
  # 改用 SprdSerialConInDxe: 串口只做输入。
  T158Pkg/Drivers/SprdSerialConInDxe/SprdSerialConInDxe.inf
  T158Pkg/Drivers/T158KeyDxe/T158KeyDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  T158Pkg/Drivers/SprdGopDxe/SprdGopDxe.inf
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  #
  # UFS 存储栈: 展锐 MMIO HC + 标准 UFS PassThru + 磁盘 IO + GPT 分区
  # 有了它们才有 BlockIo 分区设备, 菜单才能读 boot_b / recovery 启动安卓/TWRP
  #
  T158Pkg/Drivers/SprdUfsHcDxe/SprdUfsHcDxe.inf
  MdeModulePkg/Bus/Ufs/UfsPassThruDxe/UfsPassThruDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  # FAT32 文件系统驱动: espboot 等 FAT 分区提供 SimpleFileSystem,
  # LinuxBootLib 的 ESP 引导路径 (T158LinuxBootFromEsp) 靠它读 kernel/ramdisk/dtb
  FatPkg/EnhancedFatDxe/Fat.inf

  #
  # GRUB 风格启动菜单 (BDS 拉起, 菜单项可进 Shell / WinPE)
  #
  T158Pkg/Application/T158BootMenu/T158BootMenu.inf

  #
  # 内嵌 Shell: 由 T158BootMenu 菜单项拉起
  #
  ShellPkg/Application/Shell/Shell.inf {
    <PcdsFixedAtBuild>
      gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE
      gEfiShellPkgTokenSpaceGuid.PcdShellSupportLevel|3
  }

################################################################################
[BuildOptions]
  # 1) GCC 12+ / Ubuntu 默认开启 -fstack-protector-strong, 而 EDK2 没有
  #    __stack_chk_guard / __stack_chk_fail 的实现, 链接期必报 undefined reference。
  # 2) EDK2 使用 -fno-builtin, GCC 会把内存填充循环优化成 memcpy/memset 外部调用,
  #    同样链接失败; -fno-tree-loop-distribute-patterns 可禁止该优化。
  *_*_*_CC_FLAGS = -fno-stack-protector -fno-tree-loop-distribute-patterns
  # 3) EDK2 是裸机固件, 不链接 glibc, 没有 memcpy/memset/memmove 符号。
  #    GCC 生成的这些调用直接别名到 BaseMemoryLib 的 CopyMem / SetMem。
  *_*_*_DLINK_FLAGS = -Wl,--defsym=memcpy=CopyMem -Wl,--defsym=memmove=CopyMem -Wl,--defsym=memset=SetMem
