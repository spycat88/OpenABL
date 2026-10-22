/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Lang - picks the menu's language and fills in templates.

  Each language's strings are in a file of their own under Ui/Lang/. A
  language that leaves a string out shows it in English.
*/

#include "Ui/Lang.h"
#include "Base/Text.h"
#include "Core/Settings.h"

_Static_assert (LANG_COUNT == SETTINGS_LANGUAGES, "languages");
// LangSet walks the strings as an array of pointers.
_Static_assert (sizeof (LANG_STRINGS) % sizeof (CONST CHAR8 *) == 0, "strings");

extern CONST LANG_STRINGS gLangEn;
extern CONST LANG_STRINGS gLangZhHans;
extern CONST LANG_STRINGS gLangKo;

// Indexed by LANG_ID.
STATIC CONST LANG_STRINGS *CONST mLangs[LANG_COUNT] = {
  [LANG_EN]      = &gLangEn,
  [LANG_ZH_HANS] = &gLangZhHans,
  [LANG_KO]      = &gLangKo,
};

// The current language, with English in place of any string it lacks.
STATIC LANG_STRINGS mCurrent;

CONST LANG_STRINGS *gStrings = &gLangEn;

VOID
LangSet (IN UINT8 Lang)
{
  CONST CHAR8 *CONST *English = (CONST CHAR8 *CONST *)&gLangEn;
  CONST CHAR8 *CONST *Chosen;
  CONST CHAR8       **Out = (CONST CHAR8 **)&mCurrent;
  UINTN               Index;

  if (Lang >= LANG_COUNT || Lang == LANG_EN) {
    gStrings = &gLangEn;
    return;
  }
  Chosen = (CONST CHAR8 *CONST *)mLangs[Lang];
  for (Index = 0; Index < sizeof (LANG_STRINGS) / sizeof (CONST CHAR8 *); Index++) {
    Out[Index] = Chosen[Index] ? Chosen[Index] : English[Index];
  }
  gStrings = &mCurrent;
}

CONST CHAR8 *
LangName (IN UINT8 Lang)
{
  return mLangs[Lang < LANG_COUNT ? Lang : LANG_EN]->Name;
}

// Whether language A comes before language B: by name, compared byte by
// byte (UTF-8 keeps code point order), then by ID.
STATIC BOOLEAN
NameBefore (
  IN UINT8 A,
  IN UINT8 B
)
{
  CONST UINT8 *NameA = (CONST UINT8 *)mLangs[A]->Name;
  CONST UINT8 *NameB = (CONST UINT8 *)mLangs[B]->Name;

  while (*NameA && *NameA == *NameB) {
    NameA++;
    NameB++;
  }
  return *NameA != *NameB ? *NameA < *NameB : A < B;
}

UINTN
LangPosition (IN UINT8 Lang)
{
  UINTN Position = 0;

  if (Lang >= LANG_COUNT) {
    Lang = LANG_EN;
  }
  for (UINT8 Other = 0; Other < LANG_COUNT; Other++) {
    if (NameBefore (Other, Lang)) {
      Position++;
    }
  }
  return Position;
}

UINT8
LangSorted (IN UINTN Position)
{
  for (UINT8 Lang = 0; Lang < LANG_COUNT; Lang++) {
    if (LangPosition (Lang) == Position) {
      return Lang;
    }
  }
  return LANG_EN;
}

VOID
LangFill (
  OUT CHAR8             *Out,
  IN UINTN               Cap,
  IN CONST CHAR8        *Template,
  IN CONST CHAR8 *CONST *Args,
  IN UINTN               ArgCount
)
{
  UINTN Length = 0;
  UINTN Next   = 0;

  if (!Cap) {
    return;
  }
  while (*Template && Length + 1 < Cap) {
    CONST CHAR8 *Arg = NULL;

    if (Template[0] == '{' && Template[1] == '}') {
      Arg = Next < ArgCount ? Args[Next] : "";
      Next++;
      Template += 2;
    } else if (Template[0] == '{' && Template[1] >= '0' && Template[1] <= '9' &&
               Template[2] == '}') {
      UINTN Which = (UINTN)(Template[1] - '0');
      Arg         = Which < ArgCount ? Args[Which] : "";
      Template += 3;
    }
    if (Arg) {
      while (*Arg && Length + 1 < Cap) {
        Out[Length++] = *Arg++;
      }
    } else {
      Out[Length++] = *Template++;
    }
  }
  Out[Length] = 0;
}
