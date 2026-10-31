/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - the RSInput gamepad as menu input.
*/

#include "Efi/Gamepad.h"

#include "Base/Buffer.h"
#include "Efi/Qcom.h"

// When no report has come by then, say so once.
#define SILENT_MS 2000

typedef enum {
  PAD_OFF,
  // Supply on, waiting to enable the MCU.
  PAD_POWERED,
  // MCU enabled, waiting to open the UART.
  PAD_RELEASED,
  // Sending the init commands.
  PAD_INIT,
  PAD_RUNNING,
} PAD_STAGE;

typedef struct _PAD {
  PAD_STAGE      Stage;
  BOOLEAN        Known;
  RSINPUT_CONFIG      Config;
  CONST RSINPUT_INIT *Init;
  RSINPUT_SAVED       Saved;
  LOG_SINK       Log;
  // When the next stage is due, ms.
  UINT64 Due;
  UINT64 Started;
  UINT8  Sequence;
  UINTN  Sent;
  // The clocks are on, so the serial engine's registers can be touched.
  BOOLEAN    ClocksOn;
  RSINPUT_RX Rx;
  // Bytes received, for the log.
  UINT64         RawCount;
  RSINPUT_REPORT Report;
  KEYS           Decoder;
  BOOLEAN        SaidVersion;
  BOOLEAN        SaidReport;
  BOOLEAN        SaidSilent;
} PAD;

STATIC PAD mPad;

// Sends init command Index.
STATIC VOID
SendInit (IN UINTN Index)
{
  UINT8 Frame[RSINPUT_FRAME_MAX];
  UINTN Length = RsInputFrame (
    mPad.Sequence++, RSINPUT_CMD_COMMOD, mPad.Init->Commands[Index].Payload,
    mPad.Init->Commands[Index].Length, Frame
  );

  if (!RsInputUartWrite (&gMmioHardware, &mPad.Config, Frame, Length)) {
    LogPrint (mPad.Log, "gamepad: command %llu wasn't sent", (UINT64)Index);
  }
}

BOOLEAN
GamepadStart (
  IN CONST RSINPUT_CONFIG *Config,
  IN UINT64                Now,
  IN LOG_SINK              Log
)
{
  QCOM_CHIP Chip;
  UINT32    Protocol;

  GamepadStop ();
  mPad =
    (PAD){.Config = *Config, .Init = RsInputInit (Config->Driver), .Log = Log};
  if (!QcomChip (&Chip) ||
      !RsInputSocIsChip (
        Config->Soc, EFI_ERROR (Chip.Id.Status) ? 0 : Chip.Id.Value, Chip.Name
      )) {
    LogPrint (Log, "gamepad: the devicetree is for another chip");
    return FALSE;
  }
  RsInputPinsOn (&gMmioHardware, Config, &mPad.Saved);
  mPad.Stage = PAD_POWERED;
  if (!RsInputClocksOn (&gMmioHardware, Config, &mPad.Saved)) {
    LogPrint (Log, "gamepad: a clock didn't start");
    GamepadStop ();
    return FALSE;
  }
  mPad.ClocksOn = TRUE;
  Protocol      = RsInputUartProtocol (&gMmioHardware, Config);
  if (Protocol != RSINPUT_GENI_UART) {
    LogPrint (
      Log, "gamepad: serial engine 0x%llx has protocol %u, not UART",
      Config->Uart, Protocol
    );
    GamepadStop ();
    return FALSE;
  }
  mPad.Due     = Now + mPad.Init->PowerMs;
  mPad.Started = Now;
  mPad.Known   = TRUE;
  LogPrint (
    Log,
    "gamepad: starting on UART 0x%llx, enable GPIO %d%s, %u always-on supplies",
    Config->Uart, Config->Enable.Present ? (INT32)Config->Enable.Gpio : -1,
    Config->Supply.Present ? ", supply switched" : ", supply left to firmware",
    Config->AlwaysOnCount
  );
  return TRUE;
}

