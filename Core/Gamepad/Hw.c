/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad Hw - bringing up the gamepad MCU and its UART.
*/

#include "Core/Gamepad/Hw.h"

#include "Base/Memory.h"

#define COUNT_OF(Array) (sizeof (Array) / sizeof (*(Array)))

// TLMM

#define TLMM_CFG_FUNC_SHIFT 2
#define TLMM_CFG_DRV_SHIFT  6
#define TLMM_CFG_OE         (1u << 9)
#define TLMM_IO_OUT         (1u << 1)

STATIC UINT64
TlmmCfg (
  IN CONST GAMEPAD_CONFIG *Config,
  IN UINT16                Gpio
)
{
  CONST GAMEPAD_SOC *Soc = &gGamepadSocs[Config->Soc];

  return Config->Tlmm[GamepadSocTile (Soc, Gpio)] +
         (UINT64)Gpio * Soc->TlmmStride;
}

STATIC UINT32
CfgOf (IN CONST GAMEPAD_PIN *Pin)
{
  return Pin->Pull | (UINT32)Pin->Func << TLMM_CFG_FUNC_SHIFT |
         (UINT32)(Pin->DriveMa / 2 - 1) << TLMM_CFG_DRV_SHIFT;
}

// Saves Gpio's registers, once.
STATIC VOID
Save (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN OUT GAMEPAD_SAVED    *Saved,
  IN UINT16                Gpio
)
{
  UINT64 Cfg = TlmmCfg (Config, Gpio);

  for (UINTN Index = 0; Index < Saved->PinCount; Index++) {
    if (Saved->Gpio[Index] == Gpio) {
      return;
    }
  }
  if (Saved->PinCount < COUNT_OF (Saved->Gpio)) {
    Saved->Gpio[Saved->PinCount] = Gpio;
    Saved->Cfg[Saved->PinCount]  = MmioRead32 (M, Cfg);
    Saved->Io[Saved->PinCount]   = MmioRead32 (M, Cfg + 4);
    Saved->PinCount++;
  }
}

// Drives Line to its active or inactive level as a GPIO output, keeping the
// pad settings the devicetree gave the pin.
STATIC VOID
Drive (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN CONST GAMEPAD_LINE   *Line,
  IN BOOLEAN               Active
)
{
  GAMEPAD_PIN Pin  = {Line->Gpio, 0, GAMEPAD_PULL_NONE, 2};
  UINT64      Cfg  = TlmmCfg (Config, Line->Gpio);
  BOOLEAN     High = Active != Line->ActiveLow;

  if (!Line->Present) {
    return;
  }
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    if (Config->Pins[Index].Gpio == Line->Gpio) {
      Pin      = Config->Pins[Index];
      Pin.Func = 0;
    }
  }
  // The level first, so the pin doesn't glitch when its output turns on.
  MmioWrite32 (M, Cfg + 4, High ? TLMM_IO_OUT : 0);
  MmioWrite32 (M, Cfg, CfgOf (&Pin) | TLMM_CFG_OE);
}

VOID
GamepadHwPinsOn (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  OUT GAMEPAD_SAVED       *Saved
)
{
  CONST GAMEPAD_LINE *Lines[] = {
    &Config->Supply,
    &Config->Enable,
    &Config->Reset,
    &Config->Boot,
  };

  ZeroMem (Saved, sizeof (*Saved));
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    Save (M, Config, Saved, Config->Pins[Index].Gpio);
  }
  for (UINTN Index = 0; Index < COUNT_OF (Lines); Index++) {
    if (Lines[Index]->Present) {
      Save (M, Config, Saved, Lines[Index]->Gpio);
    }
  }
  for (UINTN Index = 0; Index < Config->AlwaysOnCount; Index++) {
    Save (M, Config, Saved, Config->AlwaysOn[Index].Gpio);
  }
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    MmioWrite32 (
      M, TlmmCfg (Config, Config->Pins[Index].Gpio),
      CfgOf (&Config->Pins[Index])
    );
  }
  // As Linux: the always-on supplies from the start, then as its probe:
  // supply on, boot and reset released, then enable.
  for (UINTN Index = 0; Index < Config->AlwaysOnCount; Index++) {
    Drive (M, Config, &Config->AlwaysOn[Index], TRUE);
  }
  Drive (M, Config, &Config->Supply, TRUE);
  Drive (M, Config, &Config->Boot, FALSE);
  Drive (M, Config, &Config->Reset, FALSE);
}

