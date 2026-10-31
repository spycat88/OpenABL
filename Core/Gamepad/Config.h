/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad Config - where the gamepad MCU is, and which driver speaks to it,
  read from the device model's devicetree.

  The gamepad node, found by a driver's compatible, sits under a
  "qcom,geni-uart" in a "qcom,geni-se-qup". From the tree come the serial
  engine and wrapper addresses, the UART's and the node's pins, the enable,
  reset and boot GPIOs, the supplies and the clock ids; Core/Gamepad/Soc.h
  fills in what the tree can't say. The result is small and fixed-size, so
  the menu caches it with the model.
*/

#ifndef __CORE_GAMEPAD_CONFIG_H__
#define __CORE_GAMEPAD_CONFIG_H__

#include "Base/OablBase.h"

#include "Core/Gamepad/Driver.h"
#include "Core/Gamepad/Soc.h"

// TLMM pull, as in the CFG register.
#define GAMEPAD_PULL_NONE   0
#define GAMEPAD_PULL_DOWN   1
#define GAMEPAD_PULL_KEEPER 2
#define GAMEPAD_PULL_UP     3

// A TLMM pin's mux and pad settings.
typedef struct _GAMEPAD_PIN {
  UINT16 Gpio;
  UINT8  Func;
  UINT8  Pull;
  // 2 to 16, even.
  UINT8 DriveMa;
} GAMEPAD_PIN;

// A TLMM line the host drives: a GPIO, or a fixed regulator's enable.
typedef struct _GAMEPAD_LINE {
  BOOLEAN Present;
  BOOLEAN ActiveLow;
  UINT16  Gpio;
} GAMEPAD_LINE;

#define GAMEPAD_PINS_MAX      8
#define GAMEPAD_CLOCKS_MAX    8
#define GAMEPAD_ALWAYS_ON_MAX 4

// Everything the bring-up needs, read from the devicetree. It is cached in
// a setting as raw bytes after GAMEPAD_CONFIG_VERSION, so bump that when
// the layout changes.
typedef struct _GAMEPAD_CONFIG {
  // Indexes into gGamepadSocs and gGamepadDrivers.
  UINT8 Soc;
  UINT8 Driver;
  // The serial engine, its QUP wrapper, the TLMM's tiles in the order of
  // the SoC's TileNames, and the GCC.
  UINT64 Uart;
  UINT64 Wrapper;
  UINT64 Tlmm[GAMEPAD_TILES_MAX];
  UINT64 Gcc;
  // The UART's pins, then the gamepad node's own pinctrl.
  UINT8       PinCount;
  GAMEPAD_PIN Pins[GAMEPAD_PINS_MAX];
  // A fixed regulator's enable, when vdd-supply is one.
  GAMEPAD_LINE Supply;
  // The enables of always-on fixed regulators, which Linux turns on before
  // any driver runs. The Retroid Pocket's MCU 3.3 V is one, not named by
  // the gamepad node.
  UINT8        AlwaysOnCount;
  GAMEPAD_LINE AlwaysOn[GAMEPAD_ALWAYS_ON_MAX];
  GAMEPAD_LINE Enable;
  GAMEPAD_LINE Reset;
  GAMEPAD_LINE Boot;
  // GCC clock ids: the wrapper's, then the serial engine's.
  UINT8  ClockCount;
  UINT16 Clocks[GAMEPAD_CLOCKS_MAX];
  // The serial engine's clock, which is set to the UART rate.
  UINT16  SeClock;
  BOOLEAN InvertY;
  UINT32  AxisRange;
} GAMEPAD_CONFIG;

#define GAMEPAD_CONFIG_VERSION 7
#define GAMEPAD_CONFIG_BYTES   (1 + sizeof (GAMEPAD_CONFIG))

// Writes Config as GAMEPAD_CONFIG_VERSION then its bytes.
VOID
GamepadConfigEncode (
  IN CONST GAMEPAD_CONFIG *Config,
  OUT UINT8                Out[GAMEPAD_CONFIG_BYTES]
);

// Reads what GamepadConfigEncode wrote. FALSE for another version, size,
// SoC or driver.
BOOLEAN
GamepadConfigDecode (
  IN CONST UINT8     *Bytes,
  IN UINTN            Size,
  OUT GAMEPAD_CONFIG *Config
);

// Why GamepadFromDt found no gamepad.
typedef enum {
  GAMEPAD_DT_OK,
  // No enabled node a driver binds.
  GAMEPAD_DT_NONE,
  // Not under an enabled qcom,geni-uart in a qcom,geni-se-qup.
  GAMEPAD_DT_NOT_ON_GENI,
  // The TLMM or GCC isn't one of a SoC in gGamepadSocs.
  GAMEPAD_DT_UNKNOWN_SOC,
  // A pin function or clock isn't in the SoC's entry.
  GAMEPAD_DT_NOT_IN_TABLE,
  // A property is missing or malformed.
  GAMEPAD_DT_BAD,
} GAMEPAD_DT_RESULT;

CONST CHAR8 *
GamepadDtResultName (IN GAMEPAD_DT_RESULT Result);

// Reads the gamepad from Fdt. Supplies other than a GPIO-switched fixed
// regulator are left to the firmware, as nothing here can switch them.
GAMEPAD_DT_RESULT
GamepadFromDt (
  IN CONST UINT8     *Fdt,
  IN UINTN            Size,
  OUT GAMEPAD_CONFIG *Config
);

#endif
