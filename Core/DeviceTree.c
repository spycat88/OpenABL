/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  DeviceTree - devicetree inspection and in-place editing.
*/

#include "Core/DeviceTree.h"

#include <limits.h>

#include "Base/Memory.h"
#include "Core/LibFdt.h"

STATIC OABL_TEXT
AsStr (IN OABL_BYTES Bytes)
{
  return (OABL_TEXT){(CONST CHAR8 *)Bytes.Ptr, Bytes.Len};
}

STATIC BOOLEAN
BytesIs (
  IN OABL_BYTES   Bytes,
  IN CONST CHAR8 *String
)
{
  return TextEqStr (AsStr (Bytes), String);
}

STATIC UINTN
Align4 (IN UINTN Value)
{
  return (Value + 3) & ~(UINTN)3;
}

// Gets the NUL-terminated string at offset At, if it ends within Size.
STATIC BOOLEAN
Cstr (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT OABL_BYTES *String
)
{
  if (At > Size) {
    return FALSE;
  }
  for (UINTN End = At; End < Size; End++) {
    if (!Bytes[End]) {
      *String = (OABL_BYTES){Bytes + At, End - At};
      return TRUE;
    }
  }
  return FALSE;
}

// Copies Bytes up to the first NUL. Returns NULL if that isn't UTF-8.
STATIC CHAR8 *
StringOf (IN OABL_BYTES Bytes)
{
  UINTN Length = 0;

  while (Length < Bytes.Len && Bytes.Ptr[Length]) {
    Length++;
  }
  if (!Utf8Valid (Bytes.Ptr, Length)) {
    return NULL;
  }
  return TextDup ((OABL_TEXT){(CONST CHAR8 *)Bytes.Ptr, Length});
}

BOOLEAN
DtInspect (
  IN CONST UINT8 *Blob,
  IN UINTN        Size,
  OUT DT_INFO    *Out OPTIONAL
)
{
  // One entry per open node: whether its name starts with "panel".
  OABL_BUFFER Panels     = {0};
  OABL_BYTES  Model      = {0};
  OABL_BYTES  Compatible = {0};
  OABL_BYTES  Name;
  OABL_BYTES  Value;
  BOOLEAN     HasModel      = FALSE;
  BOOLEAN     HasCompatible = FALSE;
  BOOLEAN     HasRotation   = FALSE;
  BOOLEAN     Ok            = FALSE;
  UINT64      Rotation      = 0;
  UINT32      Word;
  UINT32      Token;
  UINT32      Length;
  UINT32      NameOffset;
  UINTN       Total;
  UINTN       At;
  UINTN       Strings;

  if (!GetBe32 (Blob, Size, 0, &Word) || Word != FDT_MAGIC ||
      !GetBe32 (Blob, Size, 4, &Word) || Word > Size) {
    return FALSE;
  }
  Total = Word;
  if (!GetBe32 (Blob, Total, 8, &Word) || Word > Total) {
    return FALSE;
  }
  At = Word;
  if (!GetBe32 (Blob, Total, 12, &Word) || Word > Total) {
    return FALSE;
  }
  Strings = Word;
  for (;;) {
    if (!GetBe32 (Blob, Total, At, &Token)) {
      goto Done;
    }
    At += 4;
    if (Token == FDT_BEGIN_NODE) {
      if (!Cstr (Blob, Total, At, &Name) ||
          !BufferAppendU8 (&Panels, TextStartsWith (AsStr (Name), "panel"))) {
        goto Done;
      }
      At += Align4 (Name.Len + 1);
    } else if (Token == FDT_END_NODE) {
      if (!Panels.Len) {
        goto Done;
      }
      Panels.Len--;
    } else if (Token == FDT_PROP) {
      if (!GetBe32 (Blob, Total, At, &Length) ||
          !GetBe32 (Blob, Total, At + 4, &NameOffset) ||
          NameOffset > Total - Strings ||
          !Cstr (Blob, Total, Strings + NameOffset, &Name) ||
          !InBounds (Total, At + 8, Length)) {
        goto Done;
      }
      Value = (OABL_BYTES){Blob + At + 8, Length};
      At += 8 + Align4 (Length);
      if (Panels.Len == 1) {
        if (BytesIs (Name, "model") && !HasModel) {
          Model    = Value;
          HasModel = TRUE;
        } else if (BytesIs (Name, "compatible") && !HasCompatible) {
          Compatible    = Value;
          HasCompatible = TRUE;
        }
      } else if (BytesIs (Name, "rotation") && !HasRotation && Panels.Len &&
                 Panels.Data[Panels.Len - 1]) {
        if (Length == 4) {
          Rotation    = ReadBe32 (Value.Ptr);
          HasRotation = TRUE;
        } else if (Length == 8) {
          Rotation    = ReadBe64 (Value.Ptr);
          HasRotation = TRUE;
        }
      }
    } else if (Token == FDT_END) {
      break;
    } else if (Token != FDT_NOP) {
      goto Done;
    }
  }
  if (!HasModel) {
    goto Done;
  }
  if (!Out) {
    UINTN End = 0;
    while (End < Model.Len && Model.Ptr[End]) {
      End++;
    }
    Ok = Utf8Valid (Model.Ptr, End);
    goto Done;
  }
  *Out       = (DT_INFO){0};
  Out->Model = StringOf (Model);
  if (!Out->Model) {
    goto Done;
  }
  Out->Compatible = HasCompatible ? StringOf (Compatible) : NULL;
  if (HasRotation && Rotation % 90 == 0) {
    Out->HasPanelRotation = TRUE;
    Out->PanelRotation    = (UINT16)(Rotation % 360);
  }
  Ok = TRUE;
Done:
  BufferFree (&Panels);
  return Ok;
}

