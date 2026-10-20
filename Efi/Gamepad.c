/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - reads the buttons of an approved model's RSInput MCU through its
  UART.

  The serial engine is driven as mainline's qcom_geni_serial.c drives it in
  FIFO mode, polled: 8N1, no flow control. XBL loads the engine's UART
  firmware; it isn't loaded here, and an engine without it is left alone.
*/

#include "Efi/Gamepad.h"

#include "Efi/Efi.h"

#include "Core/Gamepad.h"
#include "Core/Mmio.h"
#include "Efi/Qcom.h"

// GENI serial engine registers (mainline's geni-se.h, qcom-geni-se.c and
// qcom_geni_serial.c).
#define GENI_FORCE_DEFAULT_REG     0x20
#define GENI_OUTPUT_CTRL           0x24
#define SE_GENI_CGC_CTRL           0x28
#define SE_GENI_STATUS             0x40
#define GENI_SER_M_CLK_CFG         0x48
#define GENI_SER_S_CLK_CFG         0x4c
#define GENI_FW_REVISION_RO        0x68
#define SE_GENI_CLK_SEL            0x7c
#define SE_UART_LOOPBACK_CFG       0x22c
#define SE_GENI_BYTE_GRAN          0x254
#define SE_GENI_DMA_MODE_EN        0x258
#define SE_UART_TX_TRANS_CFG       0x25c
#define SE_GENI_TX_PACKING_CFG0    0x260
#define SE_GENI_TX_PACKING_CFG1    0x264
#define SE_UART_TX_WORD_LEN        0x268
#define SE_UART_TX_STOP_BIT_LEN    0x26c
#define SE_UART_TX_TRANS_LEN       0x270
#define SE_UART_RX_TRANS_CFG       0x280
#define SE_GENI_RX_PACKING_CFG0    0x284
#define SE_GENI_RX_PACKING_CFG1    0x288
#define SE_UART_RX_WORD_LEN        0x28c
#define SE_UART_RX_STALE_CNT       0x294
#define SE_UART_TX_PARITY_CFG      0x2a4
#define SE_UART_RX_PARITY_CFG      0x2a8
#define SE_GENI_M_CMD0             0x600
#define SE_GENI_M_CMD_CTRL_REG     0x604
#define SE_GENI_M_IRQ_STATUS       0x610
#define SE_GENI_M_IRQ_CLEAR        0x618
#define SE_GENI_S_CMD0             0x630
#define SE_GENI_S_CMD_CTRL_REG     0x634
#define SE_GENI_S_IRQ_STATUS       0x640
#define SE_GENI_S_IRQ_CLEAR        0x648
#define SE_GENI_TX_FIFOn           0x700
#define SE_GENI_RX_FIFOn           0x780
#define SE_GENI_RX_FIFO_STATUS     0x804
#define SE_GENI_RX_WATERMARK_REG   0x810
#define SE_GENI_RX_RFR_WATERMARK   0x814
#define SE_DMA_TX_IRQ_CLR          0xc44
#define SE_DMA_RX_IRQ_CLR          0xd44
#define SE_GSI_EVENT_EN            0xe18
#define SE_DMA_GENERAL_CFG         0xe30
// QUPv3 wrapper.
#define QUPV3_HW_VER_REG 0x4

#define M_GENI_CMD_ACTIVE   (1u << 0)
#define S_GENI_CMD_ACTIVE   (1u << 12)
#define SER_CLK_EN          1u
#define CLK_DIV_SHIFT       4
#define CLK_DIV_MAX         0xfff
#define FW_PROTOCOL_SHIFT   8
#define GENI_SE_UART        2
#define OPCODE_SHIFT        27
#define UART_START_TX       1
#define UART_START_READ     1
#define CMD_ABORT           (1u << 1)
#define M_CMD_DONE          (1u << 0)
#define M_CMD_ABORT         (1u << 5)
#define S_CMD_DONE          (1u << 0)
#define S_CMD_ABORT         (1u << 5)
#define RX_FIFO_WC_MASK     0x1ffffff
#define RX_LAST             (1u << 31)
#define RX_LAST_BYTE_SHIFT  28
#define RX_LAST_BYTE_MASK   0x7
// Ignore CTS.
#define UART_CTS_MASK       (1u << 1)
#define DEFAULT_CGC_EN      0x7f
#define DMA_CLK_CGC_ON      0xf
#define DEFAULT_IO_OUTPUT   0x7f
#define FORCE_DEFAULT       1u
#define QUP_SE_VERSION_2_5  0x20050000u
// Ten bit times, as the Linux driver sets.
#define RX_STALE            (10 * 16)