VOID
GamepadHwRelease (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config
)
{
  Drive (M, Config, &Config->Enable, TRUE);
  Drive (M, Config, &Config->Reset, TRUE);
}

// GCC

#define CBCR_CLK_OFF (1u << 31)
#define CBCR_HW_CTL  (1u << 1)
#define RCG_CMD      0x0
#define RCG_CFG      0x4
#define RCG_M        0x8
#define RCG_N        0xc
#define RCG_D        0x10
#define RCG_UPDATE   (1u << 0)
// SRC_DIV, SRC_SEL and MODE.
#define RCG_CFG_MASK  0x371fu
#define RCG_DUAL_EDGE (2u << 12)
#define RCG_MND_MASK  0xffffu

// 7.3728 MHz from GPLL0's 300 MHz even output: 300 * 384 / 15625. With 16x
// oversampling that is 115200 baud at a divider of 4, at 32x one of 2.
#define SE_RATE   7372800u
#define SE_RATE_M 384u
#define SE_RATE_N 15625u

// Polls before a register is taken not to change.
#define POLLS 1000000u

STATIC BOOLEAN
PollClear (
  IN CONST MMIO *M,
  IN UINT64      Addr,
  IN UINT32      Mask
)
{
  for (UINT32 Index = 0; Index < POLLS; Index++) {
    if (!(MmioRead32 (M, Addr) & Mask)) {
      return TRUE;
    }
  }
  return FALSE;
}

// Votes for Clock, saving its vote register's value first.
STATIC BOOLEAN
Vote (
  IN CONST MMIO              *M,
  IN CONST GAMEPAD_CONFIG    *Config,
  IN CONST GAMEPAD_SOC_CLOCK *Clock,
  IN OUT GAMEPAD_SAVED       *Saved
)
{
  UINT64  Reg   = Config->Gcc + Clock->VoteReg;
  BOOLEAN Known = FALSE;

  for (UINTN Index = 0; Index < Saved->VoteCount && !Known; Index++) {
    Known = Saved->VoteReg[Index] == Clock->VoteReg;
  }
  if (!Known && Saved->VoteCount < COUNT_OF (Saved->VoteReg)) {
    Saved->VoteReg[Saved->VoteCount] = Clock->VoteReg;
    Saved->Vote[Saved->VoteCount]    = MmioRead32 (M, Reg);
    Saved->VoteCount++;
  }
  MmioWrite32 (M, Reg, MmioRead32 (M, Reg) | 1u << Clock->VoteBit);
  // Under hardware gating CLK_OFF follows the bus, not the vote.
  if (MmioRead32 (M, Config->Gcc + Clock->Cbcr) & CBCR_HW_CTL) {
    return TRUE;
  }
  return PollClear (M, Config->Gcc + Clock->Cbcr, CBCR_CLK_OFF);
}

BOOLEAN
GamepadHwClocksOn (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN OUT GAMEPAD_SAVED    *Saved
)
{
  CONST GAMEPAD_SOC       *Soc = &gGamepadSocs[Config->Soc];
  CONST GAMEPAD_SOC_CLOCK *Se  = GamepadSocClock (Soc, Config->SeClock);
  UINT64                   Rcg;

  for (UINTN Index = 0; Index < Config->ClockCount; Index++) {
    CONST GAMEPAD_SOC_CLOCK *Clock =
      GamepadSocClock (Soc, Config->Clocks[Index]);
    if (!Clock || !Vote (M, Config, Clock, Saved)) {
      return FALSE;
    }
  }
  if (!Se || !Se->Rcg) {
    return FALSE;
  }
  Rcg = Config->Gcc + Se->Rcg;
  MmioWrite32 (M, Rcg + RCG_M, SE_RATE_M);
  MmioWrite32 (M, Rcg + RCG_N, ~(SE_RATE_N - SE_RATE_M) & RCG_MND_MASK);
  MmioWrite32 (M, Rcg + RCG_D, ~SE_RATE_N & RCG_MND_MASK);
  MmioWrite32 (
    M, Rcg + RCG_CFG,
    (MmioRead32 (M, Rcg + RCG_CFG) & ~RCG_CFG_MASK) | 1u |
      (UINT32)Se->RcgSrc << 8 | RCG_DUAL_EDGE
  );
  MmioWrite32 (M, Rcg + RCG_CMD, MmioRead32 (M, Rcg + RCG_CMD) | RCG_UPDATE);
  return PollClear (M, Rcg + RCG_CMD, RCG_UPDATE);
}

