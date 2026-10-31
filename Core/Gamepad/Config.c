/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad Config - where the gamepad MCU is, read from the device model's
  devicetree.
*/

#include "Core/Gamepad/Config.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/DeviceTree.h"

CONST GAMEPAD_DRIVER *CONST gGamepadDrivers[] = {
  &gRsInputDriver,
};

CONST UINTN gGamepadDriverCount =
  sizeof (gGamepadDrivers) / sizeof (*gGamepadDrivers);

VOID
GamepadConfigEncode (
  IN CONST GAMEPAD_CONFIG *Config,
  OUT UINT8                Out[GAMEPAD_CONFIG_BYTES]
)
{
  Out[0] = GAMEPAD_CONFIG_VERSION;
  CopyMem (Out + 1, Config, sizeof (*Config));
}

BOOLEAN
GamepadConfigDecode (
  IN CONST UINT8     *Bytes,
  IN UINTN            Size,
  OUT GAMEPAD_CONFIG *Config
)
{
  if (Size != GAMEPAD_CONFIG_BYTES || Bytes[0] != GAMEPAD_CONFIG_VERSION) {
    return FALSE;
  }
  CopyMem (Config, Bytes + 1, sizeof (*Config));
  return Config->Soc < gGamepadSocCount &&
         Config->Driver < gGamepadDriverCount &&
         Config->PinCount <= GAMEPAD_PINS_MAX &&
         Config->ClockCount <= GAMEPAD_CLOCKS_MAX &&
         Config->AlwaysOnCount <= GAMEPAD_ALWAYS_ON_MAX;
}

CONST CHAR8 *
GamepadDtResultName (IN GAMEPAD_DT_RESULT Result)
{
  switch (Result) {
  case GAMEPAD_DT_OK:
    return "ok";
  case GAMEPAD_DT_NONE:
    return "no rsinput node";
  case GAMEPAD_DT_NOT_ON_GENI:
    return "not on a GENI UART";
  case GAMEPAD_DT_UNKNOWN_SOC:
    return "not a SoC in the gamepad's list";
  case GAMEPAD_DT_NOT_IN_TABLE:
    return "a pin function or clock isn't in the SoC's entry";
  case GAMEPAD_DT_BAD:
    break;
  }
  return "a property is missing or malformed";
}

