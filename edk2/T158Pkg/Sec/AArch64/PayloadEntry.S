//------------------------------------------------------------------------------
//
//  T158 UEFI 载荷入口 (AArch64)
//
//  原厂 U-Boot 把本固件当作 arm64 Linux 内核镜像加载并跳转, 进入时:
//    x0 = device tree 物理地址 (可能为 0)
//    MMU/D-Cache 关闭 (arm64 启动协议)
//    当前 PC = "装载地址", 通常不是本固件的链接地址
//
//  本 stub 的职责:
//    1) 计算 装载地址 与 链接地址 的差值
//    2) 把整个 FD 自搬到链接地址 (PcdFdBaseAddress)
//    3) 建立栈, 跳转到 C 入口 T158SecStartup, 并保留 x0
//
//  全程只用 x9-x20, 不破坏 x0-x8, 且搬迁完成前全部使用 PC 相对寻址。
//
//------------------------------------------------------------------------------

#include <AsmMacroIoLib.h>

#define FD_BASE_GLOBAL      gT158FdBaseAddress
#define FD_SIZE_GLOBAL      gT158FdSize
#define STACK_TOP_GLOBAL    gT158StackTop

    .text
    .align 3

ASM_FUNC(PayloadEntry)
    // 关中断, 无效化指令 Cache
    msr     daifset, #0xf
    ic      iallu
    isb

    // x9  = 运行时(装载区)的 PayloadEntry 地址
    // x10 = 链接地址的 PayloadEntry
    adr     x9,  ASM_PFX(PayloadEntry)
    ldr     x10, =ASM_PFX(PayloadEntry)

    // x19 = 运行时地址 - 链接地址 (搬迁偏移量)
    subs    x19, x9, x10
    b.eq    L_AtLinkAddress

    // ---- 读取链接期的全局变量 (必须从"装载区"读: 链接地址 + x19) ----
    ldr     x11, =ASM_PFX(FD_BASE_GLOBAL)
    add     x11, x11, x19
    ldr     x13, [x11]                  // x13 = 目标(链接)基址

    ldr     x12, =ASM_PFX(FD_SIZE_GLOBAL)
    add     x12, x12, x19
    ldr     x15, [x12]                  // x15 = FD 长度

    // ---- 源地址 = 装载区基址 = x13 + x19 ----
    add     x16, x13, x19

    // ---- memmove (x13 <- x16, x15 字节) ----
    cmp     x13, x16
    b.hi    L_CopyBackward

L_CopyForward:
    mov     x17, xzr
1:  cmp     x17, x15
    b.hs    L_CopyDone
    // AArch64 不支持 [reg, reg] 寻址, 只能用后索引 (post-index) 形式
    ldr     x14, [x16], #8
    str     x14, [x13], #8
    add     x17, x17, #8
    b       1b

L_CopyBackward:
    // 目的地址高于源地址时反向拷贝, 避免覆盖尚未搬走的数据
    add     x16, x16, x15                   // 源末尾
    add     x13, x13, x15                   // 目的末尾
    mov     x17, x15
2:  ldr     x14, [x16, #-8]!                // 前索引 (pre-index)
    str     x14, [x13, #-8]!
    subs    x17, x17, #8
    b.hi    2b
    b       L_CopyDone

L_CopyDone:
    // 数据 Cache: 搬迁是在 Cache 关闭的情况下做的, 无需 flush
    dsb     sy
    isb

L_AtLinkAddress:
    // ---- 建立栈 ----
    ldr     x18, =ASM_PFX(STACK_TOP_GLOBAL)
    ldr     x18, [x18]
    mov     sp, x18

    // ---- 跳到 C 入口 (x0 保持为 device tree 地址) ----
    ldr     x20, =ASM_PFX(T158SecStartup)
    br      x20

L_DeadLoop:
    wfi
    b       L_DeadLoop
