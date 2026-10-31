/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  RsInput - the RSInput gamepad MCU on a Qualcomm GENI UART, the parts that
  need no firmware.

  Some handhelds (Retroid Pocket, AYN) wire their buttons to an MCU that
  streams its state over a QUP serial engine at 115200 8N1. Linux binds it
  under a "qcom,geni-uart" with one of two drivers: RSInput, for
  "retroidpocket,rsinput", "ayntec,rsinput" or "gamepad,rsinput" nodes,
  and the older Retroid Pocket and Odin 2 gamepad drivers, for
  "retroid,retroid-pocket-gamepad" and "ayn,odin2-gamepad", which start it
  the same way. They speak the same protocol but
  start the MCU with different commands and waits. This is a fresh
  implementation of that wire protocol, the bring-up of its UART and lines,
  and the reading of all of it from the device model's devicetree.

  A frame is A5 D3 5A 3D, a sequence number, a command, a little-endian
  payload length, the payload and an XOR of everything from the sequence
  number to the end of the payload. The host asks for the version (01 02)
  and sets the report parameters (01 05 ...); the MCU then sends status
  frames (02): two bytes of buttons, the two triggers and the four stick
  axes, 16 bits each.

  What the devicetree can't say, the TLMM function numbers of the UART pins,
  which TLMM tile holds each pin and the GCC registers behind each clock id,
  comes from a table per SoC.
  Register sequences go through an MMIO so host tests can check them; their
  waits are bounded polls.
*/

#ifndef __CORE_RS_INPUT_H__
#define __CORE_RS_INPUT_H__

#include "Base/OablBase.h"

#include "Core/Mmio.h"

// Wire protocol

#define RSINPUT_CMD_COMMOD 0x01
#define RSINPUT_CMD_STATUS 0x02

#define RSINPUT_COMMOD_VERSION 0x02
#define RSINPUT_COMMOD_SET_PAR 0x05

// Header (4), sequence, command, length (2) and checksum.
#define RSINPUT_FRAME_OVERHEAD 9
#define RSINPUT_PAYLOAD_MAX    64
#define RSINPUT_FRAME_MAX      (RSINPUT_FRAME_OVERHEAD + RSINPUT_PAYLOAD_MAX)

// Button bits of a status frame, in the MCU's order.
#define RSINPUT_BTN_DPAD_UP    (1u << 0)
#define RSINPUT_BTN_DPAD_DOWN  (1u << 1)
#define RSINPUT_BTN_DPAD_LEFT  (1u << 2)
#define RSINPUT_BTN_DPAD_RIGHT (1u << 3)
#define RSINPUT_BTN_NORTH      (1u << 4)
#define RSINPUT_BTN_WEST       (1u << 5)
#define RSINPUT_BTN_EAST       (1u << 6)
#define RSINPUT_BTN_SOUTH      (1u << 7)
#define RSINPUT_BTN_TL         (1u << 8)
#define RSINPUT_BTN_TR         (1u << 9)
#define RSINPUT_BTN_SELECT     (1u << 10)
#define RSINPUT_BTN_START      (1u << 11)
#define RSINPUT_BTN_THUMBL     (1u << 12)
#define RSINPUT_BTN_THUMBR     (1u << 13)
#define RSINPUT_BTN_MODE       (1u << 14)
#define RSINPUT_BTN_BACK       (1u << 15)

// The stick range Linux reports when the devicetree gives no axis-range.
#define RSINPUT_AXIS_RANGE_DEFAULT 0x580

// Builds a frame with Payload into Out. Returns its length, or 0 if the
// payload is longer than RSINPUT_PAYLOAD_MAX.
UINTN
RsInputFrame (
  IN UINT8        Sequence,
  IN UINT8        Command,
  IN CONST UINT8 *Payload OPTIONAL,
  IN UINTN        Length,
  OUT UINT8       Out[RSINPUT_FRAME_MAX]
);

// The driver whose start-up a node asks for.
typedef enum {
  RSINPUT_DRIVER_RSINPUT,
  RSINPUT_DRIVER_RETROID,
  RSINPUT_DRIVER_COUNT,
} RSINPUT_DRIVER;

#define RSINPUT_INIT_MAX 6

