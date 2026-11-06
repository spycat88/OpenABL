/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Cyberpunk: neon magenta for focus, cyan for values and a yellow "ABL",
  on deep violet cards.
*/

#include "Ui/Theme.h"

CONST THEME gThemeCyberpunk = {
  .Name       = "Cyberpunk",
  .Card       = 0x140B22,
  .Raised     = 0x1A0F2C,
  .Line       = 0x3B2160,
  .Text       = 0xEDF9FF,
  .Text2      = 0x5FE3F2,
  .Text3      = 0x8E7BB8,
  .Accent     = 0xFF2BD6,
  .AccentBg   = 0x4A0A48,
  .AccentText = 0xFFE4FA,
  .Danger     = 0xFF6B6B,
  .DangerBg   = 0x4D0B16,
  .DangerFill = 0xF0243C,
  .DangerText = 0xFFE8EB,
  .Button     = 0x26163E,
  .Toast      = 0x2A1745,
  .Brand      = 0xFCEE0A,
};
