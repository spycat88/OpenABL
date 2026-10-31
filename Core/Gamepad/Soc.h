/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad Soc - what a devicetree can't say about the SoCs the gamepad
  MCU runs on.

  A devicetree names pin functions and clocks, not the registers behind
  them. Each SoC here gives, from the kernel's pinctrl and gcc drivers, the
  mux number of each UART function on each pin, which TLMM tile holds each
  pin, and the GCC vote bit, branch register and RCG of each clock the
  gamepad's serial engine needs. Adding a device on another SoC means
  adding its entry to Core/Gamepad/Soc.c.
*/

#ifndef __CORE_GAMEPAD_SOC_H__
#define __CORE_GAMEPAD_SOC_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

#define GAMEPAD_TILES_MAX 3

// A pin function's mux number on one pin. "gpio" is 0 on every pin and
// isn't listed. In the kernel's PINGROUP(pin, f1, f2, ...), f1 is mux 1.
typedef struct _GAMEPAD_SOC_FUNC {
  CONST CHAR8 *Name;
  UINT16       Gpio;
  UINT8        Mux;
} GAMEPAD_SOC_FUNC;

// Pins First to Last are in TLMM tile Tile (an index into TileNames).
typedef struct _GAMEPAD_SOC_TILE {
  UINT16 First;
  UINT16 Last;
  UINT8  Tile;
} GAMEPAD_SOC_TILE;

#define GAMEPAD_SOC_NEEDS_MAX 2

// A GCC branch clock: its vote bit and branch register (bit 31 is
// CLK_OFF). A serial engine clock also has its RCG and the RCG's source
// index for GPLL0's even output. Needs are other clocks it needs that the
// devicetree doesn't list (interconnect votes in Linux); 0 ends them.
typedef struct _GAMEPAD_SOC_CLOCK {
  UINT16 Id;
  UINT32 VoteReg;
  UINT8  VoteBit;
  UINT32 Cbcr;
  UINT32 Rcg;
  UINT8  RcgSrc;
  UINT16 Needs[GAMEPAD_SOC_NEEDS_MAX];
} GAMEPAD_SOC_CLOCK;

typedef struct _GAMEPAD_SOC {
  CONST CHAR8 *Name;
  // The TLMM's and the GCC's compatibles, which pick the entry.
  CONST CHAR8 *Tlmm;
  CONST CHAR8 *Gcc;
  // ChipInfo ids and name prefixes the SoC reports, to check a devicetree
  // against the chip it runs on. 0 and NULL end them.
  UINT32       ChipIds[4];
  CONST CHAR8 *ChipNames[2];
  // Bytes between pins' registers.
  UINT32 TlmmStride;
  // The TLMM's reg-names when it is split into tiles, and each pin's tile.
  // A pin in no range is in tile 0. Without names the TLMM has one region.
  CONST CHAR8             *TileNames[GAMEPAD_TILES_MAX];
  CONST GAMEPAD_SOC_TILE  *Tiles;
  UINTN                    TileCount;
  CONST GAMEPAD_SOC_FUNC  *Funcs;
  UINTN                    FuncCount;
  CONST GAMEPAD_SOC_CLOCK *Clocks;
  UINTN                    ClockCount;
} GAMEPAD_SOC;

// The SoCs, in a fixed order: a cached GAMEPAD_CONFIG holds an index, so
// new ones go at the end.
extern CONST GAMEPAD_SOC gGamepadSocs[];
extern CONST UINTN       gGamepadSocCount;

// The SoC whose TLMM and GCC have these compatibles, or NULL.
CONST GAMEPAD_SOC *
GamepadSocOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        TlmmNode,
  IN UINTN        GccNode,
  OUT UINT8      *Index
);

// TRUE if ChipInfo's id or name is Soc's.
BOOLEAN
GamepadSocIsChip (
  IN CONST GAMEPAD_SOC *Soc,
  IN UINT32             ChipId,
  IN CONST CHAR8       *Name OPTIONAL
);

// The mux number of Function on Gpio.
BOOLEAN
GamepadSocMux (
  IN CONST GAMEPAD_SOC *Soc,
  IN OABL_TEXT          Function,
  IN UINT16             Gpio,
  OUT UINT8            *Mux
);

UINT8
GamepadSocTile (
  IN CONST GAMEPAD_SOC *Soc,
  IN UINT16             Gpio
);

// Clock Id, or NULL.
CONST GAMEPAD_SOC_CLOCK *
GamepadSocClock (
  IN CONST GAMEPAD_SOC *Soc,
  IN UINT16             Id
);

#endif
