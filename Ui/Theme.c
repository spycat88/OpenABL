/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Theme - picks a theme by its THEME_ID or its Symbol.

  The themes themselves are in Ui/Theme/, one file per theme.
*/

#include "Ui/Theme.h"
#include "Base/Text.h"
#include "Core/Settings.h"

_Static_assert (THEME_COUNT == SETTINGS_THEMES, "themes");

#define THEME_TABLE_ENTRY(Id, Symbol) [Id] = &gTheme##Symbol,

STATIC CONST THEME *CONST mThemes[THEME_COUNT] = {
  THEME_LIST (THEME_TABLE_ENTRY)};

CONST THEME *
ThemeGet (IN UINT8 Theme)
{
  return mThemes[Theme < THEME_COUNT ? Theme : THEME_OPENABL];
}

UINT8
ThemeFromSymbol (IN CONST CHAR8 *Symbol)
{
  return SettingsThemeFromSymbol (
    (CONST UINT8 *)Symbol, AsciiStrLen (Symbol)
  );
}
