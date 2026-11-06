/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  OpenABL's own theme, the default: warm greys with a green accent.
*/

#include "Ui/Theme.h"

CONST THEME gThemeOpenAbl = {
  .Name       = "OpenABL",
  .Card       = 0x171716,
  .Raised     = 0x1C1C1B,
  .Line       = 0x2A2A28,
  .Text       = 0xF1EFE8,
  .Text2      = 0xB4B2A9,
  .Text3      = 0x888780,
  .Accent     = 0x5DCAA5,
  .AccentBg   = 0x085041,
  .AccentText = 0xE1F5EE,
  .Danger     = 0xF09595,
  .DangerBg   = 0x501313,
  .DangerFill = 0xE24B4A,
  .DangerText = 0xFCEBEB,
  .Button     = 0x2A2A28,
  .Toast      = 0x2C2C2A,
  .Brand      = 0xFF5555,
};
