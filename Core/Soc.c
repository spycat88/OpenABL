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

// Sorted by id. Linux names SoCs by their part number; the SoC string comes
// after the board's in a root compatible list, as in "ayntec,odin2mini",
// "qcom,qcs8550", "qcom,sm8550".
//
// RtUnsupported: SetVirtualAddressMap hangs on SDM845 (a runtime driver
// fails an ASSERT) and SM6115, and SDM845's runtime shutdown is broken, so
// the kernel resets and powers off by PSCI. On SM6115 XBL's runtime code is
// in the ADSP carveout, so none can be called.
//
// 519, 536 and 618 are only known for their SD quirks. With no part
// number of their own, their carveouts and USB host come from the entry
// whose part ChipInfo's name starts with.
STATIC CONST SOC_INFO mSocs[] = {
  {.Id            = 321,
   .Name          = "Snapdragon 845",
   .Part          = "SDM845",
   .Compatibles   = {"qcom,sdm845"},
   .Carveouts     = &gCarveoutsSdm845,
   .RtUnsupported = RT_NO_VMAP | UEFI_RT_RESET_SYSTEM},
  // SDA845.
  {.Id            = 341,
   .Part          = "SDA845",
   .Carveouts     = &gCarveoutsSdm845,
   .RtUnsupported = RT_NO_VMAP},
  {.Id          = 356,
   .Name        = "Snapdragon 865",
   .Part        = "SM8250",
   .Compatibles = {"qcom,sm8250"},
   .Carveouts   = &gCarveoutsSm8250,
   .UsbHost     = USB_HOST_BOARD_SM8250,
   .Quirks      = SOC_SD_XPC | SOC_XBL_GPT_FAT},
  {.Id            = 444,
   .Name          = "Snapdragon 662",
   .Part          = "SM6115",
   .Compatibles   = {"qcom,sm6115"},
   .Carveouts     = &gCarveoutsSm6115,
   .UsbHost       = USB_HOST_BOARD_SM6115,
   .RtUnsupported = UEFI_RT_ALL},
  // The Kamorta IoT part.
  {.Id            = 467,
   .Name          = "Dragonwing QCS6125",
   .Compatibles   = {"qcom,sm6115"},
   .Carveouts     = &gCarveoutsSm6115,
   .UsbHost       = USB_HOST_BOARD_SM6115,
   .RtUnsupported = RT_NO_VMAP},
  // SM8550.
  {.Id = 519, .Quirks = SOC_SD_XPC},
  // SM8475.
  {.Id      = 531,
   .Name    = "Snapdragon 8+ Gen 1",
   .Part    = "SM8475",
   .UsbHost = USB_HOST_BOARD_GLINK},
  // SM8550 family.
  {.Id = 536, .Quirks = SOC_SD_XPC},
  {.Id          = 601,
   .Name        = "Snapdragon G3x Gen 2",
   .Part        = "SM8550",
   .Compatibles = {"qcom,sm8550"},
   .Carveouts   = &gCarveoutsSm8550,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Quirks      = SOC_SD_XPC},
  {.Id          = 603,
   .Name        = "Dragonwing QCS8550",
   .Part        = "SM8550",
   .Compatibles = {"qcom,sm8550"},
   .Carveouts   = &gCarveoutsSm8550,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Quirks      = SOC_SD_XPC},
  // SM8750.
  {.Id = 618, .Quirks = SOC_SD_XPC | SOC_ANDROID_DISPLAY_ID},
  {.Id          = 634,
   .Name        = "Snapdragon G2 Gen 1",
   .Part        = "SM6450",
   .Compatibles = {"qcom,sm6450"},
   .Carveouts   = &gCarveoutsSm6450,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Sdcc        = SOC_SDCC_SM6450},
  {.Id          = 639,
   .Name        = "Snapdragon 8 Elite",
   .Part        = "SM8750",
   .Compatibles = {"qcom,sm8750"},
   .Carveouts   = &gCarveoutsSm8750,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Quirks      = SOC_SD_XPC | SOC_ANDROID_DISPLAY_ID},
  {.Id          = 654,
   .Name        = "Snapdragon G1 Gen 2",
   .Part        = "SM4450",
   .Compatibles = {"qcom,sm4450"},
   .Carveouts   = &gCarveoutsSm4450,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Sdcc        = SOC_SDCC_SM4450},
  {.Id          = 682,
   .Name        = "Snapdragon G3 Gen 3",
   .Part        = "SM8650",
   .Compatibles = {"qcom,sm8650"},
   .Carveouts   = &gCarveoutsSm8650,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Quirks      = SOC_SD_XPC},
  // Cliffs XBL, with PmicGlinkDxe and the ADSP, DWC3 at 0xa600000.
  {.Id          = 700,
   .Name        = "Snapdragon G2 Gen 2",
   .Compatibles = {"qcom,sm7675"},
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Sdcc        = SOC_SDCC_SM7675},
  {.Id          = 706,
   .Name        = "Dragonwing Q8750",
   .Compatibles = {"qcom,sm8750"},
   .Carveouts   = &gCarveoutsSm8750,
   .UsbHost     = USB_HOST_BOARD_GLINK,
   .Quirks      = SOC_SD_XPC | SOC_ANDROID_DISPLAY_ID},
  {.Id = 732, .Name = "Dragonwing Q7790"},
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

CONST SOC_INFO *
SocByName (IN CONST CHAR8 *Name OPTIONAL)
{
  OABL_TEXT Chip = TextTrim (TextOf (Name));

  for (UINTN Index = 0; Index < sizeof (mSocs) / sizeof (*mSocs); Index++) {
    if (mSocs[Index].Part && TextStartsWith (Chip, mSocs[Index].Part)) {
      return &mSocs[Index];
    }
  }
  return NULL;
}

BOOLEAN
SocHasQuirks (
  IN UINT32 Id,
  IN UINT32 Quirks
)
{
  CONST SOC_INFO *Soc = SocById (Id);

  return Soc && (Soc->Quirks & Quirks) == Quirks;
}

BOOLEAN
SocHasCompatibles (IN CONST SOC_INFO *Soc)
{
  return Soc && Soc->Compatibles[0];
}

BOOLEAN
SocMatches (
  IN CONST SOC_INFO *Soc,
  IN OABL_BYTES      List
)
{
  for (UINTN At = 0; At < List.Len;) {
    UINTN End = At;

    while (End < List.Len && List.Ptr[End]) {
      End++;
    }
    if (End == List.Len) {
      return FALSE;
    }
    for (UINTN Index = 0;
         Index < SOC_COMPATIBLES_MAX && Soc->Compatibles[Index]; Index++) {
      if (TextEqStr (
            (OABL_TEXT){(CONST CHAR8 *)List.Ptr + At, End - At},
            Soc->Compatibles[Index]
          )) {
        return TRUE;
      }
    }
    At = End + 1;
  }
  return FALSE;
}
