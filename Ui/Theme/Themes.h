/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Themes - every colour theme the menu offers, in the order the Theme
  setting cycles through them.

  Each entry is X (Id, Symbol): Id becomes a THEME_ID and Symbol names the
  THEME gTheme<Symbol> that Ui/Theme/<Symbol>.c defines. The Theme setting
  stores the Symbol as text ("Cyberpunk"), and the simulator's --theme
  takes it, so entries can be reordered freely. Never rename a Symbol once
  it has shipped: a menu that stored the old one would fall back to
  OpenABL's theme.

  To add a theme:

  1. Copy Ui/Theme/OpenAbl.c to Ui/Theme/<Symbol>.c and rename its table
     to gTheme<Symbol>.
  2. Set .Name to the theme's name. It is shown as it is in every
     language, so pick a name that needn't be translated.
  3. Change the colours. Every colour is required.
  4. Add its entry to THEME_LIST where it should come in the Theme
     setting.

  Nothing else changes: the build picks up the file.

  Kept free of includes so Core/Settings.h can count the entries.
*/

#ifndef __UI_THEME_THEMES_H__
#define __UI_THEME_THEMES_H__

#define THEME_LIST(X)            \
  X (THEME_OPENABL, OpenAbl)     \
  X (THEME_CYBERPUNK, Cyberpunk) \
  X (THEME_MIDNIGHT, Midnight)   \
  X (THEME_PHOSPHOR, Phosphor)   \
  X (THEME_AMBER, Amber)         \
  X (THEME_SAKURA, Sakura)       \
  X (THEME_MONO, Mono)

#endif