// RCG registers (mainline's clk-rcg2.c). With DFS on, the engine picks one
// of eight levels through SE_GENI_CLK_SEL.
#define RCG_CMD             0x0
#define RCG_CMD_UPDATE      1u
#define RCG_CFG             0x4
#define RCG_M               0x8
#define RCG_N               0xc
#define RCG_D               0x10
// CFG: pre-divider (2 x divider - 1), source, M/N mode, hardware control.
#define RCG_PRE_DIV_1       1u
#define RCG_SRC_MASK        (7u << 8)
#define RCG_SRC_GPLL0_EVEN  (6u << 8)
#define RCG_MODE_MASK       (3u << 12)
#define RCG_MODE_DUAL_EDGE  (2u << 12)
#define RCG_HW_CLK_CTRL     (1u << 20)
#define RCG_CMD_DFSR        0x14
#define RCG_PERF_DFSR(L)    (0x1c + 4 * (L))
#define RCG_PERF_M_DFSR(L)  (0x5c + 4 * (L))
#define RCG_PERF_N_DFSR(L)  (0x9c + 4 * (L))
#define RCG_DFS_LEVELS      8
#define RCG_MND_MASK        0xffff
#define RCG_HID_MASK        0x1f
// Branch CBCR: hardware gating (as hwcg_bit in gcc-sm8550.c), the clock's
// off status, and the NOC FSM state (2 is on).
#define BRANCH_HW_CTL        (1u << 1)
#define BRANCH_CLK_OFF       (1u << 31)
#define BRANCH_NOC_FSM_SHIFT 28
#define BRANCH_NOC_FSM_ON    2

// TLMM: one 4 KiB block per GPIO, its control register then its I/O
// register.
#define TLMM_CTL(G)         ((UINT64)(G) * 0x1000)
#define TLMM_IO(G)          ((UINT64)(G) * 0x1000 + 4)
// Pull (bits 0-1), function (2-5), drive strength (6-8) and output enable
// (9). Other bits are kept.
#define TLMM_CONFIG_MASK    0x3ffu
#define TLMM_PULL_DOWN      1u
#define TLMM_PULL_UP        3u
#define TLMM_FUNCTION_SHIFT 2
#define TLMM_OE             (1u << 9)
#define TLMM_OUT            (1u << 1)

#define BAUD 115200
// What Linux sets a non-DFS QUP clock to for 115200 baud.
#define UART_CLOCK_HZ 7372800
#define UART_CLOCK_M  384u
#define UART_CLOCK_N  15625u
// Tenths of a percent.
#define MAX_BAUD_ERROR 20
// After power-on, the version request, then the parameters, 100 ms apart.
#define MCU_START_MS 100
// A held button counts as released after this long without a status frame.
#define STATUS_STALE_MS 500
#define WAIT_US         20000

typedef struct _SAVED_PIN {
  UINT16 Gpio;
  UINT32 Ctl, Io;
} SAVED_PIN;

typedef struct _SAVED_RCG {
  BOOLEAN Saved;
  UINT32  Cfg, M, N, D;
} SAVED_RCG;

typedef struct _GAMEPAD {
  BOOLEAN              Open;
  CONST GAMEPAD_BOARD *Board;
  LOG_SINK             Log;
  EFI_EVENT            ExitEvent;
  // Vote bits that were off before opening.
  UINT32               VotedOn;
  // The engine clock's configuration, if it was changed.
  SAVED_RCG            Rcg;
  SAVED_PIN            Pins[4];
  UINTN                PinCount;
  GAMEPAD_PARSER       Parser;
  UINT8                Sequence;
  // Commands sent so far: 0, 1 (version request) or 2 (parameters).
  UINT8                Sent;
  BOOLEAN              VersionLogged;
  UINT64               PoweredAt;
  UINT64               LastStatus;
  UINT16               Buttons;
} GAMEPAD;

STATIC GAMEPAD mPad;

