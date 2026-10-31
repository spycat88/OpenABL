/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad Soc - what a devicetree can't say about the SoCs the gamepad
  MCU runs on.
*/

#include "Core/Gamepad/Soc.h"

#include "Core/DeviceTree.h"

#define COUNT_OF(Array) (sizeof (Array) / sizeof (*(Array)))

// A wrapper's AHB or core clock.
#define BRANCH(ClockId, Reg, Bit, Branch)                                      \
  {.Id = (ClockId), .VoteReg = (Reg), .VoteBit = (Bit), .Cbcr = (Branch)}
// A serial engine clock, set by its RCG from GPLL0's even output (Source),
// and needing the wrapper's core clocks.
#define SE_CLOCK(ClockId, Reg, Bit, Branch, Rcg_, Source, Core2x, Core)        \
  {                                                                            \
    .Id = (ClockId), .VoteReg = (Reg), .VoteBit = (Bit), .Cbcr = (Branch),     \
    .Rcg = (Rcg_), .RcgSrc = (Source), .Needs = {(Core2x), (Core)}             \
  }

// SM8550: pinctrl-sm8550.c and gcc-sm8550.c, from the Retroid Pocket's
// uart15 (QUP2 SE7, pins 74 and 75).

STATIC CONST GAMEPAD_SOC_FUNC mSm8550Funcs[] = {
  {"qup2_se7", 74, 2},
  {"qup2_se7", 75, 2},
};

#define SM8550_QUPV3_WRAP2_CORE_2X 109
#define SM8550_QUPV3_WRAP2_CORE    110
#define SM8550_QUPV3_WRAP2_S7      125
#define SM8550_QUPV3_WRAP_2_M_AHB  129
#define SM8550_QUPV3_WRAP_2_S_AHB  130

STATIC CONST GAMEPAD_SOC_CLOCK mSm8550Clocks[] = {
  BRANCH (SM8550_QUPV3_WRAP2_CORE_2X, 0x52010, 3, 0x23004),
  BRANCH (SM8550_QUPV3_WRAP2_CORE, 0x52010, 0, 0x233d4),
  SE_CLOCK (
    SM8550_QUPV3_WRAP2_S7,
    0x52010,
    17,
    0x1e88c,
    0x1e898,
    6,
    SM8550_QUPV3_WRAP2_CORE_2X,
    SM8550_QUPV3_WRAP2_CORE
  ),
  BRANCH (SM8550_QUPV3_WRAP_2_M_AHB, 0x52010, 2, 0x233cc),
  BRANCH (SM8550_QUPV3_WRAP_2_S_AHB, 0x52010, 1, 0x233d0),
};

// SM8250: pinctrl-sm8250.c and gcc-sm8250.c, from the Retroid Pocket's
// uart16 (QUP2 SE2, qup16 on pins 48 to 51). Its TLMM is split into
// tiles.

STATIC CONST GAMEPAD_SOC_FUNC mSm8250Funcs[] = {
  {"qup16", 48, 1},
  {"qup16", 49, 1},
  {"qup16", 50, 1},
  {"qup16", 51, 1},
};

#define SM8250_WEST  0
#define SM8250_SOUTH 1
#define SM8250_NORTH 2

STATIC CONST GAMEPAD_SOC_TILE mSm8250Tiles[] = {
  {0, 3, SM8250_SOUTH},    {4, 23, SM8250_NORTH},    {24, 27, SM8250_SOUTH},
  {28, 31, SM8250_NORTH},  {32, 65, SM8250_SOUTH},   {66, 68, SM8250_NORTH},
  {69, 76, SM8250_SOUTH},  {77, 84, SM8250_NORTH},   {85, 91, SM8250_SOUTH},
  {92, 124, SM8250_NORTH}, {125, 132, SM8250_SOUTH},
};

#define SM8250_QUPV3_WRAP2_CORE_2X 117
#define SM8250_QUPV3_WRAP2_CORE    118
#define SM8250_QUPV3_WRAP2_S2      123
#define SM8250_QUPV3_WRAP_2_M_AHB  135
#define SM8250_QUPV3_WRAP_2_S_AHB  136

STATIC CONST GAMEPAD_SOC_CLOCK mSm8250Clocks[] = {
  BRANCH (SM8250_QUPV3_WRAP2_CORE_2X, 0x52010, 3, 0x23278),
  BRANCH (SM8250_QUPV3_WRAP2_CORE, 0x52010, 0, 0x23270),
  SE_CLOCK (
    SM8250_QUPV3_WRAP2_S2,
    0x52010,
    6,
    0x1e26c,
    0x1e270,
    6,
    SM8250_QUPV3_WRAP2_CORE_2X,
    SM8250_QUPV3_WRAP2_CORE
  ),
  BRANCH (SM8250_QUPV3_WRAP_2_M_AHB, 0x52010, 2, 0x1e004),
  BRANCH (SM8250_QUPV3_WRAP_2_S_AHB, 0x52010, 1, 0x1e008),
};