// The menu input the buttons and the left stick hold, if any. Up and Down
// come first, so a diagonal still scrolls.
STATIC BOOLEAN
HeldKey (OUT INPUT *Key)
{
  CONST RSINPUT_REPORT *R    = &mPad.Report;
  INT32                 Y    = mPad.Config.InvertY ? -R->LeftY : R->LeftY;
  INT32                 Half = (INT32)(mPad.Config.AxisRange / 2);

  if ((R->Buttons & RSINPUT_BTN_DPAD_UP) || Y < -Half) {
    *Key = INPUT_UP;
  } else if ((R->Buttons & RSINPUT_BTN_DPAD_DOWN) || Y > Half) {
    *Key = INPUT_DOWN;
  } else if (R->Buttons & (RSINPUT_BTN_SOUTH | RSINPUT_BTN_START)) {
    *Key = INPUT_SELECT;
  } else if (R->Buttons & (RSINPUT_BTN_EAST | RSINPUT_BTN_BACK)) {
    *Key = INPUT_BACK;
  } else {
    return FALSE;
  }
  return TRUE;
}

// Reads what came in, and turns the held button into inputs.
STATIC VOID
Receive (
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
)
{
  UINT8 Bytes[256];
  UINTN Count;
  INPUT Key;
  INPUT Decoded;

  while (
    (Count =
       RsInputUartRead (&gMmioHardware, &mPad.Config, Bytes, sizeof (Bytes)))) {
    mPad.RawCount += Count;
    RsInputRxFeed (&mPad.Rx, Bytes, Count, &mPad.Report);
  }
  if (mPad.Report.Version[0] && !mPad.SaidVersion) {
    mPad.SaidVersion = TRUE;
    LogPrint (mPad.Log, "gamepad: MCU version %s", mPad.Report.Version);
  }
  if (mPad.Report.Seen && !mPad.SaidReport) {
    mPad.SaidReport = TRUE;
    LogPrint (mPad.Log, "gamepad: reporting after %llu ms", Now - mPad.Started);
  }
  if (!mPad.Report.Seen && !mPad.SaidSilent &&
      Now - mPad.Started >= SILENT_MS) {
    mPad.SaidSilent = TRUE;
    LogPrint (
      mPad.Log, "gamepad: no reports yet (%llu bytes, %u bad frames)",
      mPad.RawCount, mPad.Rx.Bad
    );
  }
  if (!mPad.Report.Seen) {
    return;
  }
  if (!HeldKey (&Key)) {
    // The MCU says exactly when a button is up, so there is no gap to wait
    // for.
    mPad.Decoder.Held = FALSE;
    return;
  }
  // The input is lost if memory runs out.
  if (KeysPress (&mPad.Decoder, Key, Now, &Decoded)) {
    (VOID)VecPush (Out, Decoded);
  }
}

VOID
GamepadPoll (
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
)
{
  switch (mPad.Stage) {
  case PAD_OFF:
    return;
  case PAD_POWERED:
    if (Now < mPad.Due) {
      return;
    }
    RsInputRelease (&gMmioHardware, &mPad.Config);
    mPad.Stage = PAD_RELEASED;
    mPad.Due   = Now + mPad.Init->ReleaseMs;
    return;
  case PAD_RELEASED:
    if (Now < mPad.Due) {
      return;
    }
    RsInputUartSetup (&gMmioHardware, &mPad.Config);
    mPad.Stage = PAD_INIT;
    mPad.Due   = Now + mPad.Init->CommandMs;
    return;
  case PAD_INIT:
    if (Now >= mPad.Due) {
      SendInit (mPad.Sent++);
      mPad.Due = Now + mPad.Init->CommandMs;
      if (mPad.Sent == mPad.Init->Count) {
        mPad.Stage = PAD_RUNNING;
      }
    }
    break;
  case PAD_RUNNING:
    break;
  }
  Receive (Now, Out);
}

VOID
GamepadAfterBusy (IN UINT64 Now)
{
  if (mPad.Decoder.Held) {
    KeysMute (&mPad.Decoder, mPad.Decoder.Key, Now);
  }
}

BOOLEAN
GamepadStop (VOID)
{
  if (mPad.Stage == PAD_OFF) {
    return FALSE;
  }
  RsInputOff (&gMmioHardware, &mPad.Config, &mPad.Saved, mPad.ClocksOn);
  mPad.ClocksOn = FALSE;
  mPad.Stage    = PAD_OFF;
  LogPrint (mPad.Log, "gamepad: off");
  return TRUE;
}

VOID
GamepadResume (IN UINT64 Now)
{
  if (mPad.Known && mPad.Stage == PAD_OFF) {
    RSINPUT_CONFIG Config = mPad.Config;
    GamepadStart (&Config, Now, mPad.Log);
  }
}