// How a driver starts the MCU: the waits after the supply and after the
// enable, then the commands it sends, that far apart.
typedef struct _RSINPUT_INIT {
  UINT16 PowerMs;
  UINT16 ReleaseMs;
  UINT16 CommandMs;
  UINT8  Count;
  struct {
    CONST UINT8 *Payload;
    UINT8        Length;
  } Commands[RSINPUT_INIT_MAX];
} RSINPUT_INIT;

CONST RSINPUT_INIT *
RsInputInit (IN UINT8 Driver);

// What the MCU last reported.
typedef struct _RSINPUT_REPORT {
  // A status frame has been seen.
  BOOLEAN Seen;
  UINT16  Buttons;
  // Left stick, as Linux reports it before inversion: up is negative.
  INT32 LeftX, LeftY;
  // The version answer, NUL-terminated, empty until one comes.
  CHAR8 Version[32];
} RSINPUT_REPORT;

// Bytes received but not yet a whole frame.
typedef struct _RSINPUT_RX {
  UINT8 Buf[RSINPUT_FRAME_MAX * 2];
  UINTN Len;
  // Frames dropped for a bad checksum or length.
  UINT32 Bad;
} RSINPUT_RX;

// Adds Count bytes and parses every whole frame into Report. Bytes before a
// header are skipped. Returns the number of status frames parsed.
UINTN
RsInputRxFeed (
  IN OUT RSINPUT_RX     *Rx,
  IN CONST UINT8        *Bytes,
  IN UINTN               Count,
  IN OUT RSINPUT_REPORT *Report
);

// Configuration

// The SoCs with a table.
typedef enum {
  RSINPUT_SOC_SM8550,
  RSINPUT_SOC_SM8250,
  RSINPUT_SOC_COUNT,
} RSINPUT_SOC;

// TLMM pull, as in the CFG register.
#define RSINPUT_PULL_NONE   0
#define RSINPUT_PULL_DOWN   1
#define RSINPUT_PULL_KEEPER 2
#define RSINPUT_PULL_UP     3

// A TLMM pin's mux and pad settings.
typedef struct _RSINPUT_PIN {
  UINT16 Gpio;
  UINT8  Func;
  UINT8  Pull;
  // 2 to 16, even.
  UINT8 DriveMa;
} RSINPUT_PIN;

// A TLMM line the host drives: a GPIO, or a fixed regulator's enable.
typedef struct _RSINPUT_LINE {
  BOOLEAN Present;
  BOOLEAN ActiveLow;
  UINT16  Gpio;
} RSINPUT_LINE;

#define RSINPUT_PINS_MAX      8
#define RSINPUT_CLOCKS_MAX    8
#define RSINPUT_ALWAYS_ON_MAX 4
#define RSINPUT_TILES_MAX     3

// Everything the bring-up needs, read from the devicetree. It is cached in
// a setting as raw bytes after RSINPUT_CONFIG_VERSION, so bump that when
// the layout changes.
typedef struct _RSINPUT_CONFIG {
  UINT8 Soc;
  UINT8 Driver;
  // The serial engine, its QUP wrapper, the TLMM's tiles in the SoC table's
  // order and the GCC.
  UINT64 Uart;
  UINT64 Wrapper;
  UINT64 Tlmm[RSINPUT_TILES_MAX];
  UINT64 Gcc;
  // The UART's pins, then the gamepad node's own pinctrl.
  UINT8       PinCount;
  RSINPUT_PIN Pins[RSINPUT_PINS_MAX];
  // A fixed regulator's enable, when vdd-supply is one.
  RSINPUT_LINE Supply;
  // The enables of always-on fixed regulators, which Linux turns on before
  // any driver runs. The Retroid Pocket's MCU 3.3 V is one, not named by
  // the gamepad node.
  UINT8        AlwaysOnCount;
  RSINPUT_LINE AlwaysOn[RSINPUT_ALWAYS_ON_MAX];
  RSINPUT_LINE Enable;
  RSINPUT_LINE Reset;
  RSINPUT_LINE Boot;
  // GCC clock ids: the wrapper's, then the serial engine's.
  UINT8  ClockCount;
  UINT16 Clocks[RSINPUT_CLOCKS_MAX];
  // The serial engine's clock, which is set to the UART rate.
  UINT16  SeClock;
  BOOLEAN InvertY;
  UINT32  AxisRange;
} RSINPUT_CONFIG;

#define RSINPUT_CONFIG_VERSION 4
#define RSINPUT_CONFIG_BYTES   (1 + sizeof (RSINPUT_CONFIG))

