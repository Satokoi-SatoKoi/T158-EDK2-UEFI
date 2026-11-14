/** @file
 *  灞曢攼 (Unisoc) UART 鈥斺€?compatible = "sprd,ums9621-uart", "sprd,sc9836-uart"
 *
 *  瀵勫瓨鍣ㄥ畾涔夊搴?Linux drivers/tty/serial/sprd_serial.c銆?
 *  鏈┍鍔ㄤ负绾疆璇㈡ā寮?(涓嶅紑涓柇銆佷笉寮€ DMA), 渚?SEC/PEI 涓?DXE 涓や釜闃舵鍏辩敤:
 *    - SEC/PEI: DebugLib / 鏃╂湡鎵撳嵃
 *    - DXE    : SerialDxe + TerminalDxe 鎻愪緵 ConIn/ConOut, 浠庤€屽緱鍒?Shell 浜や簰鐣岄潰
 *
 *  瀹炴祴 (vendor_boot 鍐呰澶囨爲):
 *    aliases.serial1 = /soc/ap-apb/serial@10000
 *    ap-apb ranges   = <0 0 0x20200000 0x100000>
 *    => ttyS1 鐗╃悊鍩哄潃 = 0x20210000, chosen.stdout-path = "serial1:115200n8"
 **/

#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/SerialPortLib.h>
#include <T158Platform.h>

STATIC UINTN  mUartBase    = T158_UART1_BASE;
STATIC UINT32 mUartClockHz = T158_UART_INPUT_CLOCK_HZ;
STATIC UINT64 mBaudRate    = T158_UART_DEFAULT_BAUD;
STATIC UINT32 mStride      = 4;

STATIC
UINT32
UartRead (
  IN UINTN  Offset
  )
{
  return MmioRead32 (mUartBase + Offset);
}

STATIC
VOID
UartWrite (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 (mUartBase + Offset, Value);
}

/**
  璁剧疆娉㈢壒鐜? 灞曢攼 UART 鐨勯櫎鏁板瘎瀛樺櫒涓?32 浣?(CLK0/CLK1),
  divisor = uartclk / baud; 杩欓噷閲囩敤涓庡師鍘?u-boot 涓€鑷寸殑 "鏃堕挓/娉㈢壒鐜? 璁＄畻銆?
**/
STATIC
VOID
UartSetBaud (
  IN UINT64  BaudRate
  )
{
  UINT32  Divisor;

  if (BaudRate == 0) {
    return;
  }

  Divisor = (UINT32)(mUartClockHz / (UINT32)BaudRate);

  UartWrite (SPRD_UART_CLKD0, Divisor & 0xFFFF);
  UartWrite (SPRD_UART_CLKD1, (Divisor >> 16) & 0xFFFF);
}

RETURN_STATUS
EFIAPI
SprdSerialPortLibConstructor (
  VOID
  )
{
  mUartBase    = (UINTN)FixedPcdGet64(PcdUartBase);
  mUartClockHz = FixedPcdGet32(PcdUartClockHz);
  mBaudRate    = FixedPcdGet64(PcdUartBaudRate);
  mStride      = FixedPcdGet32(PcdUartRegStride);
  if (mUartBase == 0) {
    mUartBase = T158_UART1_BASE;
  }
  if (mUartClockHz == 0) {
    mUartClockHz = T158_UART_INPUT_CLOCK_HZ;
  }
  if (mStride == 0) {
    mStride = 4;
  }

  return RETURN_SUCCESS;
}

