/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Pe - tells a PE/COFF image, such as an EFI-stub kernel, from other files.
*/

#include "Core/Pe.h"

#include "Base/Text.h"

BOOLEAN
PeIsPe (
  IN CONST UINT8 *Head,
  IN UINTN        Size
)
{
  UINT32 PeOffset;

  if (Size < 2 || Head[0] != 'M' || Head[1] != 'Z') {
    return FALSE;
  }
  if (!GetLe32 (Head, Size, 0x3c, &PeOffset) || !InBounds (Size, PeOffset, 4)) {
    return FALSE;
  }
  return Head[PeOffset] == 'P' && Head[PeOffset + 1] == 'E' &&
         !Head[PeOffset + 2] && !Head[PeOffset + 3];
}