// Writes Config as RSINPUT_CONFIG_VERSION then its bytes.
VOID
RsInputConfigEncode (
  IN CONST RSINPUT_CONFIG *Config,
  OUT UINT8                Out[RSINPUT_CONFIG_BYTES]
);

// Reads what RsInputConfigEncode wrote. FALSE for another version, size,
// SoC or driver.
BOOLEAN
RsInputConfigDecode (
  IN CONST UINT8     *Bytes,
  IN UINTN            Size,
  OUT RSINPUT_CONFIG *Config
);

// Why RsInputFromDt found no gamepad.
typedef enum {
  RSINPUT_DT_OK,
  // No enabled rsinput node.
  RSINPUT_DT_NONE,
  // Not under an enabled qcom,geni-uart in a qcom,geni-se-qup.
  RSINPUT_DT_NOT_ON_GENI,
  // The TLMM or GCC isn't one with a table.
  RSINPUT_DT_UNKNOWN_SOC,
  // A pin function or clock isn't in the SoC's table.
  RSINPUT_DT_NOT_IN_TABLE,
  // A property is missing or malformed.
  RSINPUT_DT_BAD,
} RSINPUT_DT_RESULT;

CONST CHAR8 *
RsInputDtResultName (IN RSINPUT_DT_RESULT Result);

// Reads the gamepad from Fdt. Supplies other than a GPIO-switched fixed
// regulator are left to the firmware, as nothing here can switch them.
RSINPUT_DT_RESULT
RsInputFromDt (
  IN CONST UINT8     *Fdt,
  IN UINTN            Size,
  OUT RSINPUT_CONFIG *Config
);

// Bring-up

// TRUE if the chip ChipInfo reports is Soc. Guards against a devicetree
// for another SoC.
BOOLEAN
RsInputSocIsChip (
  IN UINT8        Soc,
  IN UINT32       ChipId,
  IN CONST CHAR8 *Name OPTIONAL
);

// TLMM registers the bring-up changes, kept to put back.
typedef struct _RSINPUT_SAVED {
  UINT8  PinCount;
  UINT16 Gpio[RSINPUT_PINS_MAX + RSINPUT_ALWAYS_ON_MAX + 4];
  UINT32 Cfg[RSINPUT_PINS_MAX + RSINPUT_ALWAYS_ON_MAX + 4];
  UINT32 Io[RSINPUT_PINS_MAX + RSINPUT_ALWAYS_ON_MAX + 4];
  // GCC vote registers and their values before.
  UINT8  VoteCount;
  UINT32 VoteReg[RSINPUT_CLOCKS_MAX];
  UINT32 Vote[RSINPUT_CLOCKS_MAX];
} RSINPUT_SAVED;

// Saves, then sets, the pins' mux and pads, and drives the lines as Linux's
// probe does before its wait: the always-on supplies and the supply on,
// boot and reset inactive.
VOID
RsInputPinsOn (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  OUT RSINPUT_SAVED       *Saved
);

// Starts the MCU as Linux's probe does after its wait: enable and reset
// active.
VOID
RsInputRelease (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config
);

// Turns the clocks on and sets the serial engine's to 7.3728 MHz. FALSE if
// one doesn't report running.
BOOLEAN
RsInputClocksOn (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN OUT RSINPUT_SAVED    *Saved
);

// Returns the serial engine's protocol (2 is UART) from its firmware
// revision.
UINT32
RsInputUartProtocol (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config
);

#define RSINPUT_GENI_UART 2

// Sets the serial engine up for 115200 8N1 in FIFO mode, without flow
// control, and starts receiving.
VOID
RsInputUartSetup (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config
);

// Sends Length bytes, at most the TX FIFO's 64. FALSE if the engine didn't
// finish.
BOOLEAN
RsInputUartWrite (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN CONST UINT8          *Bytes,
  IN UINTN                 Length
);

// Reads what the RX FIFO holds, up to Size bytes. Returns the count.
UINTN
RsInputUartRead (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  OUT UINT8               *Out,
  IN UINTN                 Size
);

// Stops receiving if Uart (the engine's clocks are on), powers the MCU
// down and puts back what was saved.
VOID
RsInputOff (
  IN CONST MMIO           *M,
  IN CONST RSINPUT_CONFIG *Config,
  IN CONST RSINPUT_SAVED  *Saved,
  IN BOOLEAN               Uart
);

#endif
