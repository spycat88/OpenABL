/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - the built-in controls of handhelds whose buttons sit behind an
  RSInput microcontroller on a UART, and the models known to have one.

  The MCU's protocol is from the Linux driver for it (rsinput.c, Teguh
  Sobirin). Frames are A5 D3 5A 3D, a sequence number, a command, a 16-bit
  little-endian payload length, the payload, and a checksum: the XOR of
  every byte from the sequence number to the end of the payload. Only the
  buttons are read; the sticks and triggers in a status frame are ignored.
*/

#ifndef __CORE_GAMEPAD_H__
#define __CORE_GAMEPAD_H__

#include "Base/OablBase.h"

// Button bits, as in the first two bytes of a status payload. The four face
// buttons are named by position. The shoulder buttons, the stick clicks,
// Home and Back are bits 8, 9 and 12 to 15, not read.
#define GAMEPAD_UP     0x0001
#define GAMEPAD_DOWN   0x0002
#define GAMEPAD_LEFT   0x0004
#define GAMEPAD_RIGHT  0x0008
#define GAMEPAD_NORTH  0x0010
#define GAMEPAD_WEST   0x0020
#define GAMEPAD_EAST   0x0040
#define GAMEPAD_SOUTH  0x0080
#define GAMEPAD_SELECT 0x0400
#define GAMEPAD_START  0x0800
#define GAMEPAD_BUTTONS                                                        \
  (GAMEPAD_UP | GAMEPAD_DOWN | GAMEPAD_LEFT | GAMEPAD_RIGHT | GAMEPAD_NORTH |  \
   GAMEPAD_WEST | GAMEPAD_EAST | GAMEPAD_SOUTH | GAMEPAD_SELECT |              \
   GAMEPAD_START)

#define GAMEPAD_CMD_COMMOD 0x01
#define GAMEPAD_CMD_STATUS 0x02
// The first payload byte of a COMMOD frame.
#define GAMEPAD_COMMOD_VERSION 0x02
#define GAMEPAD_COMMOD_SET_PAR 0x05

// Header, sequence, command, length and checksum.
#define GAMEPAD_FRAME_OVERHEAD 9
// Longer payloads are taken for noise. A status payload is 14 bytes.
#define GAMEPAD_PAYLOAD_MAX 64

typedef struct _GAMEPAD_FRAME {
  UINT8  Cmd;
  UINT16 Len;
  UINT8  Data[GAMEPAD_PAYLOAD_MAX];
} GAMEPAD_FRAME;

// Collects bytes into frames. Zero it to start.
typedef struct _GAMEPAD_PARSER {
  UINT8 Buf[GAMEPAD_FRAME_OVERHEAD + GAMEPAD_PAYLOAD_MAX];
  UINTN Len;
} GAMEPAD_PARSER;

// Takes the next byte from the UART. Returns TRUE with the frame in Out when
// Byte completes one whose checksum is right. Bytes outside a frame, and
// frames that are too long or fail the checksum, are dropped.
BOOLEAN
GamepadParse (
  IN OUT GAMEPAD_PARSER *Parser,
  IN UINT8               Byte,
  OUT GAMEPAD_FRAME     *Out
);

// Returns the buttons held in a status frame, masked to GAMEPAD_BUTTONS.
// FALSE if Frame isn't a status frame of at least 6 bytes, as in the driver.
BOOLEAN
GamepadButtons (
  IN CONST GAMEPAD_FRAME *Frame,
  OUT UINT16             *Buttons
);

// Builds a frame in Out, which holds GAMEPAD_FRAME_OVERHEAD + Len bytes.
// Returns its length.
UINTN
GamepadFrameBuild (
  IN UINT8        Sequence,
  IN UINT8        Cmd,
  IN CONST UINT8 *Data,
  IN UINT16       Len,
  OUT UINT8      *Out
);

// The COMMOD payloads the driver sends after powering the MCU on: ask for
// its version, then set its parameters, 100 ms apart.
extern CONST UINT8 gGamepadVersionRequest[1];
extern CONST UINT8 gGamepadSetParameters[10];

// Where a model's MCU is: a QUPv3 serial engine set up as a UART by the
// firmware, its clocks, its pins and the MCU's power. Addresses are
// physical, offsets are from the block named.
typedef struct _GAMEPAD_BOARD {
  // As named by the device-model setting.
  CONST CHAR8 *Model;
  // ChipInfo ids the board comes with. Nothing is touched on another chip.
  UINT32 ChipIds[4];
  UINT64 Gcc;
  // The apps clock vote register, the bits that turn on the serial engine
  // and its QUP wrapper, and each clock's branch register (bit 31 is set
  // while it is off).
  UINT32 ClockVote;
  UINT32 ClockVoteBits;
  UINT32 ClockBranches[5];
  // The serial engine clock's RCG, read to find the rate for 115200 baud.
  UINT32 SeRcg;
  UINT64 Wrapper;
  UINT64 Se;
  UINT64 Tlmm;
  UINT16 TxGpio, RxGpio;
  UINT8  UartFunction;
  // Driven high, in order: the MCU's 3.3 V supply, then its enable.
  UINT16 PowerGpios[2];
} GAMEPAD_BOARD;

// The approved models. Only these get the gamepad: the MCU's wiring varies.
extern CONST GAMEPAD_BOARD gGamepadBoards[];
extern CONST UINTN         gGamepadBoardsCount;

// Returns NULL if Model isn't approved. Case is ignored, as in the ABL.
CONST GAMEPAD_BOARD *
GamepadBoardForModel (IN CONST CHAR8 *Model);

#endif
