/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Mono: plain greys and white, with no colour but the warnings.
*/

#include "Ui/Theme.h"

CONST THEME gThemeMono = {
  .Name       = "Mono",
  .Card       = 0x141414,
  .Raised     = 0x1A1A1A,
  .Line       = 0x2E2E2E,
  .Text       = 0xFFFFFF,
  .Text2      = 0xBDBDBD,
  .Text3      = 0x8A8A8A,
  .Accent     = 0xFFFFFF,
  .AccentBg   = 0x3A3A3A,
  .AccentText = 0xFFFFFF,
  .Danger     = 0xF09595,
  .DangerBg   = 0x501313,
  .DangerFill = 0xE24B4A,
  .DangerText = 0xFCEBEB,
  .Button     = 0x2A2A2A,
  .Toast      = 0x2C2C2C,
  .Brand      = 0xFFFFFF,
};
