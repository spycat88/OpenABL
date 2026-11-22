/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Soc - the Qualcomm SoCs OpenABL knows.
*/

#include "Core/Soc.h"

#include "Core/EfiData.h"

#define RT_NO_VMAP (UEFI_RT_SET_VIRTUAL_ADDRESS_MAP | UEFI_RT_CONVERT_POINTER)

// Sorted by id.
STATIC CONST SOC_INFO mSocs[] = {
  // SetVirtualAddressMap hangs on SDM845 (a runtime driver fails an
  // ASSERT), and EFI runtime shutdown is broken, so Linux uses PSCI for
  // reset and shutdown.
  {321, "Snapdragon 845", SOC_FAMILY_SDM845, RT_NO_VMAP | UEFI_RT_RESET_SYSTEM,
   0},
  // SDA845
  {341, NULL, SOC_FAMILY_SDM845, RT_NO_VMAP, 0},
  {356, "Snapdragon 865", SOC_FAMILY_SM8250, 0,
   SOC_QUIRK_GPT_FAT | SOC_QUIRK_SD_XPC},
  // SetVirtualAddressMap hangs on SM6115 too, and its XBL runtime code is
  // in the ADSP carveout, so none of it can be called.
  {444, "Snapdragon 662", SOC_FAMILY_SM6115, UEFI_RT_ALL, 0},
  // The Kamorta IoT part.
  {467, "Dragonwing QCS6125", SOC_FAMILY_SM6115, RT_NO_VMAP, 0},
  // SM8550
  {519, NULL, SOC_FAMILY_SM8550, 0, SOC_QUIRK_SD_XPC},
  // SM8475
  {530, NULL, SOC_FAMILY_SM8475, 0, 0},
  // SM8475P
  {531, "Snapdragon 8+ Gen 1", SOC_FAMILY_SM8475, 0, 0},
  // In the SD XPC list, but not in Linux's ids.
  {536, NULL, SOC_FAMILY_UNKNOWN, 0, SOC_QUIRK_SD_XPC},
  // SM8475_2
  {540, NULL, SOC_FAMILY_SM8475, 0, 0},
  // SM8650
  {557, NULL, SOC_FAMILY_SM8650, 0, 0},
  // SM4450
  {568, NULL, SOC_FAMILY_SM4450, 0, 0},
  {601, "Snapdragon G3x Gen 2", SOC_FAMILY_SM8550, 0, SOC_QUIRK_SD_XPC},
  {603, "Dragonwing QCS8550", SOC_FAMILY_SM8550, 0, SOC_QUIRK_SD_XPC},
  // SM8750
  {618, NULL, SOC_FAMILY_SM8750, 0, SOC_QUIRK_SD_XPC},
  // TG4070
  {634, "Snapdragon G2 Gen 1", SOC_FAMILY_SM6450, 0, SOC_QUIRK_SDCC_SM6450},
  {639, "Snapdragon 8 Elite", SOC_FAMILY_SM8750, 0, SOC_QUIRK_SD_XPC},
  {654, "Snapdragon G1 Gen 2", SOC_FAMILY_SM4450, 0, SOC_QUIRK_SDCC_SM4450},
  {682, "Snapdragon G3 Gen 3", SOC_FAMILY_SM8650, 0, SOC_QUIRK_SD_XPC},
  // RP G2, with a Cliffs XBL.
  {700, "Snapdragon G2 Gen 2", SOC_FAMILY_SM7675, 0, SOC_QUIRK_SDCC_SM7675},
  {706, "Dragonwing Q8750", SOC_FAMILY_SM8750, 0, SOC_QUIRK_SD_XPC},
  {732, "Dragonwing Q7790", SOC_FAMILY_UNKNOWN, 0, 0},
};

#define NAMES_MAX       2
#define COMPATIBLES_MAX 2

