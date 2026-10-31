/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  RsInput - the RSInput gamepad MCU on a Qualcomm GENI UART, the parts that
  need no firmware.
*/

#include "Core/RsInput.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/DeviceTree.h"

#define COUNT_OF(Array) (sizeof (Array) / sizeof (*(Array)))

// Wire protocol

STATIC CONST UINT8 mHead[4] = {0xA5, 0xD3, 0x5A, 0x3D};

#define POS_SEQ  4
#define POS_CMD  5
#define POS_LEN  6
#define POS_DATA 8

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

UINTN
RsInputFrame (
  IN UINT8        Sequence,
  IN UINT8        Command,
  IN CONST UINT8 *Payload OPTIONAL,
  IN UINTN        Length,
  OUT UINT8       Out[RSINPUT_FRAME_MAX]
)
{
  UINTN Total = RSINPUT_FRAME_OVERHEAD + Length;

  if (Length > RSINPUT_PAYLOAD_MAX || (Length && !Payload)) {
    return 0;
  }
  CopyMem (Out, mHead, sizeof (mHead));
  Out[POS_SEQ] = Sequence;
  Out[POS_CMD] = Command;
  WriteLe16 (Out + POS_LEN, (UINT16)Length);
  if (Length) {
    CopyMem (Out + POS_DATA, Payload, Length);
  }
  Out[Total - 1] = Checksum (Out, Total);
  return Total;
}

// The version request, the report parameters both drivers set, and the
// two other subcommands the Retroid driver sends, whose meaning it doesn't
// say.
STATIC CONST UINT8 mVersionRequest[] = {RSINPUT_COMMOD_VERSION};
STATIC CONST UINT8 mSetParameters[]  = {
  RSINPUT_COMMOD_SET_PAR, 0x01, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x07,
};
STATIC CONST UINT8 mRetroid07[] = {0x07, 0x01};
STATIC CONST UINT8 mRetroid06[] = {0x06};

#define COMMAND(Bytes) {Bytes, sizeof (Bytes)}

STATIC CONST RSINPUT_INIT mInits[RSINPUT_DRIVER_COUNT] = {
  [RSINPUT_DRIVER_RSINPUT] =
    {20, 50, 50, 2, {COMMAND (mVersionRequest), COMMAND (mSetParameters)}},
  [RSINPUT_DRIVER_RETROID] = {100,
                              100,
                              100,
                              6,
                              {COMMAND (mRetroid07), COMMAND (mRetroid06),
                               COMMAND (mVersionRequest),
                               COMMAND (mSetParameters), COMMAND (mRetroid06),
                               COMMAND (mVersionRequest)}},
};

CONST RSINPUT_INIT *
RsInputInit (IN UINT8 Driver)
{
  return &mInits[Driver < RSINPUT_DRIVER_COUNT ? Driver : 0];
}

// Linux reports a stick as the negated raw value.
STATIC INT32
Axis (IN CONST UINT8 *Bytes)
{
  return -(INT32)(INT16)ReadLe16 (Bytes);
}

