/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - the RSInput gamepad (Core/RsInput.h) as menu input.

  GamepadStart checks the chip, sets the pins, turns the clocks on and
  checks the serial engine has UART firmware; the rest of the power-up
  follows Linux's timing (20 ms, then 50 ms, then the two commands 50 ms
  apart) in GamepadPoll, so the menu doesn't wait for it. The D-pad and the
  left stick move the selection, A (south) and Start select, B (east) and
  Back go back. Everything the bring-up changed is put back by GamepadStop,
  which must run before anything else boots. Log lines start with
  "gamepad: ".
*/

#ifndef __EFI_GAMEPAD_H__
#define __EFI_GAMEPAD_H__

#include "Base/OablBase.h"

#include "Core/RsInput.h"
#include "Efi/Input.h"
#include "Efi/Serial.h"

// Starts the gamepad Config describes at time Now (ms), stopping one
// already on. FALSE, logged, if it can't run here.
BOOLEAN
GamepadStart (
  IN CONST RSINPUT_CONFIG *Config,
  IN UINT64                Now,
  IN LOG_SINK              Log
);

// Moves the power-up along and appends the inputs the buttons make.
VOID
GamepadPoll (
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
);

// Mutes a button still held after the menu was busy, until it is released.
VOID
GamepadAfterBusy (IN UINT64 Now);

// Powers the MCU down and puts back the pins and clocks. Returns TRUE if
// the gamepad was on.
BOOLEAN
GamepadStop (VOID);

// Starts the last gamepad again, after a GamepadStop.
VOID
GamepadResume (IN UINT64 Now);

#endif
