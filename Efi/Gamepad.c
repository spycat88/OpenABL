/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - the gamepad MCU as menu input.
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
  // Sending the driver's init messages.
  PAD_INIT,
  PAD_RUNNING,
} PAD_STAGE;

typedef struct _PAD {
  PAD_STAGE Stage;
  // Config holds a gamepad started before, for GamepadResume.
  BOOLEAN               Known;
  GAMEPAD_CONFIG        Config;
  CONST GAMEPAD_DRIVER *Driver;
  GAMEPAD_SAVED         Saved;
  LOG_SINK              Log;
  // When the next stage is due, ms.
  UINT64 Due;
  UINT64 Started;
  UINTN  Sent;
  // The clocks are on, so the serial engine's registers can be touched.
  BOOLEAN ClocksOn;
  // Bytes received, for the log.
  UINT64         RawCount;
  GAMEPAD_REPORT Report;
  KEYS           Decoder;
  BOOLEAN        SaidVersion;
  BOOLEAN        SaidReport;
  BOOLEAN        SaidSilent;
} PAD;

STATIC PAD mPad;

// Sends the driver's next init message. FALSE once there are no more.
STATIC BOOLEAN
SendInit (VOID)
{
  UINT8 Message[GAMEPAD_INIT_MAX];
  UINTN Length = mPad.Driver->Init (mPad.Sent, Message);

  if (!Length) {
    return FALSE;
  }
  if (!GamepadHwUartWrite (&gMmioHardware, &mPad.Config, Message, Length)) {
    LogPrint (
      mPad.Log, "gamepad: init message %llu wasn't sent", (UINT64)mPad.Sent
    );
  }
  mPad.Sent++;
  return TRUE;
}

BOOLEAN
GamepadStart (
  IN CONST GAMEPAD_CONFIG *Config,
  IN UINT64                Now,
  IN LOG_SINK              Log
)
{
  CONST GAMEPAD_SOC *Soc = &gGamepadSocs[Config->Soc];
  QCOM_CHIP          Chip;
  UINT32             Protocol;

  GamepadStop ();
  mPad = (PAD){
    .Config = *Config, .Driver = gGamepadDrivers[Config->Driver], .Log = Log};
  if (!QcomChip (&Chip) ||
      !GamepadSocIsChip (
        Soc, EFI_ERROR (Chip.Id.Status) ? 0 : Chip.Id.Value, Chip.Name
      )) {
    LogPrint (
      Log, "gamepad: the devicetree is for %s, not this chip", Soc->Name
    );
    return FALSE;
  }
  GamepadHwPinsOn (&gMmioHardware, Config, &mPad.Saved);
  mPad.Stage = PAD_POWERED;
  if (!GamepadHwClocksOn (&gMmioHardware, Config, &mPad.Saved)) {
    LogPrint (Log, "gamepad: a clock didn't start");
    GamepadStop ();
    return FALSE;
  }
  mPad.ClocksOn = TRUE;
  Protocol      = GamepadHwUartProtocol (&gMmioHardware, Config);
  if (Protocol != GAMEPAD_GENI_UART) {
    LogPrint (
      Log, "gamepad: serial engine 0x%llx has protocol %u, not UART",
      Config->Uart, Protocol
    );
    GamepadStop ();
    return FALSE;
  }
  mPad.Due     = Now + mPad.Driver->PowerMs;
  mPad.Started = Now;
  mPad.Known   = TRUE;
  mPad.Driver->Begin ();
  LogPrint (
    Log, "gamepad: starting the %s MCU on %s UART 0x%llx", mPad.Driver->Name,
    Soc->Name, Config->Uart
  );
  return TRUE;
}

// The menu input the buttons and the left stick hold, if any. Up and Down
// come first, so a diagonal still scrolls.
STATIC BOOLEAN
HeldKey (OUT INPUT *Key)
{
  CONST GAMEPAD_REPORT *R    = &mPad.Report;
  INT32                 Y    = mPad.Config.InvertY ? -R->LeftY : R->LeftY;
  INT32                 Half = (INT32)(mPad.Config.AxisRange / 2);

  if ((R->Buttons & GAMEPAD_BTN_DPAD_UP) || Y < -Half) {
    *Key = INPUT_UP;
  } else if ((R->Buttons & GAMEPAD_BTN_DPAD_DOWN) || Y > Half) {
    *Key = INPUT_DOWN;
  } else if (R->Buttons & (GAMEPAD_BTN_SOUTH | GAMEPAD_BTN_START)) {
    *Key = INPUT_SELECT;
  } else if (R->Buttons & (GAMEPAD_BTN_EAST | GAMEPAD_BTN_BACK)) {
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

  while ((
    Count =
      GamepadHwUartRead (&gMmioHardware, &mPad.Config, Bytes, sizeof (Bytes))
  )) {
    mPad.RawCount += Count;
    mPad.Driver->Feed (Bytes, Count, &mPad.Report);
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
      mPad.RawCount, mPad.Report.Bad
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
    GamepadHwRelease (&gMmioHardware, &mPad.Config);
    mPad.Stage = PAD_RELEASED;
    mPad.Due   = Now + mPad.Driver->ReleaseMs;
    return;
  case PAD_RELEASED:
    if (Now < mPad.Due) {
      return;
    }
    GamepadHwUartSetup (&gMmioHardware, &mPad.Config);
    mPad.Stage = PAD_INIT;
    mPad.Due   = Now + mPad.Driver->InitMs;
    return;
  case PAD_INIT:
    if (Now >= mPad.Due) {
      mPad.Due = Now + mPad.Driver->InitMs;
      if (!SendInit ()) {
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
  GamepadHwOff (&gMmioHardware, &mPad.Config, &mPad.Saved, mPad.ClocksOn);
  mPad.ClocksOn = FALSE;
  mPad.Stage    = PAD_OFF;
  LogPrint (mPad.Log, "gamepad: off");
  return TRUE;
}

VOID
GamepadResume (IN UINT64 Now)
{
  if (mPad.Known && mPad.Stage == PAD_OFF) {
    GAMEPAD_CONFIG Config = mPad.Config;
    GamepadStart (&Config, Now, mPad.Log);
  }
}
