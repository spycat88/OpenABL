/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Soc - the Qualcomm SoCs OpenABL knows, by ChipInfo id (GetChipId), and
  everything OpenABL does differently on each: the name the info card
  shows, the root compatibles of their Linux devicetrees, the no-map
  carveouts, the USB host recipe, the runtime services XBL lacks and the
  firmware quirks. Ids are from Linux's dt-bindings/arm/qcom,ids.h where it
  has them.

  This is the one place that knows chip ids; other code asks for a field.
*/

#ifndef __CORE_SOC_H__
#define __CORE_SOC_H__

#include "Base/OablBase.h"

#include "Base/Text.h"
#include "Core/Carveouts.h"
#include "Core/UsbHostRegs.h"

#define SOC_COMPATIBLES_MAX 2

// The SdccDxe build that needs more than the S18R patch (Efi/Quirks.c).
typedef enum {
  SOC_SDCC_NONE,
  SOC_SDCC_SM4450,
  SOC_SDCC_SM6450,
  // Snapdragon G2 Gen 2 (RP G2).
  SOC_SDCC_SM7675,
} SOC_SDCC;

// Quirk flags.
//
// SD cards get XPC (OCR bit 28, SDXC power control) along with S18R clear.
#define SOC_SD_XPC (1u << 0)
// XBL's PartitionDxe "repairs" a card's GPT and its FAT driver overruns its
// cache tags (SM8250): QuirksGptClobber and QuirksFatCache apply.
#define SOC_XBL_GPT_FAT (1u << 1)
// The stock ABL passes the display id as an androidboot parameter.
#define SOC_ANDROID_DISPLAY_ID (1u << 2)

typedef struct _SOC_INFO {
  UINT32 Id;
  // Marketing name, or NULL to show ChipInfo's own name.
  CONST CHAR8 *Name;
  // Part number that ChipInfo's name (GetChipIdString) starts with, by
  // which a chip with an unknown id is matched. NULL if not matched by
  // name.
  CONST CHAR8 *Part;
  // Strings any one of which a devicetree's root compatible lists for this
  // SoC; unused ones are NULL. All NULL if not known.
  CONST CHAR8 *Compatibles[SOC_COMPATIBLES_MAX];
  // No-map carveouts, or NULL to reclaim from the banks alone.
  CONST CARVEOUTS_TABLE *Carveouts;
  // USB host recipe, or USB_HOST_BOARD_NONE.
  USB_HOST_BOARD UsbHost;
  // UEFI_RT_* runtime services XBL lacks; 0 if it has them all.
  UINT32 RtUnsupported;
  SOC_SDCC Sdcc;
  // SOC_* quirk flags.
  UINT32 Quirks;
} SOC_INFO;

// The SoC with ChipInfo id Id, or NULL.
CONST SOC_INFO *
SocById (IN UINT32 Id);

// The first SoC whose part number Name, ChipInfo's name, starts with, or
// NULL.
CONST SOC_INFO *
SocByName (IN CONST CHAR8 *Name OPTIONAL);

// Whether the SoC with ChipInfo id Id has all of the quirk flags Quirks.
BOOLEAN
SocHasQuirks (
  IN UINT32 Id,
  IN UINT32 Quirks
);

// Whether Soc has any compatibles to match devicetrees by.
BOOLEAN
SocHasCompatibles (IN CONST SOC_INFO *Soc);

// Whether List, a devicetree compatible property (NUL-terminated strings),
// has one of Soc's compatibles.
BOOLEAN
SocMatches (
  IN CONST SOC_INFO *Soc,
  IN OABL_BYTES      List
);

#endif
