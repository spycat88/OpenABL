/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Display - draws frames through the Graphics Output Protocol.
*/

#include "Efi/Display.h"

#include "Efi/Handover.h"
#include "Efi/Quirks.h"

// GET_PROTOCOL leaves the console driver bound to the GOP. We only call Blt
// and read the mode.
STATIC EFI_STATUS
OpenGop (
  IN EFI_HANDLE                      Handle,
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop
)
{
  VOID      *Interface = NULL;
  EFI_STATUS Status    = gBS->OpenProtocol (
    Handle, &gEfiGraphicsOutputProtocolGuid, &Interface, gImageHandle, NULL,
    EFI_OPEN_PROTOCOL_GET_PROTOCOL
  );

  *Gop = Interface;
  return Status;
}

// ChipInfo id of the SM8250.
#define CHIP_SM8250 356u

// SM8250 MDSS registers, as physical addresses.
// DSI0 CTRL; bit 0 is set while the controller is on.
#define DSI0_CTRL 0x0AE94004ull
// Tear check SYNC_CONFIG_VSYNC; bit 20 is hw_vsync_mode.
#define TE_SYNC_CONFIG_VSYNC 0x0AE6BA88ull
#define TE_HW_VSYNC_MODE     0x00100000u
// Written as 1 alongside the above in the original patch. What it enables
// is undocumented.
#define TE_ENABLE 0x0AE6B0A8ull

// Retroid's Panel_vtdr6130_visionox_cog_1080p_cmd.xml leaves out
// <DSITEUsingDedicatedTEPin>True, so DisplayDxe never syncs frame transfers
// to the panel's TE pin and the command-mode panel tears. Setting
// hw_vsync_mode makes each 60 Hz TE edge on GPIO 66 reset the read pointer,
// so the DSI transfer starts 21 lines into vblank. Done once a boot, only on
// an SM8250 with DSI0 on and a 1080x1920 mode.
STATIC VOID
EnableTearCheck (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
#if defined(__aarch64__)
  STATIC BOOLEAN mDone = FALSE;

  if (mDone) {
    return;
  }
  mDone = TRUE;
  if (Gop->Mode->Info->HorizontalResolution != 1080 ||
      Gop->Mode->Info->VerticalResolution != 1920 ||
      QuirksChipId () != CHIP_SM8250 ||
      (*(volatile CONST UINT32 *)(UINTN)DSI0_CTRL & 0x1u) == 0) {
    return;
  }
  *(volatile UINT32 *)(UINTN)TE_SYNC_CONFIG_VSYNC =
    *(volatile CONST UINT32 *)(UINTN)TE_SYNC_CONFIG_VSYNC | TE_HW_VSYNC_MODE;
  *(volatile UINT32 *)(UINTN)TE_ENABLE = 1u;
  __asm__ volatile ("dsb sy" : : : "memory");
#else
  (VOID)Gop;
#endif
}

STATIC DISPLAY
Opened (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop)
{
  EnableTearCheck (Gop);
  return (DISPLAY){Gop, Gop->Mode->Info->HorizontalResolution,
                   Gop->Mode->Info->VerticalResolution};
}

EFI_STATUS
DisplayOpen (OUT DISPLAY *Out)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_HANDLE                   *Handles = NULL;
  EFI_HANDLE                    Handle;
  UINTN                         Count = 0;
  EFI_STATUS                    Status;

  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles
  );
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  Handle = Count ? Handles[0] : NULL;
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (!Handle) {
    return EFI_NOT_FOUND;
  }
  Status = OpenGop (Handle, &Gop);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  *Out = Opened (Gop);
  return EFI_SUCCESS;
}

EFI_STATUS
DisplayOpenPanel (OUT DISPLAY *Out)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *First        = NULL;
  EFI_HANDLE                   *Handles      = NULL;
  UINT64                        RegionBase   = 0;
  UINT64                        RegionLength = 0;
  BOOLEAN                       Region;
  UINTN                         Count = 0;
  EFI_STATUS                    Status;

  Status = gBS->LocateHandleBuffer (
    ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles
  );
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  Region = HandoverDisplayRegion (&RegionBase, &RegionLength);
  for (UINTN Index = 0; Index < Count; Index++) {
    UINT64 Base;

    if (OpenGop (Handles[Index], &Gop) != EFI_SUCCESS) {
      continue;
    }
    Base = Gop->Mode->FrameBufferBase;
    if (Gop->Mode->Info->PixelFormat != PixelBltOnly && Region &&
        RegionBase <= Base && Base < RegionBase + RegionLength) {
      gBS->FreePool (Handles);
      *Out = Opened (Gop);
      return EFI_SUCCESS;
    }
    if (!First) {
      First = Gop;
    }
  }
  if (Handles) {
    gBS->FreePool (Handles);
  }
  if (!First) {
    return EFI_NOT_FOUND;
  }
  *Out = Opened (First);
  return EFI_SUCCESS;
}

VOID
DisplayBlit (
  IN DISPLAY      *Display,
  IN CONST UINT32 *Pixels,
  IN UINT32        Stride,
  IN UINT32        X,
  IN UINT32        Y,
  IN UINT32        W,
  IN UINT32        H
)
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = Display->Gop->Mode->Info;

  if ((UINT64)X + W > Info->HorizontalResolution ||
      (UINT64)Y + H > Info->VerticalResolution) {
    return;
  }
  // A BltPixel is {blue, green, red, reserved}, which is a little-endian
  // 0x00RRGGBB.
  Display->Gop->Blt (
    Display->Gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Pixels, EfiBltBufferToVideo,
    0, 0, X, Y, W, H, (UINTN)Stride * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
  );
}
