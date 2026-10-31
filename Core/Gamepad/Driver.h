/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gamepad drivers - the protocols of the gamepad MCUs handhelds wire their
  buttons to.

  The MCU sits on a QUP serial engine at 115200 8N1, under a devicetree node
  with one of the driver's compatibles. Everything up to the UART is shared:
  Core/Gamepad/Config.h reads where the MCU is, Core/Gamepad/Hw.h brings it
  up and Efi/Gamepad.h turns its reports into menu input. A driver is one
  file that says how to start its MCU and how to read what it sends.

  To add one: write Core/Gamepad/<Name>.c defining a GAMEPAD_DRIVER, declare
  it below, and add it to the end of gGamepadDrivers in Config.c.
*/

#ifndef __CORE_GAMEPAD_DRIVER_H__
#define __CORE_GAMEPAD_DRIVER_H__

#include "Base/OablBase.h"

// Buttons, as a driver reports them.
#define GAMEPAD_BTN_DPAD_UP    (1u << 0)
#define GAMEPAD_BTN_DPAD_DOWN  (1u << 1)
#define GAMEPAD_BTN_DPAD_LEFT  (1u << 2)
#define GAMEPAD_BTN_DPAD_RIGHT (1u << 3)
#define GAMEPAD_BTN_NORTH      (1u << 4)
#define GAMEPAD_BTN_WEST       (1u << 5)
#define GAMEPAD_BTN_EAST       (1u << 6)
#define GAMEPAD_BTN_SOUTH      (1u << 7)
#define GAMEPAD_BTN_TL         (1u << 8)
#define GAMEPAD_BTN_TR         (1u << 9)
#define GAMEPAD_BTN_SELECT     (1u << 10)
#define GAMEPAD_BTN_START      (1u << 11)
#define GAMEPAD_BTN_THUMBL     (1u << 12)
#define GAMEPAD_BTN_THUMBR     (1u << 13)
#define GAMEPAD_BTN_MODE       (1u << 14)
#define GAMEPAD_BTN_BACK       (1u << 15)

// What the MCU last reported.
typedef struct _GAMEPAD_REPORT {
  // A state report has been seen.
  BOOLEAN Seen;
  UINT32  Buttons;
  // Left stick, as Linux reports it before invert-y: up is negative.
  INT32 LeftX, LeftY;
  // The MCU's version, NUL-terminated, empty until it says.
  CHAR8 Version[32];
  // Frames dropped for a bad checksum or length.
  UINT32 Bad;
} GAMEPAD_REPORT;

#define GAMEPAD_INIT_MAX 80

typedef struct _GAMEPAD_DRIVER {
  // For the log.
  CONST CHAR8 *Name;
  // The devicetree compatibles it binds, NULL-terminated.
  CONST CHAR8 *CONST *Compatibles;
  // The stick range when the devicetree gives no axis-range.
  UINT32 AxisRange;
  // Waits after the supply goes on, after the MCU is enabled, and between
  // init messages, ms.
  UINT16 PowerMs;
  UINT16 ReleaseMs;
  UINT16 InitMs;
  // Forgets what an earlier start left.
  VOID (*Begin) (VOID);
  // Builds init message Index into Out. Returns its length, 0 when there
  // are no more.
  UINTN (*Init) (
    IN UINTN  Index,
    OUT UINT8 Out[GAMEPAD_INIT_MAX]
  );
  // Takes bytes received and updates Report.
  VOID (*Feed) (
    IN CONST UINT8        *Bytes,
    IN UINTN               Count,
    IN OUT GAMEPAD_REPORT *Report
  );
} GAMEPAD_DRIVER;

// The drivers, in a fixed order: a cached GAMEPAD_CONFIG holds an index,
// so new ones go at the end.
extern CONST GAMEPAD_DRIVER *CONST gGamepadDrivers[];
extern CONST UINTN                 gGamepadDriverCount;

// Core/Gamepad/RsInput.c: the Retroid Pocket and AYN MCU.
extern CONST GAMEPAD_DRIVER gRsInputDriver;

#endif
