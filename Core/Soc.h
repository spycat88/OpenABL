/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Soc - the Qualcomm SoCs OpenABL knows, by ChipInfo id (GetChipId): the
  name the info card shows, the family the rest of OpenABL picks its
  tables and recipes by, and the runtime services XBL lacks. Ids are from
  Linux's dt-bindings/arm/qcom,ids.h where it has them.
*/

#ifndef __CORE_SOC_H__
#define __CORE_SOC_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

// SoCs that share a die, and so carveouts, USB host and devicetrees.
typedef enum {
  SOC_FAMILY_UNKNOWN,
  SOC_FAMILY_SDM845,
  SOC_FAMILY_SM6115,
  SOC_FAMILY_SM8250,
  SOC_FAMILY_SM4450,
  SOC_FAMILY_SM6450,
  SOC_FAMILY_SM7675,
  SOC_FAMILY_SM8475,
  SOC_FAMILY_SM8550,
  SOC_FAMILY_SM8650,
  SOC_FAMILY_SM8750,
  SOC_FAMILY_COUNT,
} SOC_FAMILY;

typedef struct _SOC_INFO {
  UINT32 Id;
  // Marketing name, or NULL to show ChipInfo's own name.
  CONST CHAR8 *Name;
  SOC_FAMILY   Family;
  // The UEFI_RT_* runtime services XBL lacks (Core/EfiData.h).
  UINT32 RuntimeGaps;
} SOC_INFO;

// The SoC with ChipInfo id Id, or NULL.
CONST SOC_INFO *
SocById (IN UINT32 Id);

// The family of ChipInfo id Id, else by ChipInfo's name for it, such as
// "SM8550" or "SDA845 v2.1".
SOC_FAMILY
SocFamilyOf (
  IN UINT32       Id,
  IN CONST CHAR8 *Name OPTIONAL
);

// The runtime services XBL lacks on chip Id, or 0.
UINT32
SocRuntimeGaps (IN UINT32 Id);

// Whether Family has root compatibles to match devicetrees by.
BOOLEAN
SocHasCompatibles (IN SOC_FAMILY Family);

// Whether List, a devicetree compatible property (NUL-terminated strings),
// has one of Family's compatibles.
BOOLEAN
SocMatches (
  IN SOC_FAMILY Family,
  IN OABL_BYTES List
);

#endif
