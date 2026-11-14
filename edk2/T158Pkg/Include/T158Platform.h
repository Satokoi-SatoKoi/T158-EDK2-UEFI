/** @file
 *  T158 (UMS9621) 平台常量与寄存器定义
 *
 *  所有数值均来自对原厂分区镜像的实测取证 (见 docs/01-分区镜像与平台取证报告.md):
 *    - 设备树: vendor_boot_a @0x4CF0040, size 0x25C7C (Android boot header v4 的 vendor_boot 尾部)
 *    - 串口  : /aliases serial1 -> /soc/ap-apb/serial@10000, ap-apb ranges <0 0 0x20200000 0x100000>
 *    - UFS   : /soc/ap-ahb/ufs@2000000, ap-ahb ranges <0 0 0x20000000 0x22F0000>
 *    - GIC   : /interrupt-controller@12000000, arm,gic-v3
 **/

#ifndef T158_PLATFORM_H_
#define T158_PLATFORM_H_

/* ------------------------------------------------------------------ SoC */
#define T158_SOC_NAME            "UMS9621"
#define T158_BOARD_NAME          "sprd,ums9621-1h10"

/* ------------------------------------------------------- 地址空间 (物理) */
#define T158_AP_APB_BASE         0x20200000U   /* ap-apb ranges 第 1 项        */
#define T158_AP_APB_SIZE         0x00100000U
#define T158_AP_AHB_BASE         0x20000000U   /* ap-ahb ranges 第 1 项        */
#define T158_AP_AHB_SIZE         0x022F0000U
#define T158_AON_BASE            0x64000000U
#define T158_PUB_BASE            0x60000000U

/* UART: serial0@0 / serial1@0x10000 / serial2@0x20000 / serial3@0x30000 */
#define T158_UART0_BASE          (T158_AP_APB_BASE + 0x00000U)
#define T158_UART1_BASE          (T158_AP_APB_BASE + 0x10000U)  /* ttyS1, 默认控制台 */
#define T158_UART2_BASE          (T158_AP_APB_BASE + 0x20000U)
#define T158_UART3_BASE          (T158_AP_APB_BASE + 0x30000U)
#define T158_UART_REG_LENGTH     0x100U

/* UFS 主机控制器 (sprd,ufshc-ums9621) */
#define T158_UFS_BASE            0x22000000U
#define T158_UFS_LENGTH          0x00010000U

/* SDIO 控制器 (sprd,sdhci-r11) */
#define T158_SDIO0_BASE          0x42200000U
#define T158_SDIO1_BASE          0x42210000U
#define T158_SDIO2_BASE          0x42220000U

/* GICv3 */
#define T158_GICD_BASE           0x12000000U
#define T158_GICD_LENGTH         0x00020000U
#define T158_GICR_BASE           0x12040000U
#define T158_GICR_LENGTH         0x00100000U
#define T158_GICR_STRIDE         0x00020000U

/* ------------------------------------------------------------- 串口默认 */
#define T158_UART_INPUT_CLOCK_HZ 26000000U     /* 典型展锐 UART 源时钟; 若波特率偏差请改 48000000 */
#define T158_UART_DEFAULT_BAUD   115200U

/* ------------------------------------------- ARM64 Linux 内核镜像头 (载荷) */
/* 原厂 boot.img 的 kernel 为未压缩 arm64 Image, 其 magic 实测为 "ARMd"(非标准 "ARM64"),
 * 因此本工程生成载荷时沿用 "ARMd", 保证与原厂 U-Boot 的 booti 检查一致。 */
#define T158_PAYLOAD_MAGIC_ARM64 0x644D5241U   /* "ARMd" (原厂实测值) */
#define T158_PAYLOAD_MAGIC_STD   0x34364D52U   /* "ARM64" (主线标准值) */
#define T158_PAYLOAD_PE_OFFSET   0x40U
#define T158_PAYLOAD_HEADER_SIZE 0x40U

/* -------------------------------------------------- 展锐 UART 寄存器偏移 */
/* 对应 Linux drivers/tty/serial/sprd_serial.c (compatible = "sprd,sc9836-uart") */
#define SPRD_UART_TXD            0x0000U   /* 发送保持寄存器            */
#define SPRD_UART_RXD            0x0004U   /* 接收缓冲寄存器            */
#define SPRD_UART_STS1           0x0008U   /* 状态 1                    */
#define SPRD_UART_STS2           0x000CU   /* 状态 2                    */
#define SPRD_UART_CTRL0          0x0010U
#define SPRD_UART_CTRL1          0x0014U
#define SPRD_UART_CTRL2          0x0018U
#define SPRD_UART_CLKD0          0x0020U   /* 波特率低 16 位            */
#define SPRD_UART_CLKD1          0x0024U   /* 波特率高 16 位            */

/* 状态位: 与 Linux 驱动保持一致; 若真机无输出/乱码, 优先核对这两个位 */
#define SPRD_UART_STS1_TX_FIFO_FULL    BIT9   /* 0x0200 */
#define SPRD_UART_STS1_RX_FIFO_NOT_EMPTY BIT8 /* 0x0100 */

/* 控制位 */
#define SPRD_UART_CTRL0_ENABLE         BIT0
#define SPRD_UART_CTRL1_DMA_ENABLE     BIT4   /* 关 DMA, 轮询模式 */
#define SPRD_UART_CTRL2_PARITY         BIT0

#endif /* T158_PLATFORM_H_ */