// Takes one whole, checked frame.
STATIC BOOLEAN
Take (
  IN CONST UINT8        *Frame,
  IN UINTN               Length,
  IN OUT RSINPUT_REPORT *Report
)
{
  CONST UINT8 *Data = Frame + POS_DATA;

  if (Frame[POS_CMD] == RSINPUT_CMD_STATUS && Length >= 6) {
    Report->Seen    = TRUE;
    Report->Buttons = ReadLe16 (Data);
    if (Length >= 10) {
      Report->LeftX = Axis (Data + 6);
      Report->LeftY = Axis (Data + 8);
    }
    return TRUE;
  }
  if (Frame[POS_CMD] == RSINPUT_CMD_COMMOD && Length >= 1 &&
      Data[0] == RSINPUT_COMMOD_VERSION) {
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
  return FALSE;
}

UINTN
RsInputRxFeed (
  IN OUT RSINPUT_RX     *Rx,
  IN CONST UINT8        *Bytes,
  IN UINTN               Count,
  IN OUT RSINPUT_REPORT *Report
)
{
  UINTN Statuses = 0;
  UINTN At       = 0;

  // Keep the newest bytes when more come than fit.
  if (Count > sizeof (Rx->Buf)) {
    Bytes += Count - sizeof (Rx->Buf);
    Count   = sizeof (Rx->Buf);
    Rx->Len = 0;
  }
  if (Rx->Len + Count > sizeof (Rx->Buf)) {
    UINTN Drop = Rx->Len + Count - sizeof (Rx->Buf);
    CopyMem (Rx->Buf, Rx->Buf + Drop, Rx->Len - Drop);
    Rx->Len -= Drop;
  }
  CopyMem (Rx->Buf + Rx->Len, Bytes, Count);
  Rx->Len += Count;

  while (Rx->Len - At >= RSINPUT_FRAME_OVERHEAD) {
    CONST UINT8 *Frame = Rx->Buf + At;
    UINTN        Length;

    if (CompareMem (Frame, mHead, sizeof (mHead)) != 0) {
      At++;
      continue;
    }
    Length = ReadLe16 (Frame + POS_LEN);
    if (Length > RSINPUT_PAYLOAD_MAX) {
      Rx->Bad++;
      At++;
      continue;
    }
    if (Rx->Len - At < RSINPUT_FRAME_OVERHEAD + Length) {
      break;
    }
    if (Checksum (Frame, RSINPUT_FRAME_OVERHEAD + Length) !=
        Frame[RSINPUT_FRAME_OVERHEAD + Length - 1]) {
      Rx->Bad++;
      At++;
      continue;
    }
    if (Take (Frame, Length, Report)) {
      Statuses++;
    }
    At += RSINPUT_FRAME_OVERHEAD + Length;
  }
  CopyMem (Rx->Buf, Rx->Buf + At, Rx->Len - At);
  Rx->Len -= At;
  return Statuses;
}

// Configuration

VOID
RsInputConfigEncode (
  IN CONST RSINPUT_CONFIG *Config,
  OUT UINT8                Out[RSINPUT_CONFIG_BYTES]
)
{
  Out[0] = RSINPUT_CONFIG_VERSION;
  CopyMem (Out + 1, Config, sizeof (*Config));
}

BOOLEAN
RsInputConfigDecode (
  IN CONST UINT8     *Bytes,
  IN UINTN            Size,
  OUT RSINPUT_CONFIG *Config
)
{
  if (Size != RSINPUT_CONFIG_BYTES || Bytes[0] != RSINPUT_CONFIG_VERSION) {
    return FALSE;
  }
  CopyMem (Config, Bytes + 1, sizeof (*Config));
  return Config->Soc < RSINPUT_SOC_COUNT &&
         Config->Driver < RSINPUT_DRIVER_COUNT &&
         Config->PinCount <= RSINPUT_PINS_MAX &&
         Config->ClockCount <= RSINPUT_CLOCKS_MAX &&
         Config->AlwaysOnCount <= RSINPUT_ALWAYS_ON_MAX;
}

// SoC tables

// No clock: clock ids are below this.
#define NO_CLOCK 0xFFFFu

// A pin function's mux number on one pin, from the kernel's pinctrl
// driver. "gpio" is 0 on every pin and isn't listed.
typedef struct _SOC_FUNC {
  CONST CHAR8 *Name;
  UINT16       Gpio;
  UINT8        Mux;
} SOC_FUNC;

// A GCC branch clock, from the kernel's clock driver: its vote bit, its
// branch register (bit 31 is CLK_OFF), and for a serial engine clock its
// RCG and the RCG's GPLL0 even source. Needs are clocks the wrapper also
// needs that its devicetree node doesn't list (they are interconnect votes
// in Linux).
typedef struct _SOC_CLOCK {
  UINT16 Id;
  UINT32 VoteReg;
  UINT8  VoteBit;
  UINT32 Cbcr;
  UINT32 Rcg;
  UINT8  RcgSrc;
  UINT16 Needs[2];
} SOC_CLOCK;

// Pins First to Last are in the TLMM tile Tile.
typedef struct _SOC_TILE {
  UINT16 First;
  UINT16 Last;
  UINT8  Tile;
} SOC_TILE;

typedef struct _SOC_TABLE {
  CONST CHAR8 *Tlmm;
  CONST CHAR8 *Gcc;
  // Bytes between pins' registers.
  UINT32 TlmmStride;
  // The TLMM's reg-names, when it has several tiles, and the tile of each
  // pin not in tile 0.
  CONST CHAR8     *TileNames[RSINPUT_TILES_MAX];
  CONST SOC_TILE  *Tiles;
  UINTN            TileCount;
  CONST CHAR8     *ChipNames[2];
  UINT32           ChipIds[4];
  CONST SOC_FUNC  *Funcs;
  UINTN            FuncCount;
  CONST SOC_CLOCK *Clocks;
  UINTN            ClockCount;
} SOC_TABLE;

// SM8550: pinctrl-sm8550.c and gcc-sm8550.c. The Retroid Pocket's gamepad
// is on QUP2 SE7 (uart15), pins 74 and 75. Mux 0 is gpio, so the first
// function PINGROUP lists is mux 1: qup2_se7 comes second on both pins.
STATIC CONST SOC_FUNC mSm8550Funcs[] = {
  {"qup2_se7", 74, 2},
  {"qup2_se7", 75, 2},
};

#define SM8550_QUPV3_WRAP2_CORE_2X 109
#define SM8550_QUPV3_WRAP2_CORE    110

STATIC CONST SOC_CLOCK mSm8550Clocks[] = {
  {SM8550_QUPV3_WRAP2_CORE_2X, 0x52010, 3, 0x23004, 0, 0, {NO_CLOCK, NO_CLOCK}},
  {SM8550_QUPV3_WRAP2_CORE, 0x52010, 0, 0x233d4, 0, 0, {NO_CLOCK, NO_CLOCK}},
  // GCC_QUPV3_WRAP2_S7_CLK, from gcc_qupv3_wrap2_s7_clk_src.
  {125,
   0x52010,
   17,
   0x1e88c,
   0x1e898,
   6,
   {SM8550_QUPV3_WRAP2_CORE_2X, SM8550_QUPV3_WRAP2_CORE}},
  // GCC_QUPV3_WRAP_2_M_AHB_CLK and _S_AHB_CLK.
  {129, 0x52010, 2, 0x233cc, 0, 0, {NO_CLOCK, NO_CLOCK}},
  {130, 0x52010, 1, 0x233d0, 0, 0, {NO_CLOCK, NO_CLOCK}},
};

// SM8250: pinctrl-sm8250.c and gcc-sm8250.c. The Retroid Pocket's gamepad
// is on QUP2 SE2 (uart16), function qup16 on pins 48 to 51, all mux 1.
STATIC CONST SOC_FUNC mSm8250Funcs[] = {
  {"qup16", 48, 1},
  {"qup16", 49, 1},
  {"qup16", 50, 1},
  {"qup16", 51, 1},
};

#define SM8250_TILE_WEST  0
#define SM8250_TILE_SOUTH 1
#define SM8250_TILE_NORTH 2

STATIC CONST SOC_TILE mSm8250Tiles[] = {
  {0, 3, SM8250_TILE_SOUTH},     {4, 23, SM8250_TILE_NORTH},
  {24, 27, SM8250_TILE_SOUTH},   {28, 31, SM8250_TILE_NORTH},
  {32, 65, SM8250_TILE_SOUTH},   {66, 68, SM8250_TILE_NORTH},
  {69, 76, SM8250_TILE_SOUTH},   {77, 84, SM8250_TILE_NORTH},
  {85, 91, SM8250_TILE_SOUTH},   {92, 124, SM8250_TILE_NORTH},
  {125, 132, SM8250_TILE_SOUTH},
};

#define SM8250_QUPV3_WRAP2_CORE_2X 117
#define SM8250_QUPV3_WRAP2_CORE    118

STATIC CONST SOC_CLOCK mSm8250Clocks[] = {
  {SM8250_QUPV3_WRAP2_CORE_2X, 0x52010, 3, 0x23278, 0, 0, {NO_CLOCK, NO_CLOCK}},
  {SM8250_QUPV3_WRAP2_CORE, 0x52010, 0, 0x23270, 0, 0, {NO_CLOCK, NO_CLOCK}},
  // GCC_QUPV3_WRAP2_S2_CLK, from gcc_qupv3_wrap2_s2_clk_src.
  {123,
   0x52010,
   6,
   0x1e26c,
   0x1e270,
   6,
   {SM8250_QUPV3_WRAP2_CORE_2X, SM8250_QUPV3_WRAP2_CORE}},
  // GCC_QUPV3_WRAP_2_M_AHB_CLK and _S_AHB_CLK.
  {135, 0x52010, 2, 0x1e004, 0, 0, {NO_CLOCK, NO_CLOCK}},
  {136, 0x52010, 1, 0x1e008, 0, 0, {NO_CLOCK, NO_CLOCK}},
};

STATIC CONST SOC_TABLE mSocs[RSINPUT_SOC_COUNT] = {
  [RSINPUT_SOC_SM8550] =
    {
      "qcom,sm8550-tlmm",
      "qcom,sm8550-gcc",
      0x1000,
      {NULL},
      NULL,
      0,
      {"SM8550", "QCS8550"},
      {519, 601, 603, 604},
      mSm8550Funcs,
      COUNT_OF (mSm8550Funcs),
      mSm8550Clocks,
      COUNT_OF (mSm8550Clocks),
    },
  [RSINPUT_SOC_SM8250] =
    {
      "qcom,sm8250-pinctrl",
      "qcom,gcc-sm8250",
      0x1000,
      {"west", "south", "north"},
      mSm8250Tiles,
      COUNT_OF (mSm8250Tiles),
      {"SM8250", "QRB5165"},
      {356, 455},
      mSm8250Funcs,
      COUNT_OF (mSm8250Funcs),
      mSm8250Clocks,
      COUNT_OF (mSm8250Clocks),
    },
};

STATIC CONST SOC_CLOCK *
ClockOf (
  IN CONST SOC_TABLE *Soc,
  IN UINT16           Id
)
{
  for (UINTN Index = 0; Index < Soc->ClockCount; Index++) {
    if (Soc->Clocks[Index].Id == Id) {
      return &Soc->Clocks[Index];
    }
  }
  return NULL;
}

BOOLEAN
RsInputSocIsChip (
  IN UINT8        Soc,
  IN UINT32       ChipId,
  IN CONST CHAR8 *Name OPTIONAL
)
{
  CONST SOC_TABLE *Table;
  OABL_TEXT        Trimmed;

  if (Soc >= RSINPUT_SOC_COUNT) {
    return FALSE;
  }
  Table = &mSocs[Soc];
  for (UINTN Index = 0; Index < COUNT_OF (Table->ChipIds); Index++) {
    if (ChipId && Table->ChipIds[Index] == ChipId) {
      return TRUE;
    }
  }
  Trimmed = TextTrim (TextOf (Name));
  for (UINTN Index = 0; Index < COUNT_OF (Table->ChipNames); Index++) {
    if (Table->ChipNames[Index] &&
        TextStartsWith (Trimmed, Table->ChipNames[Index])) {
      return TRUE;
    }
  }
  return FALSE;
}

// Reading the devicetree

CONST CHAR8 *
RsInputDtResultName (IN RSINPUT_DT_RESULT Result)
{
  switch (Result) {
  case RSINPUT_DT_OK:
    return "ok";
  case RSINPUT_DT_NONE:
    return "no rsinput node";
  case RSINPUT_DT_NOT_ON_GENI:
    return "not on a GENI UART";
  case RSINPUT_DT_UNKNOWN_SOC:
    return "no table for this SoC";
  case RSINPUT_DT_NOT_IN_TABLE:
    return "a pin function or clock isn't in the SoC's table";
  case RSINPUT_DT_BAD:
    break;
  }
  return "a property is missing or malformed";
}

// The tree being read and what is known so far.
typedef struct _DT_READ {
  CONST UINT8     *Fdt;
  UINTN            Size;
  CONST SOC_TABLE *Soc;
  UINTN            TlmmNode;
  UINTN            GccNode;
  RSINPUT_CONFIG  *Config;
} DT_READ;

STATIC BOOLEAN
Prop (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  IN CONST CHAR8   *Name,
  OUT OABL_BYTES   *Value
)
{
  return DtPropAt (R->Fdt, R->Size, Node, Name, Value);
}

STATIC BOOLEAN
PropU32 (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  IN CONST CHAR8   *Name,
  OUT UINT32       *Value
)
{
  OABL_BYTES Bytes;

  if (!Prop (R, Node, Name, &Bytes) || Bytes.Len != 4) {
    return FALSE;
  }
  *Value = ReadBe32 (Bytes.Ptr);
  return TRUE;
}

// The node a phandle cell points to.
STATIC BOOLEAN
Deref (
  IN CONST DT_READ *R,
  IN CONST UINT8   *Cell,
  OUT UINTN        *Node
)
{
  return DtNodeByPhandle (R->Fdt, R->Size, ReadBe32 (Cell), Node);
}

// The address of entry Index of Node's reg, read with its parent's
// #address-cells and #size-cells. Assumes identity ranges up the tree, as on
// these SoCs.
STATIC BOOLEAN
RegAt (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  IN UINTN          Index,
  OUT UINT64       *Base
)
{
  UINTN      Parent;
  UINT32     Cells     = 2;
  UINT32     SizeCells = 1;
  UINTN      At;
  OABL_BYTES Reg;

  if (!DtParent (R->Fdt, R->Size, Node, &Parent)) {
    return FALSE;
  }
  PropU32 (R, Parent, "#address-cells", &Cells);
  PropU32 (R, Parent, "#size-cells", &SizeCells);
  At = Index * (Cells + SizeCells) * 4;
  if ((Cells != 1 && Cells != 2) || SizeCells > 2 ||
      !Prop (R, Node, "reg", &Reg) || Reg.Len < At + Cells * 4) {
    return FALSE;
  }
  *Base = Cells == 2 ? ReadBe64 (Reg.Ptr + At) : ReadBe32 (Reg.Ptr + At);
  return TRUE;
}

STATIC BOOLEAN
RegBase (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  OUT UINT64       *Base
)
{
  return RegAt (R, Node, 0, Base);
}

// Reads the TLMM's tiles, in the order of the SoC table's names, or its one
// region.
STATIC BOOLEAN
TlmmTiles (
  IN CONST DT_READ   *R,
  OUT RSINPUT_CONFIG *Config
)
{
  OABL_BYTES Names;

  if (!R->Soc->TileNames[0]) {
    return RegBase (R, R->TlmmNode, &Config->Tlmm[0]);
  }
  if (!Prop (R, R->TlmmNode, "reg-names", &Names)) {
    return FALSE;
  }
  for (UINTN Tile = 0; Tile < RSINPUT_TILES_MAX && R->Soc->TileNames[Tile];
       Tile++) {
    UINTN Entry = 0;
    UINTN At    = 0;

    // Find the name's position in reg-names.
    while (At < Names.Len) {
      UINTN End = At;

      while (End < Names.Len && Names.Ptr[End]) {
        End++;
      }
      if (TextEqStr (
            (OABL_TEXT){(CONST CHAR8 *)Names.Ptr + At, End - At},
            R->Soc->TileNames[Tile]
          )) {
        break;
      }
      At = End + 1;
      Entry++;
    }
    if (At >= Names.Len ||
        !RegAt (R, R->TlmmNode, Entry, &Config->Tlmm[Tile])) {
      return FALSE;
    }
  }
  return TRUE;
}

// The first string of a property.
STATIC BOOLEAN
PropText (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  IN CONST CHAR8   *Name,
  OUT OABL_TEXT    *Text
)
{
  OABL_BYTES Bytes;
  UINTN      Length = 0;

  if (!Prop (R, Node, Name, &Bytes)) {
    return FALSE;
  }
  while (Length < Bytes.Len && Bytes.Ptr[Length]) {
    Length++;
  }
  if (Length == Bytes.Len) {
    return FALSE;
  }
  *Text = (OABL_TEXT){(CONST CHAR8 *)Bytes.Ptr, Length};
  return TRUE;
}

// The mux number of Function on Gpio.
STATIC BOOLEAN
MuxOf (
  IN CONST SOC_TABLE *Soc,
  IN OABL_TEXT        Function,
  IN UINT16           Gpio,
  OUT UINT8          *Mux
)
{
  if (TextEqStr (Function, "gpio")) {
    *Mux = 0;
    return TRUE;
  }
  for (UINTN Index = 0; Index < Soc->FuncCount; Index++) {
    if (Soc->Funcs[Index].Gpio == Gpio &&
        TextEqStr (Function, Soc->Funcs[Index].Name)) {
      *Mux = Soc->Funcs[Index].Mux;
      return TRUE;
    }
  }
  return FALSE;
}

// Parses "gpioN".
STATIC BOOLEAN
GpioOfPin (
  IN OABL_TEXT Pin,
  OUT UINT16  *Gpio
)
{
  UINT64 Number;

  if (!TextStartsWith (Pin, "gpio") ||
      !TextParseU64 (TextSub (Pin, 4, Pin.Len), 10, &Number) ||
      Number > 0x3ff) {
    return FALSE;
  }
  *Gpio = (UINT16)Number;
  return TRUE;
}

// Adds or replaces the settings of Gpio.
STATIC BOOLEAN
AddPin (
  IN OUT RSINPUT_CONFIG *Config,
  IN RSINPUT_PIN         Pin
)
{
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    if (Config->Pins[Index].Gpio == Pin.Gpio) {
      Config->Pins[Index] = Pin;
      return TRUE;
    }
  }
  if (Config->PinCount == RSINPUT_PINS_MAX) {
    return FALSE;
  }
  Config->Pins[Config->PinCount++] = Pin;
  return TRUE;
}

