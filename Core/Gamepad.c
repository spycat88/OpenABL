/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - the built-in controls of handhelds whose buttons sit behind an
  RSInput microcontroller on a UART, and the models known to have one.
*/

#include "Core/Gamepad.h"

#include "Base/Text.h"

STATIC CONST UINT8 mHeader[] = {0xa5, 0xd3, 0x5a, 0x3d};

#define POS_SEQ   4
#define POS_CMD   5
#define POS_LEN_L 6
#define POS_LEN_H 7
#define POS_DATA  8

// XOR of Frame[POS_SEQ] up to, not including, Frame[Len - 1].
STATIC UINT8
Checksum (
  IN CONST UINT8 *Frame,
  IN UINTN        Len
)
{
  UINT8 Sum = 0;

  for (UINTN Index = POS_SEQ; Index + 1 < Len; Index++) {
    Sum ^= Frame[Index];
  }
  return Sum;
}

BOOLEAN
GamepadParse (
  IN OUT GAMEPAD_PARSER *Parser,
  IN UINT8               Byte,
  OUT GAMEPAD_FRAME     *Out
)
{
  UINTN Payload;
  UINTN Total;

  if (Parser->Len < sizeof (mHeader)) {
    if (Byte == mHeader[Parser->Len]) {
      Parser->Buf[Parser->Len++] = Byte;
    } else {
      // A lost header byte may start the next frame.
      Parser->Len = Byte == mHeader[0] ? 1 : 0;
      Parser->Buf[0] = Byte;
    }
    return FALSE;
  }
  Parser->Buf[Parser->Len++] = Byte;
  if (Parser->Len <= POS_LEN_H) {
    return FALSE;
  }
  Payload = Parser->Buf[POS_LEN_L] | (UINTN)Parser->Buf[POS_LEN_H] << 8;
  if (Payload > GAMEPAD_PAYLOAD_MAX) {
    Parser->Len = 0;
    return FALSE;
  }
  Total = GAMEPAD_FRAME_OVERHEAD + Payload;
  if (Parser->Len < Total) {
    return FALSE;
  }
  Parser->Len = 0;
  if (Checksum (Parser->Buf, Total) != Parser->Buf[Total - 1]) {
    return FALSE;
  }
  Out->Cmd = Parser->Buf[POS_CMD];
  Out->Len = (UINT16)Payload;
  for (UINTN Index = 0; Index < Payload; Index++) {
    Out->Data[Index] = Parser->Buf[POS_DATA + Index];
  }
  return TRUE;
}

BOOLEAN
GamepadButtons (
  IN CONST GAMEPAD_FRAME *Frame,
  OUT UINT16             *Buttons
)
{
  if (Frame->Cmd != GAMEPAD_CMD_STATUS || Frame->Len < 6) {
    return FALSE;
  }
  *Buttons =
    (UINT16)((Frame->Data[0] | Frame->Data[1] << 8) & GAMEPAD_BUTTONS);
  return TRUE;
}

UINTN
GamepadFrameBuild (
  IN UINT8        Sequence,
  IN UINT8        Cmd,
  IN CONST UINT8 *Data,
  IN UINT16       Len,
  OUT UINT8      *Out
)
{
  UINTN Total = GAMEPAD_FRAME_OVERHEAD + Len;

  for (UINTN Index = 0; Index < sizeof (mHeader); Index++) {
    Out[Index] = mHeader[Index];
  }
  Out[POS_SEQ]   = Sequence;
  Out[POS_CMD]   = Cmd;
  Out[POS_LEN_L] = (UINT8)Len;
  Out[POS_LEN_H] = (UINT8)(Len >> 8);
  for (UINTN Index = 0; Index < Len; Index++) {
    Out[POS_DATA + Index] = Data[Index];
  }
  Out[Total - 1] = Checksum (Out, Total);
  return Total;
}

CONST UINT8 gGamepadVersionRequest[1] = {GAMEPAD_COMMOD_VERSION};
CONST UINT8 gGamepadSetParameters[10] = {
  GAMEPAD_COMMOD_SET_PAR, 0x01, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x07,
};

// SM8550 (519), QCS8550 (603) and QCM8550 (604): uart15 is QUPv3 wrapper 2's
// serial engine 7 (0x89c000), clocked by gcc_qupv3_wrap2_s7_clk, on TLMM 74
// (TX) and 75 (RX) in function qup2_se7. Offsets are mainline's
// gcc-sm8550.c: the vote bits are wrap2's core (0), S-AHB (1), M-AHB (2),
// core 2x (3) and S7 (17) clocks.
#define SM8550_UART15                                                          \
  .ChipIds = {519, 603, 604}, .Gcc = 0x100000, .ClockVote = 0x52010,           \
  .ClockVoteBits = 1u << 0 | 1u << 1 | 1u << 2 | 1u << 3 | 1u << 17,          \
  .ClockBranches = {0x233d4, 0x233d0, 0x233cc, 0x23004, 0x1e88c},             \
  .SeRcg = 0x1e898, .Wrapper = 0x8c0000, .Se = 0x89c000, .Tlmm = 0xf100000,    \
  .TxGpio = 74, .RxGpio = 75, .UartFunction = 2

CONST GAMEPAD_BOARD gGamepadBoards[] = {
  // The RP6 board's devicetree: the MCU's vdd_mcu_3v3 is enabled by TLMM 99
  // and the MCU by TLMM 12. Its vdd (BOB2) also feeds UFS, so XBL has it on.
  {.Model = "Retroid Pocket Nova", SM8550_UART15, .PowerGpios = {99, 12}},
};

CONST UINTN gGamepadBoardsCount =
  sizeof (gGamepadBoards) / sizeof (*gGamepadBoards);

CONST GAMEPAD_BOARD *
GamepadBoardForModel (IN CONST CHAR8 *Model)
{
  OABL_TEXT Wanted = TextOf (Model);

  for (UINTN Index = 0; Index < gGamepadBoardsCount; Index++) {
    if (TextEqNoCase (Wanted, gGamepadBoards[Index].Model)) {
      return &gGamepadBoards[Index];
    }
  }
  return NULL;
}
