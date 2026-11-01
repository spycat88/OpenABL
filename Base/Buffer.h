/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Buffer - growable arrays, sorting and byte-order helpers.

  OABL_BUFFER holds bytes and OABL_VEC (T) declares an array of T. Both start
  empty when zeroed. Appends return FALSE when memory runs out and leave
  the array unchanged.
*/

#ifndef __BASE_BUFFER_H__
#define __BASE_BUFFER_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

typedef struct _OABL_BUFFER {
  UINT8 *Data;
  UINTN  Len, Cap;
} OABL_BUFFER;

BOOLEAN
BufferReserve (
  IN OUT OABL_BUFFER *Buffer,
  IN UINTN            Extra
);

BOOLEAN
BufferAppend (
  IN OUT OABL_BUFFER *Buffer,
  IN CONST VOID      *Data,
  IN UINTN            Length
);

BOOLEAN
BufferAppendU8 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT8            Value
);
BOOLEAN
BufferAppendLe16 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT16           Value
);
BOOLEAN
BufferAppendLe32 (
  IN OUT OABL_BUFFER *Buffer,
  IN UINT32           Value
);

BOOLEAN
BufferAppendZeros (
  IN OUT OABL_BUFFER *Buffer,
  IN UINTN            Length
);

BOOLEAN
BufferFrom (
  OUT OABL_BUFFER *Buffer,
  IN CONST VOID   *Data,
  IN UINTN         Length
);

VOID
BufferFree (IN OUT OABL_BUFFER *Buffer);

STATIC inline OABL_BYTES
BufferBytes (IN CONST OABL_BUFFER *Buffer)
{
  return (OABL_BYTES){Buffer->Data, Buffer->Len};
}

// Arrays of any type

#define OABL_VEC(T)                                                            \
  {                                                                            \
    T    *Items;                                                               \
    UINTN Count, Cap;                                                          \
  }

// Grows *Items to hold at least Need elements of Size bytes.
BOOLEAN
VecGrow (
  IN OUT VOID **Items,
  IN OUT UINTN *Capacity,
  IN UINTN      Need,
  IN UINTN      Size
);

// Appends an element. The value may be a compound literal.
#define VecPush(Vec, ...)                                                      \
  (VecGrow (                                                                   \
     (VOID **)&(Vec)->Items, &(Vec)->Cap, (Vec)->Count + 1,                    \
     sizeof (*(Vec)->Items)                                                    \
   )                                                                           \
     ? ((Vec)->Items[(Vec)->Count++] = (__VA_ARGS__), TRUE)                    \
     : FALSE)

#define VecReserve(Vec, Extra)                                                 \
  VecGrow (                                                                    \
    (VOID **)&(Vec)->Items, &(Vec)->Cap, (Vec)->Count + (Extra),               \
    sizeof (*(Vec)->Items)                                                     \
  )

// Frees the array, but not what its elements point to.
#define VecFree(Vec)                                                           \
  ((VOID)(VecFreeRaw ((VOID **)&(Vec)->Items), (Vec)->Count = (Vec)->Cap = 0))
VOID
VecFreeRaw (IN OUT VOID **Items);

// Byte order

STATIC inline UINT16
ReadLe16 (IN CONST UINT8 *Bytes)
{
  return (UINT16)(Bytes[0] | Bytes[1] << 8);
}

STATIC inline UINT32
ReadLe32 (IN CONST UINT8 *Bytes)
{
  return (UINT32)Bytes[0] | (UINT32)Bytes[1] << 8 | (UINT32)Bytes[2] << 16 |
         (UINT32)Bytes[3] << 24;
}

STATIC inline UINT64
ReadLe64 (IN CONST UINT8 *Bytes)
{
  return (UINT64)ReadLe32 (Bytes) | (UINT64)ReadLe32 (Bytes + 4) << 32;
}

STATIC inline UINT32
ReadBe32 (IN CONST UINT8 *Bytes)
{
  return (UINT32)Bytes[0] << 24 | (UINT32)Bytes[1] << 16 |
         (UINT32)Bytes[2] << 8 | (UINT32)Bytes[3];
}

STATIC inline UINT64
ReadBe64 (IN CONST UINT8 *Bytes)
{
  return (UINT64)ReadBe32 (Bytes) << 32 | ReadBe32 (Bytes + 4);
}

STATIC inline VOID
WriteLe16 (
  OUT UINT8 *Bytes,
  IN UINT16  Value
)
{
  Bytes[0] = (UINT8)Value;
  Bytes[1] = (UINT8)(Value >> 8);
}

STATIC inline VOID
WriteLe32 (
  OUT UINT8 *Bytes,
  IN UINT32  Value
)
{
  for (INT32 Index = 0; Index < 4; Index++) {
    Bytes[Index] = (UINT8)(Value >> 8 * Index);
  }
}

STATIC inline VOID
WriteLe64 (
  OUT UINT8 *Bytes,
  IN UINT64  Value
)
{
  for (INT32 Index = 0; Index < 8; Index++) {
    Bytes[Index] = (UINT8)(Value >> 8 * Index);
  }
}

STATIC inline VOID
WriteBe32 (
  OUT UINT8 *Bytes,
  IN UINT32  Value
)
{
  for (INT32 Index = 0; Index < 4; Index++) {
    Bytes[Index] = (UINT8)(Value >> (24 - 8 * Index));
  }
}

// Bounds-checked reads at offset At. They fail past the end of Size bytes.
BOOLEAN
GetLe16 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT16     *Value
);
BOOLEAN
GetLe32 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT32     *Value
);
BOOLEAN
GetBe32 (
  IN CONST UINT8 *Bytes,
  IN UINTN        Size,
  IN UINTN        At,
  OUT UINT32     *Value
);

// Checks At + Length <= Size without overflowing.
STATIC inline BOOLEAN
InBounds (
  IN UINTN Size,
  IN UINTN At,
  IN UINTN Length
)
{
  return At <= Size && Length <= Size - At;
}

// Alignment must be a power of two.
STATIC inline UINTN
AlignUp (
  IN UINTN Value,
  IN UINTN Alignment
)
{
  return (Value + Alignment - 1) & ~(Alignment - 1);
}

// Stable insertion sort. The lists are short (drives, models, boot
// entries), and a general sort would take kilobytes the ABL image has no
// room for.
VOID
SortBy (
  IN OUT VOID *Elements,
  IN UINTN     Count,
  IN UINTN     Size,
  IN BOOLEAN (*Less) (
    IN CONST VOID *A,
    IN CONST VOID *B,
    IN VOID       *Context
  ),
  IN VOID *Context OPTIONAL
);

#endif