// Reads one pin group: pins, function, bias and drive strength.
STATIC RSINPUT_DT_RESULT
PinGroup (
  IN CONST DT_READ *R,
  IN UINTN          Node
)
{
  OABL_BYTES  Pins;
  OABL_BYTES  Unused;
  OABL_TEXT   Function;
  UINT32      Drive = 2;
  RSINPUT_PIN Pin   = {0};

  if (!Prop (R, Node, "pins", &Pins) ||
      !PropText (R, Node, "function", &Function)) {
    return RSINPUT_DT_BAD;
  }
  PropU32 (R, Node, "drive-strength", &Drive);
  if (Drive < 2 || Drive > 16 || Drive % 2) {
    return RSINPUT_DT_BAD;
  }
  Pin.DriveMa = (UINT8)Drive;
  Pin.Pull    = Prop (R, Node, "bias-pull-up", &Unused)     ? RSINPUT_PULL_UP
                : Prop (R, Node, "bias-pull-down", &Unused) ? RSINPUT_PULL_DOWN
                : Prop (R, Node, "bias-bus-hold", &Unused) ? RSINPUT_PULL_KEEPER
                                                           : RSINPUT_PULL_NONE;
  for (UINTN At = 0; At < Pins.Len;) {
    UINTN End = At;

    while (End < Pins.Len && Pins.Ptr[End]) {
      End++;
    }
    if (End == Pins.Len) {
      return RSINPUT_DT_BAD;
    }
    if (!GpioOfPin (
          (OABL_TEXT){(CONST CHAR8 *)Pins.Ptr + At, End - At}, &Pin.Gpio
        )) {
      return RSINPUT_DT_NOT_IN_TABLE;
    }
    if (!MuxOf (R->Soc, Function, Pin.Gpio, &Pin.Func)) {
      return RSINPUT_DT_NOT_IN_TABLE;
    }
    if (!AddPin (R->Config, Pin)) {
      return RSINPUT_DT_BAD;
    }
    At = End + 1;
  }
  return RSINPUT_DT_OK;
}