// The tree being read and what is known so far.
typedef struct _DT_READ {
  CONST UINT8       *Fdt;
  UINTN              Size;
  CONST GAMEPAD_SOC *Soc;
  UINTN              TlmmNode;
  UINTN              GccNode;
  GAMEPAD_CONFIG    *Config;
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

// The position of String in Node's string-list property Name.
STATIC BOOLEAN
PropIndexOf (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  IN CONST CHAR8   *Name,
  IN CONST CHAR8   *String,
  OUT UINTN        *Index
)
{
  OABL_BYTES List;
  UINTN      At = 0;

  if (!Prop (R, Node, Name, &List)) {
    return FALSE;
  }
  for (*Index = 0; At < List.Len; (*Index)++) {
    UINTN End = At;

    while (End < List.Len && List.Ptr[End]) {
      End++;
    }
    if (TextEqStr (
          (OABL_TEXT){(CONST CHAR8 *)List.Ptr + At, End - At}, String
        )) {
      return TRUE;
    }
    At = End + 1;
  }
  return FALSE;
}

// Reads the TLMM's tiles, in the order of the SoC's TileNames, or its one
// region.
STATIC BOOLEAN
TlmmTiles (
  IN CONST DT_READ   *R,
  OUT GAMEPAD_CONFIG *Config
)
{
  CONST GAMEPAD_SOC *Soc = R->Soc;

  if (!Soc->TileNames[0]) {
    return RegBase (R, R->TlmmNode, &Config->Tlmm[0]);
  }
  for (UINTN Tile = 0; Tile < GAMEPAD_TILES_MAX && Soc->TileNames[Tile];
       Tile++) {
    UINTN Entry;

    if (!PropIndexOf (
          R, R->TlmmNode, "reg-names", Soc->TileNames[Tile], &Entry
        ) ||
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
  IN OUT GAMEPAD_CONFIG *Config,
  IN GAMEPAD_PIN         Pin
)
{
  for (UINTN Index = 0; Index < Config->PinCount; Index++) {
    if (Config->Pins[Index].Gpio == Pin.Gpio) {
      Config->Pins[Index] = Pin;
      return TRUE;
    }
  }
  if (Config->PinCount == GAMEPAD_PINS_MAX) {
    return FALSE;
  }
  Config->Pins[Config->PinCount++] = Pin;
  return TRUE;
}

// Reads one pin group: pins, function, bias and drive strength.
STATIC GAMEPAD_DT_RESULT
PinGroup (
  IN CONST DT_READ *R,
  IN UINTN          Node
)
{
  OABL_BYTES  Pins;
  OABL_BYTES  Unused;
  OABL_TEXT   Function;
  UINT32      Drive = 2;
  GAMEPAD_PIN Pin   = {0};

  if (!Prop (R, Node, "pins", &Pins) ||
      !PropText (R, Node, "function", &Function)) {
    return GAMEPAD_DT_BAD;
  }
  PropU32 (R, Node, "drive-strength", &Drive);
  if (Drive < 2 || Drive > 16 || Drive % 2) {
    return GAMEPAD_DT_BAD;
  }
  Pin.DriveMa = (UINT8)Drive;
  Pin.Pull    = Prop (R, Node, "bias-pull-up", &Unused)     ? GAMEPAD_PULL_UP
                : Prop (R, Node, "bias-pull-down", &Unused) ? GAMEPAD_PULL_DOWN
                : Prop (R, Node, "bias-bus-hold", &Unused) ? GAMEPAD_PULL_KEEPER
                                                           : GAMEPAD_PULL_NONE;
  for (UINTN At = 0; At < Pins.Len;) {
    UINTN End = At;

    while (End < Pins.Len && Pins.Ptr[End]) {
      End++;
    }
    if (End == Pins.Len) {
      return GAMEPAD_DT_BAD;
    }
    if (!GpioOfPin (
          (OABL_TEXT){(CONST CHAR8 *)Pins.Ptr + At, End - At}, &Pin.Gpio
        )) {
      return GAMEPAD_DT_NOT_IN_TABLE;
    }
    if (!GamepadSocMux (R->Soc, Function, Pin.Gpio, &Pin.Func)) {
      return GAMEPAD_DT_NOT_IN_TABLE;
    }
    if (!AddPin (R->Config, Pin)) {
      return GAMEPAD_DT_BAD;
    }
    At = End + 1;
  }
  return GAMEPAD_DT_OK;
}

// Reads the TLMM states Node's pinctrl-0 points to. A state holds pins
// itself, or in subnodes. Nodes without pinctrl-0 have none.
STATIC GAMEPAD_DT_RESULT
PinStates (
  IN CONST DT_READ *R,
  IN UINTN          Node
)
{
  OABL_BYTES States;

  if (!Prop (R, Node, "pinctrl-0", &States)) {
    return GAMEPAD_DT_OK;
  }
  for (UINTN At = 0; At + 4 <= States.Len; At += 4) {
    UINTN             State;
    UINTN             Parent;
    OABL_BYTES        Unused;
    DT_NODES          Groups;
    GAMEPAD_DT_RESULT Result = GAMEPAD_DT_OK;

    if (!Deref (R, States.Ptr + At, &State) ||
        !DtParent (R->Fdt, R->Size, State, &Parent) || Parent != R->TlmmNode) {
      return GAMEPAD_DT_BAD;
    }
    if (Prop (R, State, "pins", &Unused)) {
      Result = PinGroup (R, State);
    } else if (DtNodeChildren (R->Fdt, R->Size, State, &Groups)) {
      for (UINTN Index = 0; Index < Groups.Count && !Result; Index++) {
        Result = PinGroup (R, Groups.Items[Index]);
      }
      VecFree (&Groups);
    } else {
      Result = GAMEPAD_DT_BAD;
    }
    if (Result) {
      return Result;
    }
  }
  return GAMEPAD_DT_OK;
}

// Reads a TLMM GPIO specifier: the TLMM's phandle, the pin and its flags.
STATIC BOOLEAN
GpioSpec (
  IN CONST DT_READ *R,
  IN OABL_BYTES     Spec,
  OUT GAMEPAD_LINE *Line
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
  OUT GAMEPAD_LINE *Line
)
{
  CHAR8      Property[32];
  OABL_BYTES Spec;

  *Line = (GAMEPAD_LINE){0};
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
  OUT GAMEPAD_LINE *Line
)
{
  OABL_BYTES Phandle;
  OABL_BYTES Spec;
  OABL_BYTES Unused;
  UINTN      Regulator;

  *Line = (GAMEPAD_LINE){0};
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
  OUT GAMEPAD_CONFIG *Config
)
{
  UINTN      Node = DT_START;
  OABL_BYTES Spec;
  OABL_BYTES Unused;

  while (DtNextCompatible (R->Fdt, R->Size, "regulator-fixed", Node, &Node)) {
    GAMEPAD_LINE Line;

    if (!DtIsEnabled (R->Fdt, R->Size, Node) ||
        !Prop (R, Node, "regulator-always-on", &Unused) ||
        !(Prop (R, Node, "gpio", &Spec) || Prop (R, Node, "gpios", &Spec)) ||
        !GpioSpec (R, Spec, &Line)) {
      continue;
    }
    if (Config->AlwaysOnCount == GAMEPAD_ALWAYS_ON_MAX) {
      return FALSE;
    }
    Line.ActiveLow = !Prop (R, Node, "enable-active-high", &Unused);
    Config->AlwaysOn[Config->AlwaysOnCount++] = Line;
  }
  return TRUE;
}

// Adds Id and the clocks it needs, unless already listed.
STATIC GAMEPAD_DT_RESULT
AddClock (
  IN CONST DT_READ *R,
  IN UINT16         Id
)
{
  GAMEPAD_CONFIG          *Config = R->Config;
  CONST GAMEPAD_SOC_CLOCK *Clock  = GamepadSocClock (R->Soc, Id);

  if (!Clock) {
    return GAMEPAD_DT_NOT_IN_TABLE;
  }
  for (UINTN Index = 0; Index < GAMEPAD_SOC_NEEDS_MAX && Clock->Needs[Index];
       Index++) {
    GAMEPAD_DT_RESULT Result = AddClock (R, Clock->Needs[Index]);
    if (Result) {
      return Result;
    }
  }
  for (UINTN Index = 0; Index < Config->ClockCount; Index++) {
    if (Config->Clocks[Index] == Id) {
      return GAMEPAD_DT_OK;
    }
  }
  if (Config->ClockCount == GAMEPAD_CLOCKS_MAX) {
    return GAMEPAD_DT_BAD;
  }
  Config->Clocks[Config->ClockCount++] = Id;
  return GAMEPAD_DT_OK;
}

// Adds Node's GCC clocks. *First gets the first one's id.
STATIC GAMEPAD_DT_RESULT
Clocks (
  IN CONST DT_READ *R,
  IN UINTN          Node,
  OUT UINT16       *First OPTIONAL
)
{
  OABL_BYTES List;

  if (!Prop (R, Node, "clocks", &List) || List.Len == 0 || List.Len % 8) {
    return GAMEPAD_DT_BAD;
  }
  for (UINTN At = 0; At < List.Len; At += 8) {
    UINTN             Gcc;
    UINT32            Id = ReadBe32 (List.Ptr + At + 4);
    GAMEPAD_DT_RESULT Result;

    if (!Deref (R, List.Ptr + At, &Gcc) || Gcc != R->GccNode || Id > 0xFFFFu) {
      return GAMEPAD_DT_BAD;
    }
    if ((Result = AddClock (R, (UINT16)Id))) {
      return Result;
    }
    if (First && At == 0) {
      *First = (UINT16)Id;
    }
  }
  return GAMEPAD_DT_OK;
}

// Finds the TLMM from the UART's first pin state and the GCC from its first
// clock, and the SoC by their compatibles.
STATIC GAMEPAD_DT_RESULT
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
    return GAMEPAD_DT_BAD;
  }
  R->Soc =
    GamepadSocOf (R->Fdt, R->Size, R->TlmmNode, R->GccNode, &R->Config->Soc);
  return R->Soc ? GAMEPAD_DT_OK : GAMEPAD_DT_UNKNOWN_SOC;
}

// Finds the first enabled node a driver binds.
STATIC BOOLEAN
FindPad (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  OUT UINTN      *Pad,
  OUT UINT8      *Driver
)
{
  for (UINTN Index = 0; Index < gGamepadDriverCount; Index++) {
    CONST CHAR8 *CONST *Compatibles = gGamepadDrivers[Index]->Compatibles;

    for (UINTN At = 0; Compatibles[At]; At++) {
      UINTN Node = DT_START;

      while (DtNextCompatible (Fdt, Size, Compatibles[At], Node, &Node)) {
        if (DtIsEnabled (Fdt, Size, Node)) {
          *Pad    = Node;
          *Driver = (UINT8)Index;
          return TRUE;
        }
      }
    }
  }
  return FALSE;
}

GAMEPAD_DT_RESULT
GamepadFromDt (
  IN CONST UINT8     *Fdt,
  IN UINTN            Size,
  OUT GAMEPAD_CONFIG *Config
)
{
  DT_READ           R = {Fdt, Size, NULL, 0, 0, Config};
  UINTN             Pad;
  UINTN             Uart = 0;
  UINTN             Wrapper;
  OABL_BYTES        Unused;
  GAMEPAD_DT_RESULT Result;

  ZeroMem (Config, sizeof (*Config));
  if (!FindPad (Fdt, Size, &Pad, &Config->Driver)) {
    return GAMEPAD_DT_NONE;
  }
  if (!DtParent (Fdt, Size, Pad, &Uart) ||
      !DtIsCompatible (Fdt, Size, Uart, "qcom,geni-uart") ||
      !DtIsEnabled (Fdt, Size, Uart) || !DtParent (Fdt, Size, Uart, &Wrapper) ||
      !DtIsCompatible (Fdt, Size, Wrapper, "qcom,geni-se-qup")) {
    return GAMEPAD_DT_NOT_ON_GENI;
  }
  if ((Result = FindSoc (&R, Uart))) {
    return Result;
  }
  if (!RegBase (&R, Uart, &Config->Uart) ||
      !RegBase (&R, Wrapper, &Config->Wrapper) || !TlmmTiles (&R, Config) ||
      !RegBase (&R, R.GccNode, &Config->Gcc)) {
    return GAMEPAD_DT_BAD;
  }
  // The wrapper's AHB clocks first, then the engine's.
  if ((Result = Clocks (&R, Wrapper, NULL)) ||
      (Result = Clocks (&R, Uart, &Config->SeClock))) {
    return Result;
  }
  if (!GamepadSocClock (R.Soc, Config->SeClock)->Rcg) {
    return GAMEPAD_DT_NOT_IN_TABLE;
  }
  if ((Result = PinStates (&R, Uart)) || (Result = PinStates (&R, Pad))) {
    return Result;
  }
  if (!Supply (&R, Pad, &Config->Supply) || !AlwaysOn (&R, Config) ||
      !Gpio (&R, Pad, "enable", &Config->Enable) ||
      !Gpio (&R, Pad, "reset", &Config->Reset) ||
      !Gpio (&R, Pad, "boot", &Config->Boot)) {
    return GAMEPAD_DT_BAD;
  }
  Config->InvertY = Prop (&R, Pad, "invert-y", &Unused);
  if (!PropU32 (&R, Pad, "axis-range", &Config->AxisRange) ||
      !Config->AxisRange) {
    Config->AxisRange = gGamepadDrivers[Config->Driver]->AxisRange;
  }
  return GAMEPAD_DT_OK;
}