// GENI serial engine

#define QUP_HW_VER                   0x4
#define GENI_FORCE_DEFAULT_REG       0x20
#define GENI_OUTPUT_CTRL             0x24
#define GENI_CGC_CTRL                0x28
#define SE_GENI_STATUS               0x40
#define GENI_SER_M_CLK_CFG           0x48
#define GENI_SER_S_CLK_CFG           0x4c
#define GENI_FW_REVISION_RO          0x68
#define SE_GENI_CLK_SEL              0x7c
#define SE_UART_LOOPBACK_CFG         0x22c
#define SE_GENI_BYTE_GRAN            0x254
#define SE_GENI_DMA_MODE_EN          0x258
#define SE_UART_TX_TRANS_CFG         0x25c
#define SE_GENI_TX_PACKING_CFG0      0x260
#define SE_GENI_TX_PACKING_CFG1      0x264
#define SE_UART_TX_WORD_LEN          0x268
#define SE_UART_TX_STOP_BIT_LEN      0x26c
#define SE_UART_TX_TRANS_LEN         0x270
#define SE_UART_RX_TRANS_CFG         0x280
#define SE_GENI_RX_PACKING_CFG0      0x284
#define SE_GENI_RX_PACKING_CFG1      0x288
#define SE_UART_RX_WORD_LEN          0x28c
#define SE_UART_RX_STALE_CNT         0x294
#define SE_UART_TX_PARITY_CFG        0x2a4
#define SE_UART_RX_PARITY_CFG        0x2a8
#define SE_GENI_M_CMD0               0x600
#define SE_GENI_M_CMD_CTRL_REG       0x604
#define SE_GENI_M_IRQ_STATUS         0x610
#define SE_GENI_M_IRQ_CLEAR          0x618
#define SE_GENI_S_CMD0               0x630
#define SE_GENI_S_CMD_CTRL_REG       0x634
#define SE_GENI_S_IRQ_STATUS         0x640
#define SE_GENI_S_IRQ_CLEAR          0x648
#define SE_GENI_TX_FIFOn             0x700
#define SE_GENI_RX_FIFOn             0x780
#define SE_GENI_RX_FIFO_STATUS       0x804
#define SE_GENI_RX_WATERMARK_REG     0x810
#define SE_GENI_RX_RFR_WATERMARK_REG 0x814
#define SE_DMA_TX_IRQ_CLR            0xc44
#define SE_DMA_RX_IRQ_CLR            0xd44
#define SE_GSI_EVENT_EN              0xe18
#define SE_IRQ_EN                    0xe1c
#define SE_DMA_GENERAL_CFG           0xe30

#define M_GENI_CMD_ACTIVE        (1u << 0)
#define S_GENI_CMD_ACTIVE        (1u << 12)
#define M_GENI_CMD_ABORT         (1u << 1)
#define S_GENI_CMD_ABORT         (1u << 1)
#define M_CMD_DONE_EN            (1u << 0)
#define OPCODE_SHIFT             27
#define UART_START_TX            1u
#define UART_START_READ          1u
#define UART_CTS_MASK            (1u << 1)
#define SER_CLK_EN               (1u << 0)
#define CLK_DIV_SHIFT            4
#define RX_FIFO_WC_MASK          0x1ffffffu
#define RX_LAST                  (1u << 31)
#define RX_LAST_BYTE_VALID_SHIFT 28
// Four 8-bit words a FIFO entry, least significant byte first.
#define PACKING_CFG0  0x0004380Eu
#define PACKING_CFG1  0x000C3E0Eu
#define BITS_PER_CHAR 8u
// Sixteen 10-bit character times of silence close a partial RX word, as
// in Linux.
#define RX_STALE           (10u * 16u)
#define QUP_SE_VERSION_2_5 0x20050000u
#define BAUD               115200u
#define TX_FIFO_BYTES      64u