// Reads the TLMM states Node's pinctrl-0 points to. A state holds pins
// itself, or in subnodes. Nodes without pinctrl-0 have none.
STATIC RSINPUT_DT_RESULT
PinStates (
  IN CONST DT_READ *R,
  IN UINTN          Node
)
{
  OABL_BYTES States;

  if (!Prop (R, Node, "pinctrl-0", &States)) {
    return RSINPUT_DT_OK;
  }
  for (UINTN At = 0; At + 4 <= States.Len; At += 4) {
    UINTN             State;
    UINTN             Parent;
    OABL_BYTES        Unused;
    DT_NODES          Groups;
    RSINPUT_DT_RESULT Result = RSINPUT_DT_OK;

    if (!Deref (R, States.Ptr + At, &State) ||
        !DtParent (R->Fdt, R->Size, State, &Parent) || Parent != R->TlmmNode) {
      return RSINPUT_DT_BAD;
    }
    if (Prop (R, State, "pins", &Unused)) {
      Result = PinGroup (R, State);
    } else if (DtNodeChildren (R->Fdt, R->Size, State, &Groups)) {
      for (UINTN Index = 0; Index < Groups.Count && !Result; Index++) {
        Result = PinGroup (R, Groups.Items[Index]);
      }
      VecFree (&Groups);
    } else {
      Result = RSINPUT_DT_BAD;
    }
    if (Result) {
      return Result;
    }
  }
  return RSINPUT_DT_OK;
}

// Reads a TLMM GPIO specifier: the TLMM's phandle, the pin and its flags.
STATIC BOOLEAN
GpioSpec (
  IN CONST DT_READ *R,
  IN OABL_BYTES     Spec,
  OUT RSINPUT_LINE *Line
)
{
  UINTN Node;

  if (Spec.Len != 12 || !Deref (R, Spec.Ptr, &Node) || Node != R->TlmmNode ||
      ReadBe32 (Spec.Ptr + 4) > 0x3ff) {
    return FALSE;
  }
  Line->Present   = TRUE;
  Line->Gpio      = (UINT16)ReadBe32 (Spec.Ptr + 4);
  Line->ActiveLow = (ReadBe32 (Spec.Ptr + 8) & 1) != 0;
  return TRUE;
}

// Reads Name-gpios (or the older Name-gpio). Absent is fine.
STATIC BOOLEAN
Gpio (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  IN CONST CHAR8   *Name,
  OUT RSINPUT_LINE *Line
)
{
  CHAR8      Property[32];
  OABL_BYTES Spec;

  *Line = (RSINPUT_LINE){0};
  AsciiStrCopy (Property, sizeof (Property), Name);
  AsciiStrAppend (Property, sizeof (Property), "-gpios");
  if (Prop (R, Node, Property, &Spec)) {
    return GpioSpec (R, Spec, Line);
  }
  Property[AsciiStrLen (Property) - 1] = 0;
  if (Prop (R, Node, Property, &Spec)) {
    return GpioSpec (R, Spec, Line);
  }
  return TRUE;
}

// Reads vdd-supply when it is a fixed regulator switched by a TLMM GPIO.
// Other supplies are left as the firmware set them.
STATIC BOOLEAN
Supply (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  OUT RSINPUT_LINE *Line
)
{
  OABL_BYTES Phandle;
  OABL_BYTES Spec;
  OABL_BYTES Unused;
  UINTN      Regulator;

  *Line = (RSINPUT_LINE){0};
  if (!Prop (R, Node, "vdd-supply", &Phandle)) {
    return TRUE;
  }
  if (Phandle.Len != 4 || !Deref (R, Phandle.Ptr, &Regulator)) {
    return FALSE;
  }
  if (!DtIsCompatible (R->Fdt, R->Size, Regulator, "regulator-fixed") ||
      !(Prop (R, Regulator, "gpio", &Spec) ||
        Prop (R, Regulator, "gpios", &Spec))) {
    return TRUE;
  }
  if (!GpioSpec (R, Spec, Line)) {
    return FALSE;
  }
  // A fixed regulator's enable is active low unless it says otherwise.
  Line->ActiveLow = !Prop (R, Regulator, "enable-active-high", &Unused);
  return TRUE;
}

