/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  RsInput - the gamepad MCU of the Retroid Pocket and AYN handhelds.

  A fresh implementation of the protocol Linux's rsinput and
  retroid-pocket-gamepad drivers speak. A frame is A5 D3 5A 3D, a sequence
  number, a command, a little-endian payload length, the payload and an XOR
  of everything from the sequence number to the end of the payload. The
  host asks for the version (01 02) and sets the report parameters
  (01 05 ...); the MCU then sends status frames (02): two bytes of buttons,
  in GAMEPAD_BTN_ order, the two triggers and the four stick axes, 16 bits
  each.
*/

#include "Core/Gamepad/Driver.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"

#define CMD_COMMOD 0x01
#define CMD_STATUS 0x02

#define COMMOD_VERSION 0x02
#define COMMOD_SET_PAR 0x05

STATIC CONST UINT8 mHead[4] = {0xA5, 0xD3, 0x5A, 0x3D};

#define POS_SEQ  4
#define POS_CMD  5
#define POS_LEN  6
#define POS_DATA 8

// Header (4), sequence, command, length (2) and checksum.
#define FRAME_OVERHEAD 9
#define PAYLOAD_MAX    64
#define FRAME_MAX      (FRAME_OVERHEAD + PAYLOAD_MAX)

STATIC CONST CHAR8 *CONST mCompatibles[] = {
  "retroidpocket,rsinput",          "ayntec,rsinput",    "gamepad,rsinput",
  "retroid,retroid-pocket-gamepad", "ayn,odin2-gamepad", NULL,
};

// The init messages: the version request, then the report parameters.
STATIC CONST UINT8 mVersionRequest[] = {COMMOD_VERSION};
STATIC CONST UINT8 mSetParameters[]  = {
  COMMOD_SET_PAR, 0x01, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x07,
};

typedef struct _INIT {
  CONST UINT8 *Payload;
  UINTN        Length;
} INIT;

STATIC CONST INIT mInit[] = {
  {mVersionRequest, sizeof (mVersionRequest)},
  {mSetParameters, sizeof (mSetParameters)},
};

// The sequence number of the next frame sent, and bytes received but not
// yet a whole frame.
STATIC UINT8 mSequence;
STATIC UINT8 mRx[FRAME_MAX * 2];
STATIC UINTN mRxLen;

// XOR of Frame's bytes from the sequence number up to, not including, its
// last byte.
STATIC UINT8
Checksum (
  IN CONST UINT8 *Frame,
  IN UINTN        Length
)
{
  UINT8 Sum = 0;

  for (UINTN Index = POS_SEQ; Index + 1 < Length; Index++) {
    Sum ^= Frame[Index];
  }
  return Sum;
}

STATIC VOID
Begin (VOID)
{
  mSequence = 0;
  mRxLen    = 0;
}

STATIC UINTN
Init (
  IN UINTN  Index,
  OUT UINT8 Out[GAMEPAD_INIT_MAX]
)
{
  UINTN Total;

  if (Index >= sizeof (mInit) / sizeof (*mInit)) {
    return 0;
  }
  Total = FRAME_OVERHEAD + mInit[Index].Length;
  CopyMem (Out, mHead, sizeof (mHead));
  Out[POS_SEQ] = mSequence++;
  Out[POS_CMD] = CMD_COMMOD;
  WriteLe16 (Out + POS_LEN, (UINT16)mInit[Index].Length);
  CopyMem (Out + POS_DATA, mInit[Index].Payload, mInit[Index].Length);
  Out[Total - 1] = Checksum (Out, Total);
  return Total;
}

// Linux reports a stick as the negated raw value.
STATIC INT32
Axis (IN CONST UINT8 *Bytes)
{
  return -(INT32)(INT16)ReadLe16 (Bytes);
}

// Takes one whole, checked frame.
STATIC VOID
Take (
  IN CONST UINT8        *Frame,
  IN UINTN               Length,
  IN OUT GAMEPAD_REPORT *Report
)
{
  CONST UINT8 *Data = Frame + POS_DATA;

  if (Frame[POS_CMD] == CMD_STATUS && Length >= 6) {
    Report->Seen    = TRUE;
    Report->Buttons = ReadLe16 (Data);
    if (Length >= 10) {
      Report->LeftX = Axis (Data + 6);
      Report->LeftY = Axis (Data + 8);
    }
  } else if (Frame[POS_CMD] == CMD_COMMOD && Length >= 1 &&
             Data[0] == COMMOD_VERSION) {
    UINTN Out = 0;

    // The text after the subcommand, printable ASCII only.
    for (UINTN Index = 1; Index < Length && Out + 1 < sizeof (Report->Version);
         Index++) {
      if (Data[Index] >= 0x20 && Data[Index] < 0x7f) {
        Report->Version[Out++] = (CHAR8)Data[Index];
      }
    }
    Report->Version[Out] = 0;
  }
}

STATIC VOID
Feed (
  IN CONST UINT8        *Bytes,
  IN UINTN               Count,
  IN OUT GAMEPAD_REPORT *Report
)
{
  UINTN At = 0;

  // Keep the newest bytes when more come than fit.
  if (Count > sizeof (mRx)) {
    Bytes += Count - sizeof (mRx);
    Count  = sizeof (mRx);
    mRxLen = 0;
  }
  if (mRxLen + Count > sizeof (mRx)) {
    UINTN Drop = mRxLen + Count - sizeof (mRx);
    CopyMem (mRx, mRx + Drop, mRxLen - Drop);
    mRxLen -= Drop;
  }
  CopyMem (mRx + mRxLen, Bytes, Count);
  mRxLen += Count;

  while (mRxLen - At >= FRAME_OVERHEAD) {
    CONST UINT8 *Frame = mRx + At;
    UINTN        Length;

    if (CompareMem (Frame, mHead, sizeof (mHead)) != 0) {
      At++;
      continue;
    }
    Length = ReadLe16 (Frame + POS_LEN);
    if (Length > PAYLOAD_MAX) {
      Report->Bad++;
      At++;
      continue;
    }
    if (mRxLen - At < FRAME_OVERHEAD + Length) {
      break;
    }
    if (Checksum (Frame, FRAME_OVERHEAD + Length) !=
        Frame[FRAME_OVERHEAD + Length - 1]) {
      Report->Bad++;
      At++;
      continue;
    }
    Take (Frame, Length, Report);
    At += FRAME_OVERHEAD + Length;
  }
  CopyMem (mRx, mRx + At, mRxLen - At);
  mRxLen -= At;
}

CONST GAMEPAD_DRIVER gRsInputDriver = {
  .Name        = "rsinput",
  .Compatibles = mCompatibles,
  .AxisRange   = 0x580,
  .PowerMs     = 20,
  .ReleaseMs   = 50,
  .InitMs      = 50,
  .Begin       = Begin,
  .Init        = Init,
  .Feed        = Feed,
};
