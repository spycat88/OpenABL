/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Pe - tells a PE/COFF image, such as an EFI-stub kernel, from other files.
*/

#ifndef __CORE_PE_H__
#define __CORE_PE_H__

#include "Base/OablBase.h"

// Returns TRUE if Head starts with an MZ header that points at a PE
// signature.
BOOLEAN
PeIsPe (
  IN CONST UINT8 *Head,
  IN UINTN        Size
);

#endif