// How ChipInfo names a family's SoCs, by prefix, and the root compatibles
// of their Linux devicetrees. Linux names SoCs by their part number, after
// the board in the list, as in "ayntec,odin2mini", "qcom,qcs8550",
// "qcom,sm8550". Unused entries are NULL.
STATIC CONST struct {
  CONST CHAR8 *Names[NAMES_MAX];
  CONST CHAR8 *Compatibles[COMPATIBLES_MAX];
} mFamilies[SOC_FAMILY_COUNT] = {
  [SOC_FAMILY_SDM845] = {{"SDM845", "SDA845"}, {"qcom,sdm845"}},
  [SOC_FAMILY_SM6115] = {{"SM6115"}, {"qcom,sm6115"}},
  [SOC_FAMILY_SM8250] = {{"SM8250"}, {"qcom,sm8250"}},
  [SOC_FAMILY_SM4450] = {{"SM4450"}, {"qcom,sm4450"}},
  [SOC_FAMILY_SM6450] = {{"SM6450"}, {"qcom,sm6450"}},
  // Only id 700, the G2 Gen 2 with a Cliffs XBL, is known to be like it.
  [SOC_FAMILY_SM7675] = {{NULL}, {"qcom,sm7675"}},
  // Linux has no SM8475 devicetrees, and ports build on SM8450's.
  [SOC_FAMILY_SM8475] = {{"SM8475"}, {"qcom,sm8475", "qcom,sm8450"}},
  [SOC_FAMILY_SM8550] = {{"SM8550"}, {"qcom,sm8550"}},
  [SOC_FAMILY_SM8650] = {{"SM8650"}, {"qcom,sm8650"}},
  [SOC_FAMILY_SM8750] = {{"SM8750"}, {"qcom,sm8750"}},
};

CONST SOC_INFO *
SocById (IN UINT32 Id)
{
  for (UINTN Index = 0; Index < sizeof (mSocs) / sizeof (*mSocs); Index++) {
    if (mSocs[Index].Id == Id) {
      return &mSocs[Index];
    }
  }
  return NULL;
}

SOC_FAMILY
SocFamilyOf (
  IN UINT32       Id,
  IN CONST CHAR8 *Name OPTIONAL
)
{
  CONST SOC_INFO *Soc = SocById (Id);
  OABL_TEXT       Chip;

  if (Soc && Soc->Family != SOC_FAMILY_UNKNOWN) {
    return Soc->Family;
  }
  Chip = TextTrim (TextOf (Name));
  for (UINTN Family = 0; Family < SOC_FAMILY_COUNT; Family++) {
    for (UINTN Index = 0; Index < NAMES_MAX && mFamilies[Family].Names[Index];
         Index++) {
      if (TextStartsWith (Chip, mFamilies[Family].Names[Index])) {
        return (SOC_FAMILY)Family;
      }
    }
  }
  return SOC_FAMILY_UNKNOWN;
}

UINT32
SocQuirks (IN UINT32 Id)
{
  CONST SOC_INFO *Soc = SocById (Id);

  return Soc ? Soc->Quirks : 0;
}

UINT32
SocRuntimeGaps (IN UINT32 Id)
{
  CONST SOC_INFO *Soc = SocById (Id);

  return Soc ? Soc->RuntimeGaps : 0;
}

BOOLEAN
SocHasCompatibles (IN SOC_FAMILY Family)
{
  return Family < SOC_FAMILY_COUNT && mFamilies[Family].Compatibles[0];
}

BOOLEAN
SocMatches (
  IN SOC_FAMILY Family,
  IN OABL_BYTES List
)
{
  if (!SocHasCompatibles (Family)) {
    return FALSE;
  }
  for (UINTN At = 0; At < List.Len;) {
    UINTN End = At;

    while (End < List.Len && List.Ptr[End]) {
      End++;
    }
    if (End == List.Len) {
      return FALSE;
    }
    for (UINTN Index = 0;
         Index < COMPATIBLES_MAX && mFamilies[Family].Compatibles[Index];
         Index++) {
      if (TextEqStr (
            (OABL_TEXT){(CONST CHAR8 *)List.Ptr + At, End - At},
            mFamilies[Family].Compatibles[Index]
          )) {
        return TRUE;
      }
    }
    At = End + 1;
  }
  return FALSE;
}