VOID
DtInfoFree (IN OUT DT_INFO *Info)
{
  FreePool (Info->Model);
  FreePool (Info->Compatible);
  *Info = (DT_INFO){0};
}

BOOLEAN
DtFindAll (
  IN CONST UINT8 *Region,
  IN UINTN        Size,
  OUT DT_BLOBS   *Found
)
{
  STATIC CONST UINT8 Magic[4] = {0xd0, 0x0d, 0xfe, 0xed};
  UINTN              From     = 0;

  *Found = (DT_BLOBS){0};
  while (From + 8 <= Size) {
    UINTN At    = From;
    UINTN Total = 0;

    while (At + 4 <= Size && CompareMem (Region + At, Magic, 4) != 0) {
      At++;
    }
    if (At + 4 > Size) {
      break;
    }
    if (At + 8 <= Size) {
      Total = ReadBe32 (Region + At + 4);
    }
    if (Total >= 64 && Total <= 16 << 20 && Total <= Size - At &&
        DtInspect (Region + At, Total, NULL)) {
      if (!VecPush (Found, (DT_BLOB){At, Total})) {
        return FALSE;
      }
      From = At + Total;
    } else {
      From = At + 4;
    }
  }
  return TRUE;
}

// Validation and libfdt wrappers

// For WalkOk: no node to check.
#define NO_NODE MAX_UINTN

// Checks for version 17 or later, the structure then the strings block
// inside totalsize, and totalsize inside the buffer and libfdt's int range.
STATIC BOOLEAN
HeaderOk (
  IN CONST VOID *Fdt,
  IN UINTN       Size
)
{
  UINT64 Total;
  UINT64 StructOffset;
  UINT64 StringsOffset;

  if (Size < sizeof (struct fdt_header) || fdt_magic (Fdt) != FDT_MAGIC ||
      fdt_version (Fdt) < 17) {
    return FALSE;
  }
  Total         = fdt_totalsize (Fdt);
  StructOffset  = fdt_off_dt_struct (Fdt);
  StringsOffset = fdt_off_dt_strings (Fdt);
  return Total <= Size && Total <= INT_MAX &&
         StructOffset + fdt_size_dt_struct (Fdt) <= StringsOffset &&
         StringsOffset + fdt_size_dt_strings (Fdt) <= Total;
}

