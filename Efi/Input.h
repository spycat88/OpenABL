/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Input - maps firmware key events to UI inputs.

  Up and volume up are Up, down and volume down are Down, Esc and Backspace
  are Back, and Enter, Space and SCAN_SUSPEND (the power key on Qualcomm
  keypads) are Select. Other keys are ignored. Repeat and hold timing is
  left to the UI's key decoder (Ui/Ui.h).

  With the gamepad open (Efi/Gamepad.h), d-pad up and down are Up and Down,
  right and A (the east button) are Select, left and B (the south button)
  are Back, Start is Start and Select is Models. X and Y do nothing. The
  gamepad has a decoder of its own; the last button pressed of those held
  is the one reported.
*/

#ifndef __EFI_INPUT_H__
#define __EFI_INPUT_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Ui/Ui.h"

typedef struct _INPUT_KEYS {
  KEYS Decoder;
  // The gamepad's decoder, its buttons at the last poll, and the button it
  // reports (0 for none).
  KEYS   Pad;
  UINT16 PadHeld;
  UINT16 PadButton;
} INPUT_KEYS;

typedef struct _INPUT_EVENTS OABL_VEC (INPUT) INPUT_EVENTS;

STATIC inline INPUT_KEYS
InputKeysNew (VOID)
{
  return (INPUT_KEYS){0};
}

// Returns TRUE if a key was already down at startup, as when volume down is
// held for the menu. Reads one mapped key.
BOOLEAN
InputKeysHeldAtStart (IN INPUT_KEYS *Keys);

// Feeds every waiting key, and the gamepad's buttons if it is open, through
// the decoders at time Now (ms) and appends the resulting inputs to Out.
// Caller frees Out with VecFree.
VOID
InputKeysPoll (
  IN OUT INPUT_KEYS   *Keys,
  IN UINT64            Now,
  IN OUT INPUT_EVENTS *Out
);

#endif