CONST GAMEPAD_SOC gGamepadSocs[] = {
  {
    .Name       = "SM8550",
    .Tlmm       = "qcom,sm8550-tlmm",
    .Gcc        = "qcom,sm8550-gcc",
    .ChipIds    = {519, 601, 603, 604},
    .ChipNames  = {"SM8550", "QCS8550"},
    .TlmmStride = 0x1000,
    .Funcs      = mSm8550Funcs,
    .FuncCount  = COUNT_OF (mSm8550Funcs),
    .Clocks     = mSm8550Clocks,
    .ClockCount = COUNT_OF (mSm8550Clocks),
  },
  {
    .Name       = "SM8250",
    .Tlmm       = "qcom,sm8250-pinctrl",
    .Gcc        = "qcom,gcc-sm8250",
    .ChipIds    = {356, 455},
    .ChipNames  = {"SM8250", "QRB5165"},
    .TlmmStride = 0x1000,
    .TileNames  = {"west", "south", "north"},
    .Tiles      = mSm8250Tiles,
    .TileCount  = COUNT_OF (mSm8250Tiles),
    .Funcs      = mSm8250Funcs,
    .FuncCount  = COUNT_OF (mSm8250Funcs),
    .Clocks     = mSm8250Clocks,
    .ClockCount = COUNT_OF (mSm8250Clocks),
  },
};

CONST UINTN gGamepadSocCount = COUNT_OF (gGamepadSocs);

CONST GAMEPAD_SOC *
GamepadSocOf (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        TlmmNode,
  IN UINTN        GccNode,
  OUT UINT8      *Index
)
{
  for (UINTN At = 0; At < gGamepadSocCount; At++) {
    if (DtIsCompatible (Fdt, Size, TlmmNode, gGamepadSocs[At].Tlmm) &&
        DtIsCompatible (Fdt, Size, GccNode, gGamepadSocs[At].Gcc)) {
      *Index = (UINT8)At;
      return &gGamepadSocs[At];
    }
  }
  return NULL;
}

BOOLEAN
GamepadSocIsChip (
  IN CONST GAMEPAD_SOC *Soc,
  IN UINT32             ChipId,
  IN CONST CHAR8       *Name OPTIONAL
)
{
  OABL_TEXT Trimmed = TextTrim (TextOf (Name));

  for (UINTN At = 0; At < COUNT_OF (Soc->ChipIds); At++) {
    if (ChipId && Soc->ChipIds[At] == ChipId) {
      return TRUE;
    }
  }
  for (UINTN At = 0; At < COUNT_OF (Soc->ChipNames); At++) {
    if (Soc->ChipNames[At] && TextStartsWith (Trimmed, Soc->ChipNames[At])) {
      return TRUE;
    }
  }
  return FALSE;
}

BOOLEAN
GamepadSocMux (
  IN CONST GAMEPAD_SOC *Soc,
  IN OABL_TEXT          Function,
  IN UINT16             Gpio,
  OUT UINT8            *Mux
)
{
  if (TextEqStr (Function, "gpio")) {
    *Mux = 0;
    return TRUE;
  }
  for (UINTN At = 0; At < Soc->FuncCount; At++) {
    if (Soc->Funcs[At].Gpio == Gpio &&
        TextEqStr (Function, Soc->Funcs[At].Name)) {
      *Mux = Soc->Funcs[At].Mux;
      return TRUE;
    }
  }
  return FALSE;
}

UINT8
GamepadSocTile (
  IN CONST GAMEPAD_SOC *Soc,
  IN UINT16             Gpio
)
{
  for (UINTN At = 0; At < Soc->TileCount; At++) {
    if (Gpio >= Soc->Tiles[At].First && Gpio <= Soc->Tiles[At].Last) {
      return Soc->Tiles[At].Tile;
    }
  }
  return 0;
}

CONST GAMEPAD_SOC_CLOCK *
GamepadSocClock (
  IN CONST GAMEPAD_SOC *Soc,
  IN UINT16             Id
)
{
  for (UINTN At = 0; At < Soc->ClockCount; At++) {
    if (Soc->Clocks[At].Id == Id) {
      return &Soc->Clocks[At];
    }
  }
  return NULL;
}