UINT32
GamepadHwUartProtocol (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config
)
{
  return (MmioRead32 (M, Config->Uart + GENI_FW_REVISION_RO) >> 8) & 0xff;
}

// Aborts both sequencers and clears their interrupts.
STATIC VOID
Abort (
  IN CONST MMIO *M,
  IN UINT64      Se
)
{
  MmioWrite32 (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  PollClear (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  MmioWrite32 (M, Se + SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);
  PollClear (M, Se + SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);
  MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
  MmioWrite32 (M, Se + SE_GENI_S_IRQ_CLEAR, 0xffffffffu);
  MmioWrite32 (M, Se + GENI_FORCE_DEFAULT_REG, 1);
}

STATIC VOID
StartRead (
  IN CONST MMIO *M,
  IN UINT64      Se
)
{
  MmioWrite32 (M, Se + SE_GENI_S_CMD0, UART_START_READ << OPCODE_SHIFT);
}

VOID
GamepadHwUartSetup (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config
)
{
  UINT64 Se       = Config->Uart;
  UINT32 Sampling = 32;

  Abort (M, Se);
  MmioWrite32 (M, Se + SE_GENI_TX_PACKING_CFG0, PACKING_CFG0);
  MmioWrite32 (M, Se + SE_GENI_TX_PACKING_CFG1, PACKING_CFG1);
  MmioWrite32 (M, Se + SE_GENI_RX_PACKING_CFG0, PACKING_CFG0);
  MmioWrite32 (M, Se + SE_GENI_RX_PACKING_CFG1, PACKING_CFG1);
  MmioWrite32 (M, Se + SE_GENI_BYTE_GRAN, 0);
  // Linux's geni_se_init, without interrupts, and FIFO mode.
  MmioWrite32 (M, Se + SE_GSI_EVENT_EN, 0);
  MmioWrite32 (M, Se + SE_DMA_TX_IRQ_CLR, 0xffffffffu);
  MmioWrite32 (M, Se + SE_DMA_RX_IRQ_CLR, 0xffffffffu);
  MmioWrite32 (
    M, Se + GENI_CGC_CTRL, MmioRead32 (M, Se + GENI_CGC_CTRL) | 0x7f
  );
  MmioWrite32 (
    M, Se + SE_DMA_GENERAL_CFG, MmioRead32 (M, Se + SE_DMA_GENERAL_CFG) | 0xf
  );
  MmioWrite32 (M, Se + GENI_OUTPUT_CTRL, 0x7f);
  MmioWrite32 (M, Se + GENI_FORCE_DEFAULT_REG, 1);
  MmioWrite32 (M, Se + SE_IRQ_EN, MmioRead32 (M, Se + SE_IRQ_EN) | 0xf);
  MmioWrite32 (
    M, Se + SE_GENI_DMA_MODE_EN, MmioRead32 (M, Se + SE_GENI_DMA_MODE_EN) & ~1u
  );
  MmioWrite32 (M, Se + SE_GENI_RX_WATERMARK_REG, 2);
  MmioWrite32 (M, Se + SE_GENI_RX_RFR_WATERMARK_REG, 14);
  // 8N1, CTS ignored.
  MmioWrite32 (M, Se + SE_UART_LOOPBACK_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_TX_TRANS_CFG, UART_CTS_MASK);
  MmioWrite32 (M, Se + SE_UART_TX_PARITY_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_RX_TRANS_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_RX_PARITY_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_TX_WORD_LEN, BITS_PER_CHAR);
  MmioWrite32 (M, Se + SE_UART_RX_WORD_LEN, BITS_PER_CHAR);
  MmioWrite32 (M, Se + SE_UART_TX_STOP_BIT_LEN, 0);
  MmioWrite32 (M, Se + SE_UART_RX_STALE_CNT, RX_STALE);
  // The serial clock comes from the RCG's first output, as Linux leaves it.
  MmioWrite32 (M, Se + SE_GENI_CLK_SEL, 0);
  // QUP 2.5 and later sample 16 times a bit, older ones 32.
  if (MmioRead32 (M, Config->Wrapper + QUP_HW_VER) >= QUP_SE_VERSION_2_5) {
    Sampling = 16;
  }
  MmioWrite32 (
    M, Se + GENI_SER_M_CLK_CFG,
    SER_CLK_EN | SE_RATE / (BAUD * Sampling) << CLK_DIV_SHIFT
  );
  MmioWrite32 (
    M, Se + GENI_SER_S_CLK_CFG,
    SER_CLK_EN | SE_RATE / (BAUD * Sampling) << CLK_DIV_SHIFT
  );
  StartRead (M, Se);
}

BOOLEAN
GamepadHwUartWrite (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN CONST UINT8          *Bytes,
  IN UINTN                 Length
)
{
  UINT64 Se = Config->Uart;

  if (Length == 0 || Length > TX_FIFO_BYTES) {
    return FALSE;
  }
  MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
  MmioWrite32 (M, Se + SE_UART_TX_TRANS_LEN, (UINT32)Length);
  MmioWrite32 (M, Se + SE_GENI_M_CMD0, UART_START_TX << OPCODE_SHIFT);
  for (UINTN At = 0; At < Length; At += 4) {
    UINT32 Word = 0;
    for (UINTN Byte = 0; Byte < 4 && At + Byte < Length; Byte++) {
      Word |= (UINT32)Bytes[At + Byte] << (8 * Byte);
    }
    MmioWrite32 (M, Se + SE_GENI_TX_FIFOn, Word);
  }
  for (UINT32 Index = 0; Index < POLLS; Index++) {
    if (MmioRead32 (M, Se + SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN) {
      MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
      return TRUE;
    }
  }
  MmioWrite32 (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  PollClear (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
  return FALSE;
}

UINTN
GamepadHwUartRead (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  OUT UINT8               *Out,
  IN UINTN                 Size
)
{
  UINT64 Se     = Config->Uart;
  UINT32 Status = MmioRead32 (M, Se + SE_GENI_RX_FIFO_STATUS);
  UINT32 Words  = Status & RX_FIFO_WC_MASK;
  UINTN  Count  = 0;

  // Clear what the receive raised (stale, overrun) so it keeps going, and
  // start it again if it stopped.
  MmioWrite32 (
    M, Se + SE_GENI_S_IRQ_CLEAR, MmioRead32 (M, Se + SE_GENI_S_IRQ_STATUS)
  );
  for (UINT32 Index = 0; Index < Words && Count + 4 <= Size; Index++) {
    UINT32 Word  = MmioRead32 (M, Se + SE_GENI_RX_FIFOn);
    UINT32 Bytes = 4;

    if (Index + 1 == Words && (Status & RX_LAST)) {
      Bytes = (Status >> RX_LAST_BYTE_VALID_SHIFT) & 7;
      if (Bytes == 0 || Bytes > 4) {
        Bytes = 4;
      }
    }
    for (UINT32 Byte = 0; Byte < Bytes; Byte++) {
      Out[Count++] = (UINT8)(Word >> (8 * Byte));
    }
  }
  if (!(MmioRead32 (M, Se + SE_GENI_STATUS) & S_GENI_CMD_ACTIVE)) {
    StartRead (M, Se);
  }
  return Count;
}

VOID
GamepadHwOff (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN CONST GAMEPAD_SAVED  *Saved,
  IN BOOLEAN               Uart
)
{
  if (Uart) {
    Abort (M, Config->Uart);
  }
  Drive (M, Config, &Config->Enable, FALSE);
  Drive (M, Config, &Config->Reset, FALSE);
  for (UINTN Index = Saved->PinCount; Index-- > 0;) {
    UINT64 Cfg = TlmmCfg (Config, Saved->Gpio[Index]);
    MmioWrite32 (M, Cfg + 4, Saved->Io[Index]);
    MmioWrite32 (M, Cfg, Saved->Cfg[Index]);
  }
  for (UINTN Index = 0; Index < Saved->VoteCount; Index++) {
    MmioWrite32 (M, Config->Gcc + Saved->VoteReg[Index], Saved->Vote[Index]);
  }
}