/**
  鍒濆鍖栦覆鍙ｃ€?
  娉ㄦ剰: 鍘熷巶 U-Boot 宸茬粡鎶?ttyS1 鍒濆鍖栧ソ浜? 杩欓噷鍙仛 "鏈€灏忓啀閰嶇疆"
  (鍏?DMA銆佽娉㈢壒鐜?, 閬垮厤鐮村潖 U-Boot 宸叉湁鐨勫紩鑴?鏃堕挓閰嶇疆銆?
**/
RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  SprdSerialPortLibConstructor ();

  /* 鍏?DMA, 绾疆璇? 淇濈暀 U-Boot 璁剧疆鐨勬暟鎹綅/鏍￠獙/鍋滄浣?*/
  UartWrite (SPRD_UART_CTRL1, UartRead (SPRD_UART_CTRL1) & ~SPRD_UART_CTRL1_DMA_ENABLE);
  UartWrite (SPRD_UART_CTRL0, UartRead (SPRD_UART_CTRL0) | SPRD_UART_CTRL0_ENABLE);

  UartSetBaud (mBaudRate);

  /* 娓呯┖鎺ユ敹 FIFO 涓彲鑳界殑娈嬬暀鏁版嵁 */
  while ((UartRead (SPRD_UART_STS1) & SPRD_UART_STS1_RX_FIFO_NOT_EMPTY) != 0) {
    (VOID)UartRead (SPRD_UART_RXD);
  }

  return RETURN_SUCCESS;
}

/**
  鍙戦€佹暟鎹?(杞, 閫愬瓧鑺傜瓑寰?FIFO 闈炴弧)銆?
**/
UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  UINTN  Index;
  UINT32 Retry;

  if (Buffer == NULL) {
    return 0;
  }

  for (Index = 0; Index < NumberOfBytes; Index++) {
    Retry = 0;
    while ((UartRead (SPRD_UART_STS1) & SPRD_UART_STS1_TX_FIFO_FULL) != 0) {
      if (++Retry > 0x1000000) {
        return Index;      /* 纭欢寮傚父鏃朵笉瑕佹绛? 鍚﹀垯鍥轰欢鐪嬭捣鏉ュ儚 "鍗℃" */
      }
    }

    if (Buffer[Index] == '\n') {
      UartWrite (SPRD_UART_TXD, '\r');
      while ((UartRead (SPRD_UART_STS1) & SPRD_UART_STS1_TX_FIFO_FULL) != 0) {
      }
    }

    UartWrite (SPRD_UART_TXD, Buffer[Index]);
  }

  return NumberOfBytes;
}

/**
  鎺ユ敹鏁版嵁 (杞)銆?
**/
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  UINTN  Index;

  if (Buffer == NULL) {
    return 0;
  }

  for (Index = 0; Index < NumberOfBytes; Index++) {
    if ((UartRead (SPRD_UART_STS1) & SPRD_UART_STS1_RX_FIFO_NOT_EMPTY) == 0) {
      break;
    }

    Buffer[Index] = (UINT8)(UartRead (SPRD_UART_RXD) & 0xFF);
  }

  return Index;
}

/**
  鏄惁鏈夋暟鎹彲璇汇€?
**/
BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return (UartRead (SPRD_UART_STS1) & SPRD_UART_STS1_RX_FIFO_NOT_EMPTY) != 0;
}

RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  if (Control == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  *Control = 0;
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64             *BaudRate,
  IN OUT UINT32             *ReceiveFifoDepth,
  IN OUT UINT32             *Timeout,
  IN OUT EFI_PARITY_TYPE    *Parity,
  IN OUT UINT8              *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits
  )
{
  if (BaudRate != NULL) {
    if (*BaudRate != 0) {
      mBaudRate = *BaudRate;
    }

    UartSetBaud (mBaudRate);
    *BaudRate = mBaudRate;
  }

  return RETURN_SUCCESS;
}

/**
  浠ヤ笅涓や釜鍑芥暟鏄緝鏂扮増鏈?MdePkg 涓?SerialPortLib 鐨勫彲閫夋帴鍙?
  鏃х増鏈笉澹版槑涔熸棤濡?(浠呬负鍏煎鑰屼繚鐣?銆?
**/
RETURN_STATUS
EFIAPI
SerialPortDrain (
  VOID
  )
{
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
SerialPortFlush (
  VOID
  )
{
  while ((UartRead (SPRD_UART_STS1) & SPRD_UART_STS1_RX_FIFO_NOT_EMPTY) != 0) {
    (VOID)UartRead (SPRD_UART_RXD);
  }

  return RETURN_SUCCESS;
}