// Checks the structure block before libfdt sees it. This libfdt checks
// neither name offsets nor property lengths, and a length of 0xfffffff4
// wraps its int offsets back to the property's own tag, so it loops forever.
// Node, unless NO_NODE, must be a begin tag. Call after HeaderOk.
STATIC BOOLEAN
WalkOk (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node
)
{
  CONST UINT8 *Struct  = Fdt + fdt_off_dt_struct (Fdt),
              *Strings = Fdt + fdt_off_dt_strings (Fdt);
  UINTN   StructSize   = fdt_size_dt_struct (Fdt);
  UINTN   StringsSize  = fdt_size_dt_strings (Fdt);
  UINTN   At           = 0;
  UINTN   Depth        = 0;
  UINTN   End;
  BOOLEAN Seen = Node == NO_NODE;
  UINT32  Tag;
  UINT32  Length;
  UINT32  NameOffset;

  for (;;) {
    if (At > StructSize || StructSize - At < 4) {
      return FALSE;
    }
    Tag = ReadBe32 (Struct + At);
    if (Tag == FDT_BEGIN_NODE) {
      for (End = At + 4; End < StructSize && Struct[End]; End++)
        ;
      if (End == StructSize) {
        return FALSE;
      }
      Seen |= At == Node;
      Depth++;
      At = Align4 (End + 1);
    } else if (Tag == FDT_END_NODE) {
      if (!Depth--) {
        return FALSE;
      }
      At += 4;
    } else if (Tag == FDT_PROP) {
      if (StructSize - At < 12) {
        return FALSE;
      }
      Length     = ReadBe32 (Struct + At + 4);
      NameOffset = ReadBe32 (Struct + At + 8);
      if (Length > StructSize - At - 12 || NameOffset >= StringsSize) {
        return FALSE;
      }
      for (End = NameOffset; End < StringsSize && Strings[End]; End++)
        ;
      if (End == StringsSize) {
        return FALSE;
      }
      At = Align4 (At + 12 + Length);
    } else if (Tag == FDT_NOP) {
      At += 4;
    } else if (Tag == FDT_END) {
      return !Depth && Seen;
    } else {
      return FALSE;
    }
  }
}

// Returns TRUE if libfdt's reads will stay inside the blob and its walks end.
STATIC BOOLEAN
Usable (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node
)
{
  return HeaderOk (Fdt, Size) && WalkOk (Fdt, Node);
}

// Thin libfdt wrappers. The blob must have passed Usable.

STATIC BOOLEAN
Get (
  IN CONST UINT8 *Fdt,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  CONST VOID *Property;
  INT32       Length;

  if (!(Property = fdt_getprop (Fdt, (INT32)Node, Name, &Length)) ||
      Length < 0) {
    return FALSE;
  }
  *Value = (OABL_BYTES){Property, (UINTN)Length};
  return TRUE;
}

STATIC BOOLEAN
AtPath (
  IN CONST UINT8 *Fdt,
  IN CONST CHAR8 *Path,
  OUT UINTN      *Node
)
{
  INT32 Offset = fdt_path_offset (Fdt, Path);

  if (Offset < 0) {
    return FALSE;
  }
  *Node = (UINTN)Offset;
  return TRUE;
}

// Lookups and subnodes

BOOLEAN
DtNodeAtPath (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN CONST CHAR8 *Path,
  OUT UINTN      *Node
)
{
  return Usable (Fdt, Size, NO_NODE) && AtPath (Fdt, Path, Node);
}

BOOLEAN
DtPropAt (
  IN CONST UINT8 *Fdt,
  IN UINTN        Size,
  IN UINTN        Node,
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  return Usable (Fdt, Size, Node) && Get (Fdt, Node, Name, Value);
}