// Reads the enables of always-on fixed regulators on the TLMM.
STATIC BOOLEAN
AlwaysOn (
  IN CONST DT_READ   *R,
  OUT RSINPUT_CONFIG *Config
)
{
  UINTN      Node = DT_START;
  OABL_BYTES Spec;
  OABL_BYTES Unused;

  while (DtNextCompatible (R->Fdt, R->Size, "regulator-fixed", Node, &Node)) {
    RSINPUT_LINE Line;

    if (!DtIsEnabled (R->Fdt, R->Size, Node) ||
        !Prop (R, Node, "regulator-always-on", &Unused) ||
        !(Prop (R, Node, "gpio", &Spec) || Prop (R, Node, "gpios", &Spec)) ||
        !GpioSpec (R, Spec, &Line)) {
      continue;
    }
    if (Config->AlwaysOnCount == RSINPUT_ALWAYS_ON_MAX) {
      return FALSE;
    }
    Line.ActiveLow = !Prop (R, Node, "enable-active-high", &Unused);
    Config->AlwaysOn[Config->AlwaysOnCount++] = Line;
  }
  return TRUE;
}

// Adds Id and the clocks it needs, unless already listed.
STATIC RSINPUT_DT_RESULT
AddClock (
  IN CONST DT_READ *R,
  IN UINT16         Id
)
{
  RSINPUT_CONFIG  *Config = R->Config;
  CONST SOC_CLOCK *Clock  = ClockOf (R->Soc, Id);

  if (!Clock) {
    return RSINPUT_DT_NOT_IN_TABLE;
  }
  for (UINTN Index = 0; Index < COUNT_OF (Clock->Needs); Index++) {
    if (Clock->Needs[Index] != NO_CLOCK) {
      RSINPUT_DT_RESULT Result = AddClock (R, Clock->Needs[Index]);
      if (Result) {
        return Result;
      }
    }
  }
  for (UINTN Index = 0; Index < Config->ClockCount; Index++) {
    if (Config->Clocks[Index] == Id) {
      return RSINPUT_DT_OK;
    }
  }
  if (Config->ClockCount == RSINPUT_CLOCKS_MAX) {
    return RSINPUT_DT_BAD;
  }
  Config->Clocks[Config->ClockCount++] = Id;
  return RSINPUT_DT_OK;
}

// Adds Node's GCC clocks. *First gets the first one's id.
STATIC RSINPUT_DT_RESULT
Clocks (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  OUT UINT16       *First OPTIONAL
)
{
  OABL_BYTES List;

  if (!Prop (R, Node, "clocks", &List) || List.Len == 0 || List.Len % 8) {
    return RSINPUT_DT_BAD;
  }
  for (UINTN At = 0; At < List.Len; At += 8) {
    UINTN             Gcc;
    UINT32            Id = ReadBe32 (List.Ptr + At + 4);
    RSINPUT_DT_RESULT Result;

    if (!Deref (R, List.Ptr + At, &Gcc) || Gcc != R->GccNode ||
        Id >= NO_CLOCK) {
      return RSINPUT_DT_BAD;
    }
    if ((Result = AddClock (R, (UINT16)Id))) {
      return Result;
    }
    if (First && At == 0) {
      *First = (UINT16)Id;
    }
  }
  return RSINPUT_DT_OK;
}

// Finds the TLMM from the UART's first pin state and the GCC from its first
// clock, and picks the SoC table by their compatibles.
STATIC RSINPUT_DT_RESULT
FindSoc (
  IN OUT DT_READ *R,
  IN UINTN        Uart
)
{
  OABL_BYTES Cells;
  UINTN      State;

  if (!Prop (R, Uart, "pinctrl-0", &Cells) || Cells.Len < 4 ||
      !Deref (R, Cells.Ptr, &State) ||
      !DtParent (R->Fdt, R->Size, State, &R->TlmmNode) ||
      !Prop (R, Uart, "clocks", &Cells) || Cells.Len < 8 ||
      !Deref (R, Cells.Ptr, &R->GccNode)) {
    return RSINPUT_DT_BAD;
  }
  for (UINTN Index = 0; Index < RSINPUT_SOC_COUNT; Index++) {
    if (DtIsCompatible (R->Fdt, R->Size, R->TlmmNode, mSocs[Index].Tlmm) &&
        DtIsCompatible (R->Fdt, R->Size, R->GccNode, mSocs[Index].Gcc)) {
      R->Soc         = &mSocs[Index];
      R->Config->Soc = (UINT8)Index;
      return RSINPUT_DT_OK;
    }
  }
  return RSINPUT_DT_UNKNOWN_SOC;
}

STATIC CONST struct {
  CONST CHAR8 *Compatible;
  UINT8        Driver;
} mCompatibles[] = {
  {"retroidpocket,rsinput", RSINPUT_DRIVER_RSINPUT},
  {"ayntec,rsinput", RSINPUT_DRIVER_RSINPUT},
  {"gamepad,rsinput", RSINPUT_DRIVER_RSINPUT},
  {"retroid,retroid-pocket-gamepad", RSINPUT_DRIVER_RETROID},
  {"ayn,odin2-gamepad", RSINPUT_DRIVER_RETROID},
};

RSINPUT_DT_RESULT
RsInputFromDt (
  IN CONST UINT8     *Fdt,
  IN UINTN            Size,
  OUT RSINPUT_CONFIG *Config
)
{
  DT_READ           R     = {Fdt, Size, NULL, 0, 0, Config};
  BOOLEAN           Found = FALSE;
  UINTN             Pad;
  UINTN             Uart = 0;
  UINTN             Wrapper;
  OABL_BYTES        Unused;
  RSINPUT_DT_RESULT Result;

  ZeroMem (Config, sizeof (*Config));
  for (UINTN Index = 0; Index < COUNT_OF (mCompatibles) && !Found; Index++) {
    Pad = DT_START;
    while (
      !Found &&
      DtNextCompatible (Fdt, Size, mCompatibles[Index].Compatible, Pad, &Pad)) {
      Found = DtIsEnabled (Fdt, Size, Pad);
    }
    Config->Driver = mCompatibles[Index].Driver;
  }
  if (!Found) {
    return RSINPUT_DT_NONE;
  }
  if (!DtParent (Fdt, Size, Pad, &Uart) ||
      !DtIsCompatible (Fdt, Size, Uart, "qcom,geni-uart") ||
      !DtIsEnabled (Fdt, Size, Uart) || !DtParent (Fdt, Size, Uart, &Wrapper) ||
      !DtIsCompatible (Fdt, Size, Wrapper, "qcom,geni-se-qup")) {
    return RSINPUT_DT_NOT_ON_GENI;
  }
  if ((Result = FindSoc (&R, Uart))) {
    return Result;
  }
  if (!RegBase (&R, Uart, &Config->Uart) ||
      !RegBase (&R, Wrapper, &Config->Wrapper) || !TlmmTiles (&R, Config) ||
      !RegBase (&R, R.GccNode, &Config->Gcc)) {
    return RSINPUT_DT_BAD;
  }
  // The wrapper's AHB clocks first, then the engine's.
  if ((Result = Clocks (&R, Wrapper, NULL)) ||
      (Result = Clocks (&R, Uart, &Config->SeClock))) {
    return Result;
  }
  if (!ClockOf (R.Soc, Config->SeClock)->Rcg) {
    return RSINPUT_DT_NOT_IN_TABLE;
  }
  if ((Result = PinStates (&R, Uart)) || (Result = PinStates (&R, Pad))) {
    return Result;
  }
  if (!Supply (&R, Pad, &Config->Supply) || !AlwaysOn (&R, Config) ||
      !Gpio (&R, Pad, "enable", &Config->Enable) ||
      !Gpio (&R, Pad, "reset", &Config->Reset) ||
      !Gpio (&R, Pad, "boot", &Config->Boot)) {
    return RSINPUT_DT_BAD;
  }
  Config->InvertY   = Prop (&R, Pad, "invert-y", &Unused);
  Config->AxisRange = RSINPUT_AXIS_RANGE_DEFAULT;
  PropU32 (&R, Pad, "axis-range", &Config->AxisRange);
  if (!Config->AxisRange) {
    Config->AxisRange = RSINPUT_AXIS_RANGE_DEFAULT;
  }
  return RSINPUT_DT_OK;
}

