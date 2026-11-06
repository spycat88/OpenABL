/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Theme - the menu's colours, in every theme the menu offers.

  Each theme is a THEME table in its own file in Ui/Theme/, listed in
  Ui/Theme/Themes.h. The Theme setting picks one, and the view draws each
  frame with it. Colours are 0xRRGGBB. The background is always black, so
  OLED pixels stay off.
*/

#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include "Base/OablBase.h"
#include "Ui/Theme/Themes.h"

#define THEME_ID_ENTRY(Id, Symbol) Id,

// The themes, in THEME_LIST order. Only held in memory: the Theme setting
// is stored as the theme's Symbol.
typedef enum { THEME_LIST (THEME_ID_ENTRY) THEME_COUNT } THEME_ID;

typedef struct _THEME {
  // Shown as it is, in every language. Required.
  CONST CHAR8 *Name;
  // Cards, the dialog, and the edges between things.
  UINT32 Card;
  UINT32 Raised;
  UINT32 Line;
  // Text: main, secondary (values, details) and faint (headers, hints).
  UINT32 Text;
  UINT32 Text2;
  UINT32 Text3;
  // The focused row and button, icons, the countdown bar and the toast.
  UINT32 Accent;
  UINT32 AccentBg;
  UINT32 AccentText;
  // Rows and dialogs that erase or can't be undone, and errors.
  UINT32 Danger;
  UINT32 DangerBg;
  UINT32 DangerFill;
  UINT32 DangerText;
  // A dialog's unfocused button, and the toast's card.
  UINT32 Button;
  UINT32 Toast;
  // "ABL" in the header's wordmark.
  UINT32 Brand;
} THEME;

#define THEME_DECLARE(Id, Symbol) extern CONST THEME gTheme##Symbol;
THEME_LIST (THEME_DECLARE)
#undef THEME_DECLARE

// Returns the theme Theme (a THEME_ID), or OpenABL's for one out of range.
CONST THEME *
ThemeGet (IN UINT8 Theme);

// Returns the THEME_ID with this Symbol, or THEME_COUNT.
UINT8
ThemeFromSymbol (IN CONST CHAR8 *Symbol);

#endif
