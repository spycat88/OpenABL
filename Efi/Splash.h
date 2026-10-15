/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Splash - keeping the firmware's splash on screen through the hand-over.

  The splash memory is reserved in the kernel's devicetree and /chosen tells
  the kernel the display is still on. The display stays on into the kernel
  unless a display-off is asked for.
*/

#ifndef __EFI_SPLASH_H__
#define __EFI_SPLASH_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Core/DeviceTree.h"
#include "Efi/Serial.h"

// Finds the framebuffer the panel scans out, if a simple-framebuffer node
// can describe it (a linear 32-bit mode). Prefers the graphics output whose
// framebuffer lies in XBL's display region, else takes the first.
BOOLEAN
SplashFramebuffer (OUT DT_FRAMEBUFFER *Out);

// Adds /chosen/openabl,display-on to tell the kernel the display is still
// running, unless DisplayQuiesce asks for a display-off.
VOID
SplashMarkDisplayOn (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
);

// Reserves XBL's display region, or else the framebuffer's pages, when the
// tree has no splash_region or cont_splash_region. The display stays on
// through the hand-over, so a kernel that allocated that memory would show
// its contents. The node is named splash_region so the ABL's splash step
// updates it as the stock ABL does Android's. Skipped when a display-off is
// asked for.
VOID
SplashReserveSplash (
  IN OUT UINT8 *Fdt,
  IN UINTN      Cap,
  IN LOG_SINK   Log
);

#endif
