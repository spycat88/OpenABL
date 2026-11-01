/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DeviceTree - devicetree inspection.

  DtInspect walks the blob itself with bounds-checked reads, since blobs
  come off SD cards. The rest uses the ABL's libfdt (Core/LibFdt.h), which
  trusts its input, so every call validates the blob first and a bad one
  finds nothing.

  Nodes are libfdt offsets. Paths must be absolute, as libfdt reads one
  without the leading '/' as an alias.

  Trees not built by dtc follow libfdt's assumptions: a node's properties
  come before its subnodes, and a last_comp_version above 17 is refused.
*/

#ifndef __CORE_DEVICE_TREE_H__
#define __CORE_DEVICE_TREE_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

typedef struct _DT_INFO {
  CHAR8 *Model;
  // First compatible string of the root, or NULL.
  CHAR8 *Compatible;
  // Rotation of the first panel* node, if it is a multiple of 90 degrees.
  BOOLEAN HasPanelRotation;
  UINT16  PanelRotation;
} DT_INFO;

// Fails if the blob isn't a valid FDT or has no model. Out may be NULL to
// only check that. This is a minimal walk, not a general parser, to keep the
// menu the ABL embeds small.
BOOLEAN
DtInspect (
  IN CONST UINT8 *Blob,
  IN UINTN        Size,
  OUT DT_INFO    *Out OPTIONAL
);

VOID
DtInfoFree (IN OUT DT_INFO *Info);

// Where a devicetree is in a region.
typedef struct _DT_BLOB {
  UINTN Offset, Size;
} DT_BLOB;

typedef struct _DT_BLOBS OABL_VEC (DT_BLOB) DT_BLOBS;

// Finds every devicetree with a model in Region, such as a .dtb file with
// several concatenated. On failure, Found keeps the blobs found so far.
BOOLEAN
DtFindAll (
  IN CONST UINT8 *Region,
  IN UINTN        Size,
  OUT DT_BLOBS   *Found
);

// Finds the node at Path with fdt_path_offset. A component without a unit
// address matches a node with one: /reserved-memory/cont_splash_region finds
// cont_splash_region@9c000000.
BOOLEAN
DtNodeAtPath (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN CONST CHAR8 *Path,
  OUT UINTN      *Node
);

// Value points into the blob.
BOOLEAN
DtPropAt (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
);

#endif