STATIC UINT32
Read (IN UINT64 Addr)
{
  return MmioRead32 (&gMmioHardware, Addr);
}

STATIC VOID
Write (
  IN UINT64 Addr,
  IN UINT32 Value
)
{
  MmioWrite32 (&gMmioHardware, Addr, Value);
}

// Waits up to WAIT_US for (Read (Addr) & Mask) == Want. It only reads the
// counter, so it also works at ExitBootServices.
STATIC BOOLEAN
WaitFor (
  IN UINT64 Addr,
  IN UINT32 Mask,
  IN UINT32 Want
)
{
  UINT64 Until = ClockNowUs () + WAIT_US;

  for (;;) {
    if ((Read (Addr) & Mask) == Want) {
      return TRUE;
    }
    if (ClockNowUs () >= Until) {
      return FALSE;
    }
  }
}

// Clocks

// Waits for the branch at Cbcr to run, as clk_branch2's enable does. A
// branch in hardware gating mode is off while idle, so it isn't waited for.
STATIC BOOLEAN
BranchOn (IN UINT64 Cbcr)
{
  UINT64 Until = ClockNowUs () + WAIT_US;

  for (;;) {
    UINT32 Value = Read (Cbcr);

    if ((Value & BRANCH_HW_CTL) || !(Value & BRANCH_CLK_OFF) ||
        ((Value >> BRANCH_NOC_FSM_SHIFT) & 7) == BRANCH_NOC_FSM_ON) {
      return TRUE;
    }
    if (ClockNowUs () >= Until) {
      LogPrint (
        mPad.Log, "gamepad: clock branch 0x%llx stays off (0x%08x)",
        Cbcr, Value
      );
      return FALSE;
    }
  }
}

