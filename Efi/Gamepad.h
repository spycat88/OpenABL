/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad - reads the buttons of an approved model's RSInput MCU
  (Core/Gamepad.h) through its UART.

  Opening turns on the serial engine's clocks, muxes its pins, powers the
  MCU and starts receiving; the MCU is sent its two setup commands from
  GamepadPoll once it has had time to start, as the Linux driver waits.
  Everything opening changed is put back as it was found by GamepadClose,
  or at ExitBootServices if it is still open, so the OS finds the hardware
  as XBL left it. There is one gamepad.
*/

#ifndef __EFI_GAMEPAD_H__
#define __EFI_GAMEPAD_H__

#include "Base/OablBase.h"

#include "Efi/Serial.h"

// Opens the gamepad if Model is approved and the chip and serial engine are
// the ones expected. Returns FALSE, having changed nothing, otherwise. Log
// gets one line on why, and later the MCU's version.
BOOLEAN
GamepadOpen (
  IN CONST CHAR8 *Model,
  IN LOG_SINK     Log
);

// Reads what the MCU sent and returns the buttons held (GAMEPAD_*) at Now
// (ms). With no status frame for a while, nothing is held. FALSE if the
// gamepad isn't open.
BOOLEAN
GamepadPoll (
  IN UINT64   Now,
  OUT UINT16 *Buttons
);

// Stops the UART and puts the clocks and pins back. Does nothing if the
// gamepad isn't open.
VOID
GamepadClose (VOID);

#endif