// TLMM

#define TLMM_CFG_FUNC_SHIFT 2
#define TLMM_CFG_DRV_SHIFT  6
#define TLMM_CFG_OE         (1u << 9)
#define TLMM_IO_OUT         (1u << 1)

STATIC UINT64
TlmmCfg (
  IN CONST RSINPUT_CONFIG *Config,
  IN UINT16                Gpio
)
{
  CONST SOC_TABLE *Soc  = &mSocs[Config->Soc];
  UINT8            Tile = 0;

  for (UINTN Index = 0; Index < Soc->TileCount; Index++) {
    if (Gpio >= Soc->Tiles[Index].First && Gpio <= Soc->Tiles[Index].Last) {
      Tile = Soc->Tiles[Index].Tile;
    }
  }
  return Config->Tlmm[Tile] + (UINT64)Gpio * Soc->TlmmStride;
}

STATIC UINT32
CfgOf (IN CONST RSINPUT_PIN *Pin)
{
  return Pin->Pull | (UINT32)Pin->Func << TLMM_CFG_FUNC_SHIFT |
         (UINT32)(Pin->DriveMa / 2 - 1) << TLMM_CFG_DRV_SHIFT;
}

// Saves Gpio's registers, once.
STATIC VOID
Save (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN OUT RSINPUT_SAVED    *Saved,
  IN UINT16                Gpio
)
{
  UINT64 Cfg = TlmmCfg (Config, Gpio);

  for (UINTN Index = 0; Index < Saved->PinCount; Index++) {
    if (Saved->Gpio[Index] == Gpio) {
      return;
    }
  }
  if (Saved->PinCount < COUNT_OF (Saved->Gpio)) {
    Saved->Gpio[Saved->PinCount] = Gpio;
    Saved->Cfg[Saved->PinCount]  = MmioRead32 (M, Cfg);
    Saved->Io[Saved->PinCount]   = MmioRead32 (M, Cfg + 4);
    Saved->PinCount++;
  }
}

// Drives Line to its active or inactive level as a GPIO output, keeping the
// pad settings the devicetree gave the pin.
STATIC VOID
Drive (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN CONST RSINPUT_LINE   *Line,
  IN BOOLEAN               Active
)
{
  RSINPUT_PIN Pin  = {Line->Gpio, 0, RSINPUT_PULL_NONE, 2};
  UINT64      Cfg  = TlmmCfg (Config, Line->Gpio);
  BOOLEAN     High = Active != Line->ActiveLow;

  if (!Line->Present) {
    return;
  }
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    if (Config->Pins[Index].Gpio == Line->Gpio) {
      Pin      = Config->Pins[Index];
      Pin.Func = 0;
    }
  }
  // The level first, so the pin doesn't glitch when its output turns on.
  MmioWrite32 (M, Cfg + 4, High ? TLMM_IO_OUT : 0);
  MmioWrite32 (M, Cfg, CfgOf (&Pin) | TLMM_CFG_OE);
}

VOID
RsInputPinsOn (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  OUT RSINPUT_SAVED       *Saved
)
{
  CONST RSINPUT_LINE *Lines[] = {
    &Config->Supply,
    &Config->Enable,
    &Config->Reset,
    &Config->Boot,
  };

  ZeroMem (Saved, sizeof (*Saved));
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    Save (M, Config, Saved, Config->Pins[Index].Gpio);
  }
  for (UINTN Index = 0; Index < COUNT_OF (Lines); Index++) {
    if (Lines[Index]->Present) {
      Save (M, Config, Saved, Lines[Index]->Gpio);
    }
  }
  for (UINTN Index = 0; Index < Config->AlwaysOnCount; Index++) {
    Save (M, Config, Saved, Config->AlwaysOn[Index].Gpio);
  }
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    MmioWrite32 (
      M, TlmmCfg (Config, Config->Pins[Index].Gpio),
      CfgOf (&Config->Pins[Index])
    );
  }
  // As Linux: the always-on supplies from the start, then as its probe:
  // supply on, boot and reset released, then enable.
  for (UINTN Index = 0; Index < Config->AlwaysOnCount; Index++) {
    Drive (M, Config, &Config->AlwaysOn[Index], TRUE);
  }
  Drive (M, Config, &Config->Supply, TRUE);
  Drive (M, Config, &Config->Boot, FALSE);
  Drive (M, Config, &Config->Reset, FALSE);
}

VOID
RsInputRelease (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config
)
{
  Drive (M, Config, &Config->Enable, TRUE);
  Drive (M, Config, &Config->Reset, TRUE);
}

// GCC

#define CBCR_CLK_OFF (1u << 31)
#define CBCR_HW_CTL  (1u << 1)
#define RCG_CMD      0x0
#define RCG_CFG      0x4
#define RCG_M        0x8
#define RCG_N        0xc
#define RCG_D        0x10
#define RCG_UPDATE   (1u << 0)
// SRC_DIV, SRC_SEL and MODE.
#define RCG_CFG_MASK  0x371fu
#define RCG_DUAL_EDGE (2u << 12)
#define RCG_MND_MASK  0xffffu

// 7.3728 MHz from GPLL0's 300 MHz even output: 300 * 384 / 15625. With 16x
// oversampling that is 115200 baud at a divider of 4, at 32x one of 2.
#define SE_RATE   7372800u
#define SE_RATE_M 384u
#define SE_RATE_N 15625u

// Polls before a register is taken not to change.
#define POLLS 1000000u

STATIC BOOLEAN
PollClear (
  IN CONST MMIO *M,
  IN UINT64      Addr,
  IN UINT32      Mask
)
{
  for (UINT32 Index = 0; Index < POLLS; Index++) {
    if (!(MmioRead32 (M, Addr) & Mask)) {
      return TRUE;
    }
  }
  return FALSE;
}

