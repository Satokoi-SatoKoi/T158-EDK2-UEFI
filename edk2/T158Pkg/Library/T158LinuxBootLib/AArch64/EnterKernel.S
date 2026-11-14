#------------------------------------------------------------------------------
# T158EnterKernel (UINT64 Entry, UINT64 DtBase) —— 不返回
#
# arm64 kernel 启动约定 (Documentation/arch/arm64/booting.rst):
#   x0 = DTB 物理地址, x1-x3 = 0
#   MMU off, D-cache off, I-cache 无要求 (这里一并关掉)
#   CPU 可在 EL1 或 EL2; UEFI DXE 在 EL1, kernel 接受 EL1 启动
#   调用方已保证 Entry 处是解压好的 Image (MZ/ARMd), 且 cache 已回写
#
# 注意: .S 文件里不能用 ASM_FUNC 宏 (那是 C 编译器的 __asm__ 宏),
#       必须用纯 GNU as 语法。GCC 工具链下符号无前缀 (ASM_PFX(Name)=Name)。
#------------------------------------------------------------------------------
    .text
    .align  3
    .global T158EnterKernel
    .type   T158EnterKernel, %function

T158EnterKernel:
    // x0 = Entry, x1 = DtBase
    mov     x4, x0                  // x4 = kernel 入口
    mov     x0, x1                  // x0 = DTB 物理地址 (kernel 约定)
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr

    // 关 D-cache / I-cache / MMU (UEFI 是 1:1 映射, PC 已在物理地址, 安全)
    mrs     x5, sctlr_el1
    bic     x5, x5, #1              // SCTLR.M = 0 (MMU off)
    bic     x5, x5, #(1 << 2)       // SCTLR.C = 0 (D-cache off)
    bic     x5, x5, #(1 << 12)      // SCTLR.I = 0 (I-cache off)
    dsb     sy
    msr     sctlr_el1, x5
    isb

    // 清 TLB (关 MMU 后残留映射无意义)
    tlbi    vmalle1
    dsb     sy
    isb

    br      x4                      // 跳 kernel, 不返回

    .size   T158EnterKernel, . - T158EnterKernel