// Turns on the vote bits that were off and waits for every branch.
STATIC BOOLEAN
ClocksOn (IN CONST GAMEPAD_BOARD *B)
{
  UINT64 Vote = B->Gcc + B->ClockVote;
  UINT32 Was  = Read (Vote);

  mPad.VotedOn = B->ClockVoteBits & ~Was;
  Write (Vote, Was | B->ClockVoteBits);
  for (UINTN Index = 0;
       Index < sizeof (B->ClockBranches) / sizeof (*B->ClockBranches);
       Index++) {
    if (!BranchOn (B->Gcc + B->ClockBranches[Index])) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC VOID
ClocksRestore (IN CONST GAMEPAD_BOARD *B)
{
  UINT64 Vote = B->Gcc + B->ClockVote;

  Write (Vote, Read (Vote) & ~mPad.VotedOn);
  mPad.VotedOn = 0;
}

// The rate of the RCG with config Cfg and raw M and N registers, in Hz, or
// 0 for a source other than XO or GPLL0.
STATIC UINT64
RcgRate (
  IN UINT32 Cfg,
  IN UINT32 M,
  IN UINT32 NotNMinusM
)
{
  UINT32 Hid  = Cfg & RCG_HID_MASK;
  UINT32 Mode = (Cfg >> 12) & 3;
  UINT64 Rate;
  UINT32 N;

  // The sources in gcc-sm8550.c's parent map 0.
  switch ((Cfg >> 8) & 7) {
  case 0:
    Rate = 19200000;
    break;
  case 1:
    Rate = 600000000;
    break;
  case 6:
    Rate = 300000000;
    break;
  default:
    return 0;
  }
  if (Hid) {
    Rate = Rate * 2 / (Hid + 1);
  }
  if (Mode) {
    M &= RCG_MND_MASK;
    N  = ((~NotNMinusM) & RCG_MND_MASK) + M;
    if (!N) {
      return 0;
    }
    Rate = Rate * M / N;
  }
  return Rate;
}

// The rate of the RCG's current configuration, or of DFS level Level.
STATIC UINT64
LevelRate (
  IN UINT64  Rcg,
  IN BOOLEAN Dfs,
  IN UINTN   Level
)
{
  if (!Dfs) {
    return RcgRate (
      Read (Rcg + RCG_CFG), Read (Rcg + RCG_M), Read (Rcg + RCG_N)
    );
  }
  return RcgRate (
    Read (Rcg + RCG_PERF_DFSR (Level)), Read (Rcg + RCG_PERF_M_DFSR (Level)),
    Read (Rcg + RCG_PERF_N_DFSR (Level))
  );
}

// Sets the RCG to UART_CLOCK_HZ as clk_rcg2_configure does with the
// 7372800 Hz row of gcc-sm8550.c's QUP table: GPLL0 even, M/N 384/15625,
// D at N. The old configuration is kept for RcgRestore.
STATIC BOOLEAN
RcgSetUartClock (IN UINT64 Rcg)
{
  UINT32 Cfg = Read (Rcg + RCG_CFG);

  mPad.Rcg      = (SAVED_RCG){TRUE, Cfg, Read (Rcg + RCG_M),
                              Read (Rcg + RCG_N), Read (Rcg + RCG_D)};
  Write (Rcg + RCG_M, (mPad.Rcg.M & ~RCG_MND_MASK) | UART_CLOCK_M);
  Write (
    Rcg + RCG_N,
    (mPad.Rcg.N & ~RCG_MND_MASK) | (~(UART_CLOCK_N - UART_CLOCK_M) & RCG_MND_MASK)
  );
  Write (
    Rcg + RCG_D, (mPad.Rcg.D & ~RCG_MND_MASK) | (~UART_CLOCK_N & RCG_MND_MASK)
  );
  Cfg &= ~(RCG_HID_MASK | RCG_SRC_MASK | RCG_MODE_MASK | RCG_HW_CLK_CTRL);
  Cfg |= RCG_PRE_DIV_1 | RCG_SRC_GPLL0_EVEN | RCG_MODE_DUAL_EDGE;
  Write (Rcg + RCG_CFG, Cfg);
  Write (Rcg + RCG_CMD, Read (Rcg + RCG_CMD) | RCG_CMD_UPDATE);
  return WaitFor (Rcg + RCG_CMD, RCG_CMD_UPDATE, 0);
}

STATIC VOID
RcgRestore (IN CONST GAMEPAD_BOARD *B)
{
  UINT64 Rcg = B->Gcc + B->SeRcg;

  if (!mPad.Rcg.Saved) {
    return;
  }
  Write (Rcg + RCG_M, mPad.Rcg.M);
  Write (Rcg + RCG_N, mPad.Rcg.N);
  Write (Rcg + RCG_D, mPad.Rcg.D);
  Write (Rcg + RCG_CFG, mPad.Rcg.Cfg);
  Write (Rcg + RCG_CMD, Read (Rcg + RCG_CMD) | RCG_CMD_UPDATE);
  (VOID)WaitFor (Rcg + RCG_CMD, RCG_CMD_UPDATE, 0);
  mPad.Rcg.Saved = FALSE;
}

// The baud rate error, in tenths of a percent, of Rate / (Target / BAUD)
// divided by the nearest divider. Sets *Divider to it.
STATIC UINT64
BaudError (
  IN UINT64  Rate,
  IN UINT64  Target,
  OUT UINT32 *Divider
)
{
  UINT64 Div  = (Rate + Target / 2) / Target;
  UINT64 Got;

  Div      = Div ? Div : 1;
  *Divider = (UINT32)Div;
  Got      = Rate / Div;
  return (Got > Target ? Got - Target : Target - Got) * 1000 / Target;
}

// Picks the engine clock level and divider for BAUD with Oversampling, as
// geni_serial_set_rate does. A rate the baud divides exactly is taken
// first. Without DFS and without one, the RCG is set to UART_CLOCK_HZ, as
// Linux's set_rate would. With DFS, the nearest level within
// MAX_BAUD_ERROR is taken.
STATIC BOOLEAN
PickRate (
  IN CONST GAMEPAD_BOARD *B,
  IN UINT32               Oversampling,
  OUT UINT32             *Level,
  OUT UINT32             *Divider
)
{
  UINT64  Rcg    = B->Gcc + B->SeRcg;
  UINT64  Target = (UINT64)BAUD * Oversampling;
  BOOLEAN Dfs    = (Read (Rcg + RCG_CMD_DFSR) & 1) != 0;
  UINTN   Levels = Dfs ? RCG_DFS_LEVELS : 1;
  UINT64  BestError = MAX_UINT64;
  UINT32  BestLevel = 0;
  UINT32  BestDivider = 1;

  for (UINTN Index = 0; Index < Levels; Index++) {
    UINT64 Rate = LevelRate (Rcg, Dfs, Index);
    UINT32 Div;
    UINT64 Error;

    if (!Rate || Rate < Target) {
      continue;
    }
    Error = BaudError (Rate, Target, &Div);
    if (Rate % Target == 0 && Div <= CLK_DIV_MAX) {
      LogPrint (
        mPad.Log, "gamepad: %llu Hz (level %u) for %u baud", Rate,
        (UINT32)Index, BAUD
      );
      *Level   = (UINT32)Index;
      *Divider = Div;
      return TRUE;
    }
    if (Error < BestError && Div <= CLK_DIV_MAX) {
      BestError   = Error;
      BestLevel   = (UINT32)Index;
      BestDivider = Div;
    }
  }
  if (!Dfs) {
    LogPrint (
      mPad.Log, "gamepad: the engine runs at %llu Hz; setting %u Hz",
      LevelRate (Rcg, FALSE, 0), UART_CLOCK_HZ
    );
    if (!RcgSetUartClock (Rcg)) {
      LogPrint (mPad.Log, "gamepad: the engine clock didn't update");
      return FALSE;
    }
    *Level   = 0;
    *Divider = (UINT32)(UART_CLOCK_HZ / Target);
    return TRUE;
  }
  if (BestError <= MAX_BAUD_ERROR) {
    LogPrint (
      mPad.Log, "gamepad: %llu Hz (level %u, %llu.%llu%% off) for %u baud",
      LevelRate (Rcg, TRUE, BestLevel), BestLevel, BestError / 10,
      BestError % 10, BAUD
    );
    *Level   = BestLevel;
    *Divider = BestDivider;
    return TRUE;
  }
  for (UINTN Index = 0; Index < Levels; Index++) {
    LogPrint (
      mPad.Log, "gamepad: level %u: %llu Hz (cfg 0x%x)", (UINT32)Index,
      LevelRate (Rcg, TRUE, Index), Read (Rcg + RCG_PERF_DFSR (Index))
    );
  }
  return FALSE;
}

// Pins

STATIC VOID
SavePin (
  IN CONST GAMEPAD_BOARD *B,
  IN UINT16               Gpio
)
{
  mPad.Pins[mPad.PinCount++] = (SAVED_PIN){
    Gpio, Read (B->Tlmm + TLMM_CTL (Gpio)), Read (B->Tlmm + TLMM_IO (Gpio))};
}

STATIC VOID
PinsRestore (IN CONST GAMEPAD_BOARD *B)
{
  // The last changed first: the MCU's enable before its supply.
  while (mPad.PinCount) {
    SAVED_PIN Pin = mPad.Pins[--mPad.PinCount];

    Write (B->Tlmm + TLMM_IO (Pin.Gpio), Pin.Io);
    Write (B->Tlmm + TLMM_CTL (Pin.Gpio), Pin.Ctl);
  }
}

// Saves Gpio, sets its output to High if it will be an output, then sets
// its configuration bits to Config.
STATIC VOID
ConfigPin (
  IN CONST GAMEPAD_BOARD *B,
  IN UINT16               Gpio,
  IN UINT32               Config,
  IN BOOLEAN              High
)
{
  SavePin (B, Gpio);
  if (High) {
    Write (B->Tlmm + TLMM_IO (Gpio), TLMM_OUT);
  }
  Write (
    B->Tlmm + TLMM_CTL (Gpio),
    (mPad.Pins[mPad.PinCount - 1].Ctl & ~TLMM_CONFIG_MASK) | Config
  );
}

// The UART pins as in the devicetree: 2 mA, pulled up. Then the MCU's power
// lines as outputs driven high, pulled down as its enable is.
STATIC VOID
PinsOn (IN CONST GAMEPAD_BOARD *B)
{
  UINT32 Uart = (UINT32)B->UartFunction << TLMM_FUNCTION_SHIFT | TLMM_PULL_UP;

  ConfigPin (B, B->TxGpio, Uart, FALSE);
  ConfigPin (B, B->RxGpio, Uart, FALSE);
  for (UINTN Index = 0;
       Index < sizeof (B->PowerGpios) / sizeof (*B->PowerGpios); Index++) {
    ConfigPin (B, B->PowerGpios[Index], TLMM_OE | TLMM_PULL_DOWN, TRUE);
  }
}

// Serial engine

STATIC VOID
AbortTx (IN UINT64 Se)
{
  if (!(Read (Se + SE_GENI_STATUS) & M_GENI_CMD_ACTIVE)) {
    return;
  }
  Write (Se + SE_GENI_M_CMD_CTRL_REG, CMD_ABORT);
  (VOID)WaitFor (Se + SE_GENI_M_IRQ_STATUS, M_CMD_ABORT, M_CMD_ABORT);
  Write (Se + SE_GENI_M_IRQ_CLEAR, M_CMD_ABORT | M_CMD_DONE);
}

// qcom_geni_serial_abort_rx.
STATIC VOID
AbortRx (IN UINT64 Se)
{
  Write (Se + SE_GENI_S_CMD_CTRL_REG, CMD_ABORT);
  (VOID)WaitFor (Se + SE_GENI_S_CMD_CTRL_REG, CMD_ABORT, 0);
  Write (Se + SE_GENI_S_IRQ_CLEAR, S_CMD_DONE | S_CMD_ABORT);
  Write (Se + GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
}

STATIC VOID
StartRx (IN UINT64 Se)
{
  Write (Se + SE_GENI_S_CMD0, (UINT32)UART_START_READ << OPCODE_SHIFT);
}

// geni_se_config_packing for 8-bit words, four to a FIFO word, LSB first,
// both ways. Each vector is start << 5 | length - 1 << 1, the last with the
// stop bit.
STATIC VOID
Packing (IN UINT64 Se)
{
  UINT32 Vector[4];

  for (UINT32 Index = 0; Index < 4; Index++) {
    Vector[Index] = Index * 8 << 5 | 7 << 1;
  }
  Vector[3] |= 1;
  Write (Se + SE_GENI_TX_PACKING_CFG0, Vector[0] | Vector[1] << 10);
  Write (Se + SE_GENI_TX_PACKING_CFG1, Vector[2] | Vector[3] << 10);
  Write (Se + SE_GENI_RX_PACKING_CFG0, Vector[0] | Vector[1] << 10);
  Write (Se + SE_GENI_RX_PACKING_CFG1, Vector[2] | Vector[3] << 10);
  Write (Se + SE_GENI_BYTE_GRAN, 0);
}

// What qcom_geni_serial's earlycon setup, port setup and set_termios do for
// 115200 8N1 without flow control, in FIFO mode, without interrupts.
STATIC VOID
SetupUart (
  IN UINT64 Se,
  IN UINT32 Level,
  IN UINT32 Divider
)
{
  UINT32 ClockCfg = Divider << CLK_DIV_SHIFT | SER_CLK_EN;

  AbortTx (Se);
  AbortRx (Se);
  Packing (Se);
  Write (Se + SE_GSI_EVENT_EN, 0);
  Write (Se + SE_GENI_M_IRQ_CLEAR, 0xffffffff);
  Write (Se + SE_GENI_S_IRQ_CLEAR, 0xffffffff);
  Write (Se + SE_DMA_TX_IRQ_CLR, 0xffffffff);
  Write (Se + SE_DMA_RX_IRQ_CLR, 0xffffffff);
  Write (Se + SE_GENI_CGC_CTRL, Read (Se + SE_GENI_CGC_CTRL) | DEFAULT_CGC_EN);
  Write (
    Se + SE_DMA_GENERAL_CFG, Read (Se + SE_DMA_GENERAL_CFG) | DMA_CLK_CGC_ON
  );
  Write (Se + GENI_OUTPUT_CTRL, DEFAULT_IO_OUTPUT);
  Write (Se + GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
  Write (Se + SE_GENI_DMA_MODE_EN, Read (Se + SE_GENI_DMA_MODE_EN) & ~1u);
  Write (Se + SE_GENI_RX_WATERMARK_REG, 8);
  Write (Se + SE_GENI_RX_RFR_WATERMARK, 14);
  Write (Se + SE_UART_RX_STALE_CNT, RX_STALE);
  Write (Se + SE_UART_LOOPBACK_CFG, 0);
  Write (Se + SE_UART_TX_TRANS_CFG, UART_CTS_MASK);
  Write (Se + SE_UART_TX_PARITY_CFG, 0);
  Write (Se + SE_UART_RX_TRANS_CFG, 0);
  Write (Se + SE_UART_RX_PARITY_CFG, 0);
  Write (Se + SE_UART_TX_WORD_LEN, 8);
  Write (Se + SE_UART_RX_WORD_LEN, 8);
  Write (Se + SE_UART_TX_STOP_BIT_LEN, 0);
  Write (Se + GENI_SER_M_CLK_CFG, ClockCfg);
  Write (Se + GENI_SER_S_CLK_CFG, ClockCfg);
  Write (Se + SE_GENI_CLK_SEL, Level);
  StartRx (Se);
}

// Sends a COMMOD frame and waits for it to go out.
STATIC VOID
Send (
  IN CONST UINT8 *Data,
  IN UINT16       Len
)
{
  UINT64 Se = mPad.Board->Se;
  UINT8  Frame[GAMEPAD_FRAME_OVERHEAD + 16];
  UINTN  Total;

  Total = GamepadFrameBuild (
    mPad.Sequence++, GAMEPAD_CMD_COMMOD, Data, Len, Frame
  );
  AbortTx (Se);
  Write (Se + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE);
  Write (Se + SE_UART_TX_TRANS_LEN, (UINT32)Total);
  Write (Se + SE_GENI_M_CMD0, (UINT32)UART_START_TX << OPCODE_SHIFT);
  for (UINTN Index = 0; Index < Total; Index += 4) {
    UINT32 Word = 0;

    for (UINTN Byte = 0; Byte < 4 && Index + Byte < Total; Byte++) {
      Word |= (UINT32)Frame[Index + Byte] << (8 * Byte);
    }
    Write (Se + SE_GENI_TX_FIFOn, Word);
  }
  if (!WaitFor (Se + SE_GENI_M_IRQ_STATUS, M_CMD_DONE, M_CMD_DONE)) {
    LogPrint (mPad.Log, "gamepad: command 0x%02x wasn't sent", Data[0]);
    AbortTx (Se);
  }
  Write (Se + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE);
}

STATIC VOID EFIAPI
AtExitBootServices (
  IN EFI_EVENT Event,
  IN VOID     *Context
)
{
  (VOID)Event;
  (VOID)Context;
  // The event is closed with the boot services.
  mPad.ExitEvent = NULL;
  GamepadClose ();
}

// Undoes a partial open.
STATIC VOID
Undo (VOID)
{
  // While the clock runs, so the RCG takes the update.
  RcgRestore (mPad.Board);
  PinsRestore (mPad.Board);
  ClocksRestore (mPad.Board);
}

BOOLEAN
GamepadOpen (
  IN CONST CHAR8 *Model,
  IN LOG_SINK     Log
)
{
  CONST GAMEPAD_BOARD *B = GamepadBoardForModel (Model);
  QCOM_CHIP            Chip;
  UINT32               Id;
  UINT32               Proto;
  UINT32               Level;
  UINT32               Divider;
  BOOLEAN              Known = FALSE;

  if (mPad.Open || !B) {
    return mPad.Open;
  }
  Id = QcomChip (&Chip) && Chip.Id.Status == EFI_SUCCESS ? Chip.Id.Value : 0;
  for (UINTN Index = 0; Index < sizeof (B->ChipIds) / sizeof (*B->ChipIds);
       Index++) {
    Known = Known || (Id && Id == B->ChipIds[Index]);
  }
  if (!Known) {
    LogPrint (Log, "gamepad: %s on an unexpected chip (%u)", B->Model, Id);
    return FALSE;
  }
  mPad = (GAMEPAD){.Board = B, .Log = Log};
  if (!ClocksOn (B)) {
    Undo ();
    return FALSE;
  }
  Proto = (Read (B->Se + GENI_FW_REVISION_RO) >> FW_PROTOCOL_SHIFT) & 0xff;
  if (Proto != GENI_SE_UART) {
    LogPrint (Log, "gamepad: the serial engine isn't a UART (%u)", Proto);
    Undo ();
    return FALSE;
  }
  // Engines from QUP 2.5 sample 16 times a bit, older ones 32.
  if (!PickRate (
        B, Read (B->Wrapper + QUPV3_HW_VER_REG) >= QUP_SE_VERSION_2_5 ? 16 : 32,
        &Level, &Divider
      )) {
    LogPrint (Log, "gamepad: no clock rate fits %u baud", BAUD);
    Undo ();
    return FALSE;
  }
  PinsOn (B);
  SetupUart (B->Se, Level, Divider);
  mPad.PoweredAt = ClockNowMs ();
  mPad.Open      = TRUE;
  if (EFI_ERROR (gBS->CreateEvent (
        EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK, AtExitBootServices, NULL,
        &mPad.ExitEvent
      ))) {
    // The OS would find the MCU powered and the engine reading.
    LogPrint (Log, "gamepad: no ExitBootServices event; not using it");
    GamepadClose ();
    return FALSE;
  }
  LogPrint (Log, "gamepad: %s, waiting for the MCU", B->Model);
  return TRUE;
}

// Feeds what the RX FIFO holds to the parser.
STATIC VOID
Receive (IN UINT64 Now)
{
  UINT64 Se     = mPad.Board->Se;
  UINT32 Status = Read (Se + SE_GENI_RX_FIFO_STATUS);
  UINT32 Words  = Status & RX_FIFO_WC_MASK;

  for (UINT32 Index = 0; Index < Words; Index++) {
    UINT32        Word  = Read (Se + SE_GENI_RX_FIFOn);
    UINT32        Bytes = 4;
    GAMEPAD_FRAME Frame;
    UINT16        Buttons;

    if (Index + 1 == Words && (Status & RX_LAST)) {
      Bytes = (Status >> RX_LAST_BYTE_SHIFT) & RX_LAST_BYTE_MASK;
      Bytes = Bytes ? Bytes : 4;
    }
    for (UINT32 Byte = 0; Byte < Bytes; Byte++) {
      if (!GamepadParse (&mPad.Parser, (UINT8)(Word >> (8 * Byte)), &Frame)) {
        continue;
      }
      if (GamepadButtons (&Frame, &Buttons)) {
        mPad.Buttons    = Buttons;
        mPad.LastStatus = Now;
      } else if (Frame.Cmd == GAMEPAD_CMD_COMMOD && Frame.Len &&
                 Frame.Data[0] == GAMEPAD_COMMOD_VERSION &&
                 !mPad.VersionLogged) {
        // The driver prints the payload, sub-command byte included.
        CHAR8 Version[GAMEPAD_PAYLOAD_MAX + 1];
        UINTN Length = 0;

        for (UINTN Char = 1; Char < Frame.Len; Char++) {
          UINT8 C = Frame.Data[Char];
          if (C >= 0x20 && C < 0x7f) {
            Version[Length++] = (CHAR8)C;
          }
        }
        Version[Length]    = 0;
        mPad.VersionLogged = TRUE;
        LogPrint (mPad.Log, "gamepad: MCU version \"%s\"", Version);
      }
    }
  }
}

BOOLEAN
GamepadPoll (
  IN UINT64   Now,
  OUT UINT16 *Buttons
)
{
  UINT64 Se;

  if (!mPad.Open) {
    return FALSE;
  }
  Se = mPad.Board->Se;
  if (mPad.Sent == 0 && Now >= mPad.PoweredAt + MCU_START_MS) {
    Send (gGamepadVersionRequest, sizeof (gGamepadVersionRequest));
    mPad.Sent = 1;
  } else if (mPad.Sent == 1 && Now >= mPad.PoweredAt + 2 * MCU_START_MS) {
    Send (gGamepadSetParameters, sizeof (gGamepadSetParameters));
    mPad.Sent = 2;
  }
  Receive (Now);
  // An error or a break ends the read; clear it and read on.
  Write (Se + SE_GENI_S_IRQ_CLEAR, Read (Se + SE_GENI_S_IRQ_STATUS));
  if (!(Read (Se + SE_GENI_STATUS) & S_GENI_CMD_ACTIVE)) {
    StartRx (Se);
  }
  if (mPad.Buttons && Now - mPad.LastStatus >= STATUS_STALE_MS) {
    mPad.Buttons = 0;
  }
  *Buttons = mPad.Buttons;
  return TRUE;
}

VOID
GamepadClose (VOID)
{
  if (!mPad.Open) {
    return;
  }
  mPad.Open = FALSE;
  if (mPad.ExitEvent) {
    gBS->CloseEvent (mPad.ExitEvent);
    mPad.ExitEvent = NULL;
  }
  AbortTx (mPad.Board->Se);
  AbortRx (mPad.Board->Se);
  Undo ();
}