// Votes for Clock, saving its vote register's value first.
STATIC BOOLEAN
Vote (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN CONST SOC_CLOCK      *Clock,
  IN OUT RSINPUT_SAVED    *Saved
)
{
  UINT64  Reg   = Config->Gcc + Clock->VoteReg;
  BOOLEAN Known = FALSE;

  for (UINTN Index = 0; Index < Saved->VoteCount && !Known; Index++) {
    Known = Saved->VoteReg[Index] == Clock->VoteReg;
  }
  if (!Known && Saved->VoteCount < COUNT_OF (Saved->VoteReg)) {
    Saved->VoteReg[Saved->VoteCount] = Clock->VoteReg;
    Saved->Vote[Saved->VoteCount]    = MmioRead32 (M, Reg);
    Saved->VoteCount++;
  }
  MmioWrite32 (M, Reg, MmioRead32 (M, Reg) | 1u << Clock->VoteBit);
  // Under hardware gating CLK_OFF follows the bus, not the vote.
  if (MmioRead32 (M, Config->Gcc + Clock->Cbcr) & CBCR_HW_CTL) {
    return TRUE;
  }
  return PollClear (M, Config->Gcc + Clock->Cbcr, CBCR_CLK_OFF);
}

BOOLEAN
RsInputClocksOn (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN OUT RSINPUT_SAVED    *Saved
)
{
  CONST SOC_TABLE *Soc = &mSocs[Config->Soc];
  CONST SOC_CLOCK *Se  = ClockOf (Soc, Config->SeClock);
  UINT64           Rcg;

  for (UINTN Index = 0; Index < Config->ClockCount; Index++) {
    CONST SOC_CLOCK *Clock = ClockOf (Soc, Config->Clocks[Index]);
    if (!Clock || !Vote (M, Config, Clock, Saved)) {
      return FALSE;
    }
  }
  if (!Se || !Se->Rcg) {
    return FALSE;
  }
  Rcg = Config->Gcc + Se->Rcg;
  MmioWrite32 (M, Rcg + RCG_M, SE_RATE_M);
  MmioWrite32 (M, Rcg + RCG_N, ~(SE_RATE_N - SE_RATE_M) & RCG_MND_MASK);
  MmioWrite32 (M, Rcg + RCG_D, ~SE_RATE_N & RCG_MND_MASK);
  MmioWrite32 (
    M, Rcg + RCG_CFG,
    (MmioRead32 (M, Rcg + RCG_CFG) & ~RCG_CFG_MASK) | 1u |
      (UINT32)Se->RcgSrc << 8 | RCG_DUAL_EDGE
  );
  MmioWrite32 (M, Rcg + RCG_CMD, MmioRead32 (M, Rcg + RCG_CMD) | RCG_UPDATE);
  return PollClear (M, Rcg + RCG_CMD, RCG_UPDATE);
}

// GENI serial engine

#define QUP_HW_VER                   0x4
#define GENI_FORCE_DEFAULT_REG       0x20
#define GENI_OUTPUT_CTRL             0x24
#define GENI_CGC_CTRL                0x28
#define SE_GENI_STATUS               0x40
#define GENI_SER_M_CLK_CFG           0x48
#define GENI_SER_S_CLK_CFG           0x4c
#define GENI_FW_REVISION_RO          0x68
#define SE_GENI_CLK_SEL              0x7c
#define SE_UART_LOOPBACK_CFG         0x22c
#define SE_GENI_BYTE_GRAN            0x254
#define SE_GENI_DMA_MODE_EN          0x258
#define SE_UART_TX_TRANS_CFG         0x25c
#define SE_GENI_TX_PACKING_CFG0      0x260
#define SE_GENI_TX_PACKING_CFG1      0x264
#define SE_UART_TX_WORD_LEN          0x268
#define SE_UART_TX_STOP_BIT_LEN      0x26c
#define SE_UART_TX_TRANS_LEN         0x270
#define SE_UART_RX_TRANS_CFG         0x280
#define SE_GENI_RX_PACKING_CFG0      0x284
#define SE_GENI_RX_PACKING_CFG1      0x288
#define SE_UART_RX_WORD_LEN          0x28c
#define SE_UART_RX_STALE_CNT         0x294
#define SE_UART_TX_PARITY_CFG        0x2a4
#define SE_UART_RX_PARITY_CFG        0x2a8
#define SE_GENI_M_CMD0               0x600
#define SE_GENI_M_CMD_CTRL_REG       0x604
#define SE_GENI_M_IRQ_STATUS         0x610
#define SE_GENI_M_IRQ_CLEAR          0x618
#define SE_GENI_S_CMD0               0x630
#define SE_GENI_S_CMD_CTRL_REG       0x634
#define SE_GENI_S_IRQ_STATUS         0x640
#define SE_GENI_S_IRQ_CLEAR          0x648
#define SE_GENI_TX_FIFOn             0x700
#define SE_GENI_RX_FIFOn             0x780
#define SE_GENI_RX_FIFO_STATUS       0x804
#define SE_GENI_RX_WATERMARK_REG     0x810
#define SE_GENI_RX_RFR_WATERMARK_REG 0x814
#define SE_DMA_TX_IRQ_CLR            0xc44
#define SE_DMA_RX_IRQ_CLR            0xd44
#define SE_GSI_EVENT_EN              0xe18
#define SE_IRQ_EN                    0xe1c
#define SE_DMA_GENERAL_CFG           0xe30

#define M_GENI_CMD_ACTIVE        (1u << 0)
#define S_GENI_CMD_ACTIVE        (1u << 12)
#define M_GENI_CMD_ABORT         (1u << 1)
#define S_GENI_CMD_ABORT         (1u << 1)
#define M_CMD_DONE_EN            (1u << 0)
#define OPCODE_SHIFT             27
#define UART_START_TX            1u
#define UART_START_READ          1u
#define UART_CTS_MASK            (1u << 1)
#define SER_CLK_EN               (1u << 0)
#define CLK_DIV_SHIFT            4
#define RX_FIFO_WC_MASK          0x1ffffffu
#define RX_LAST                  (1u << 31)
#define RX_LAST_BYTE_VALID_SHIFT 28
// Four 8-bit words a FIFO entry, least significant byte first.
#define PACKING_CFG0  0x0004380Eu
#define PACKING_CFG1  0x000C3E0Eu
#define BITS_PER_CHAR 8u
// Sixteen 10-bit character times of silence close a partial RX word, as
// in Linux.
#define RX_STALE           (10u * 16u)
#define QUP_SE_VERSION_2_5 0x20050000u
#define BAUD               115200u
#define TX_FIFO_BYTES      64u

UINT32
RsInputUartProtocol (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config
)
{
  return (MmioRead32 (M, Config->Uart + GENI_FW_REVISION_RO) >> 8) & 0xff;
}

