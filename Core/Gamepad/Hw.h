/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad Hw - bringing up the gamepad MCU and its UART from a
  GAMEPAD_CONFIG: the TLMM pins and lines, the GCC clocks and the GENI
  serial engine in FIFO mode, polled.

  Every register access goes through an MMIO so host tests can check the
  sequences; waits are bounded polls. The pins and clock votes changed are
  saved, to put back before anything else boots.
*/

#ifndef __CORE_GAMEPAD_HW_H__
#define __CORE_GAMEPAD_HW_H__

#include "Base/OablBase.h"

#include "Core/Mmio.h"
#include "Core/Gamepad/Config.h"

// TLMM registers the bring-up changes, kept to put back.
typedef struct _GAMEPAD_SAVED {
  UINT8  PinCount;
  UINT16 Gpio[GAMEPAD_PINS_MAX + GAMEPAD_ALWAYS_ON_MAX + 4];
  UINT32 Cfg[GAMEPAD_PINS_MAX + GAMEPAD_ALWAYS_ON_MAX + 4];
  UINT32 Io[GAMEPAD_PINS_MAX + GAMEPAD_ALWAYS_ON_MAX + 4];
  // GCC vote registers and their values before.
  UINT8  VoteCount;
  UINT32 VoteReg[GAMEPAD_CLOCKS_MAX];
  UINT32 Vote[GAMEPAD_CLOCKS_MAX];
} GAMEPAD_SAVED;

// Saves, then sets, the pins' mux and pads, and drives the lines as Linux's
// probe does before its wait: the always-on supplies and the supply on,
// boot and reset inactive.
VOID
GamepadHwPinsOn (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  OUT GAMEPAD_SAVED       *Saved
);

// Starts the MCU as Linux's probe does after its wait: enable and reset
// active.
VOID
GamepadHwRelease (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config
);

// Turns the clocks on and sets the serial engine's to 7.3728 MHz. FALSE if
// one doesn't report running.
BOOLEAN
GamepadHwClocksOn (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN OUT GAMEPAD_SAVED    *Saved
);

// Returns the serial engine's protocol (2 is UART) from its firmware
// revision.
UINT32
GamepadHwUartProtocol (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config
);

#define GAMEPAD_GENI_UART 2

// Sets the serial engine up for 115200 8N1 in FIFO mode, without flow
// control, and starts receiving.
VOID
GamepadHwUartSetup (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config
);

// Sends Length bytes, at most the TX FIFO's 64. FALSE if the engine didn't
// finish.
BOOLEAN
GamepadHwUartWrite (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN CONST UINT8          *Bytes,
  IN UINTN                 Length
);

// Reads what the RX FIFO holds, up to Size bytes. Returns the count.
UINTN
GamepadHwUartRead (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  OUT UINT8               *Out,
  IN UINTN                 Size
);

// Stops receiving if Uart (the engine's clocks are on), powers the MCU
// down and puts back what was saved.
VOID
GamepadHwOff (
  IN CONST MMIO           *M,
  IN CONST GAMEPAD_CONFIG *Config,
  IN CONST GAMEPAD_SAVED  *Saved,
  IN BOOLEAN               Uart
);

#endif
