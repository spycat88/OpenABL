/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Amber: an old amber screen terminal, orange on black.
*/

#include "Ui/Theme.h"

CONST THEME gThemeAmber = {
  .Name       = "Amber",
  .Card       = 0x170F05,
  .Raised     = 0x1D1407,
  .Line       = 0x3D2A10,
  .Text       = 0xFFE9C7,
  .Text2      = 0xF2B45C,
  .Text3      = 0xA8793A,
  .Accent     = 0xFFB000,
  .AccentBg   = 0x4A3000,
  .AccentText = 0xFFF2D6,
  .Danger     = 0xFF7A7A,
  .DangerBg   = 0x45100E,
  .DangerFill = 0xE5383B,
  .DangerText = 0xFFE6E6,
  .Button     = 0x33240E,
  .Toast      = 0x2E200C,
  .Brand      = 0xFF8C00,
};