// Aborts both sequencers and clears their interrupts.
STATIC VOID
Abort (
  IN CONST MMIO *M,
  IN UINT64      Se
)
{
  MmioWrite32 (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  PollClear (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  MmioWrite32 (M, Se + SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);
  PollClear (M, Se + SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);
  MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
  MmioWrite32 (M, Se + SE_GENI_S_IRQ_CLEAR, 0xffffffffu);
  MmioWrite32 (M, Se + GENI_FORCE_DEFAULT_REG, 1);
}

STATIC VOID
StartRead (
  IN CONST MMIO *M,
  IN UINT64      Se
)
{
  MmioWrite32 (M, Se + SE_GENI_S_CMD0, UART_START_READ << OPCODE_SHIFT);
}

VOID
RsInputUartSetup (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config
)
{
  UINT64 Se       = Config->Uart;
  UINT32 Sampling = 32;

  Abort (M, Se);
  MmioWrite32 (M, Se + SE_GENI_TX_PACKING_CFG0, PACKING_CFG0);
  MmioWrite32 (M, Se + SE_GENI_TX_PACKING_CFG1, PACKING_CFG1);
  MmioWrite32 (M, Se + SE_GENI_RX_PACKING_CFG0, PACKING_CFG0);
  MmioWrite32 (M, Se + SE_GENI_RX_PACKING_CFG1, PACKING_CFG1);
  MmioWrite32 (M, Se + SE_GENI_BYTE_GRAN, 0);
  // Linux's geni_se_init, without interrupts, and FIFO mode.
  MmioWrite32 (M, Se + SE_GSI_EVENT_EN, 0);
  MmioWrite32 (M, Se + SE_DMA_TX_IRQ_CLR, 0xffffffffu);
  MmioWrite32 (M, Se + SE_DMA_RX_IRQ_CLR, 0xffffffffu);
  MmioWrite32 (
    M, Se + GENI_CGC_CTRL, MmioRead32 (M, Se + GENI_CGC_CTRL) | 0x7f
  );
  MmioWrite32 (
    M, Se + SE_DMA_GENERAL_CFG, MmioRead32 (M, Se + SE_DMA_GENERAL_CFG) | 0xf
  );
  MmioWrite32 (M, Se + GENI_OUTPUT_CTRL, 0x7f);
  MmioWrite32 (M, Se + GENI_FORCE_DEFAULT_REG, 1);
  MmioWrite32 (M, Se + SE_IRQ_EN, MmioRead32 (M, Se + SE_IRQ_EN) | 0xf);
  MmioWrite32 (
    M, Se + SE_GENI_DMA_MODE_EN, MmioRead32 (M, Se + SE_GENI_DMA_MODE_EN) & ~1u
  );
  MmioWrite32 (M, Se + SE_GENI_RX_WATERMARK_REG, 2);
  MmioWrite32 (M, Se + SE_GENI_RX_RFR_WATERMARK_REG, 14);
  // 8N1, CTS ignored.
  MmioWrite32 (M, Se + SE_UART_LOOPBACK_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_TX_TRANS_CFG, UART_CTS_MASK);
  MmioWrite32 (M, Se + SE_UART_TX_PARITY_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_RX_TRANS_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_RX_PARITY_CFG, 0);
  MmioWrite32 (M, Se + SE_UART_TX_WORD_LEN, BITS_PER_CHAR);
  MmioWrite32 (M, Se + SE_UART_RX_WORD_LEN, BITS_PER_CHAR);
  MmioWrite32 (M, Se + SE_UART_TX_STOP_BIT_LEN, 0);
  MmioWrite32 (M, Se + SE_UART_RX_STALE_CNT, RX_STALE);
  // The serial clock comes from the RCG's first output, as Linux leaves it.
  MmioWrite32 (M, Se + SE_GENI_CLK_SEL, 0);
  // QUP 2.5 and later sample 16 times a bit, older ones 32.
  if (MmioRead32 (M, Config->Wrapper + QUP_HW_VER) >= QUP_SE_VERSION_2_5) {
    Sampling = 16;
  }
  MmioWrite32 (
    M, Se + GENI_SER_M_CLK_CFG,
    SER_CLK_EN | SE_RATE / (BAUD * Sampling) << CLK_DIV_SHIFT
  );
  MmioWrite32 (
    M, Se + GENI_SER_S_CLK_CFG,
    SER_CLK_EN | SE_RATE / (BAUD * Sampling) << CLK_DIV_SHIFT
  );
  StartRead (M, Se);
}

BOOLEAN
RsInputUartWrite (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN CONST UINT8          *Bytes,
  IN UINTN                 Length
)
{
  UINT64 Se = Config->Uart;

  if (Length == 0 || Length > TX_FIFO_BYTES) {
    return FALSE;
  }
  MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
  MmioWrite32 (M, Se + SE_UART_TX_TRANS_LEN, (UINT32)Length);
  MmioWrite32 (M, Se + SE_GENI_M_CMD0, UART_START_TX << OPCODE_SHIFT);
  for (UINTN At = 0; At < Length; At += 4) {
    UINT32 Word = 0;
    for (UINTN Byte = 0; Byte < 4 && At + Byte < Length; Byte++) {
      Word |= (UINT32)Bytes[At + Byte] << (8 * Byte);
    }
    MmioWrite32 (M, Se + SE_GENI_TX_FIFOn, Word);
  }
  for (UINT32 Index = 0; Index < POLLS; Index++) {
    if (MmioRead32 (M, Se + SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN) {
      MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
      return TRUE;
    }
  }
  MmioWrite32 (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  PollClear (M, Se + SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
  MmioWrite32 (M, Se + SE_GENI_M_IRQ_CLEAR, 0xffffffffu);
  return FALSE;
}

UINTN
RsInputUartRead (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  OUT UINT8               *Out,
  IN UINTN                 Size
)
{
  UINT64 Se     = Config->Uart;
  UINT32 Status = MmioRead32 (M, Se + SE_GENI_RX_FIFO_STATUS);
  UINT32 Words  = Status & RX_FIFO_WC_MASK;
  UINTN  Count  = 0;

  // Clear what the receive raised (stale, overrun) so it keeps going, and
  // start it again if it stopped.
  MmioWrite32 (
    M, Se + SE_GENI_S_IRQ_CLEAR, MmioRead32 (M, Se + SE_GENI_S_IRQ_STATUS)
  );
  for (UINT32 Index = 0; Index < Words && Count + 4 <= Size; Index++) {
    UINT32 Word  = MmioRead32 (M, Se + SE_GENI_RX_FIFOn);
    UINT32 Bytes = 4;

    if (Index + 1 == Words && (Status & RX_LAST)) {
      Bytes = (Status >> RX_LAST_BYTE_VALID_SHIFT) & 7;
      if (Bytes == 0 || Bytes > 4) {
        Bytes = 4;
      }
    }
    for (UINT32 Byte = 0; Byte < Bytes; Byte++) {
      Out[Count++] = (UINT8)(Word >> (8 * Byte));
    }
  }
  if (!(MmioRead32 (M, Se + SE_GENI_STATUS) & S_GENI_CMD_ACTIVE)) {
    StartRead (M, Se);
  }
  return Count;
}

VOID
RsInputOff (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN CONST RSINPUT_SAVED  *Saved,
  IN BOOLEAN               Uart
)
{
  if (Uart) {
    Abort (M, Config->Uart);
  }
  Drive (M, Config, &Config->Enable, FALSE);
  Drive (M, Config, &Config->Reset, FALSE);
  for (UINTN Index = Saved->PinCount; Index-- > 0;) {
    UINT64 Cfg = TlmmCfg (Config, Saved->Gpio[Index]);
    MmioWrite32 (M, Cfg + 4, Saved->Io[Index]);
    MmioWrite32 (M, Cfg, Saved->Cfg[Index]);
  }
  for (UINTN Index = 0; Index < Saved->VoteCount; Index++) {
    MmioWrite32 (M, Config->Gcc + Saved->VoteReg[Index], Saved->Vote[Index]);
  }
}
