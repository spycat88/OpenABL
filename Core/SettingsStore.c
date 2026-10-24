/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SettingsStore - named variables in the last 4 KiB of the misc partition.
*/

#include "Core/SettingsStore.h"

#include "Base/Buffer.h"
#include "Base/Memory.h"

// Header offsets.
#define OFF_MAGIC     0
#define OFF_VERSION   8
#define OFF_SIZE      10
#define OFF_CRC       12
#define OFF_COUNT     16
#define OFF_HEAP_USED 18

// Index entry offsets.
#define ENTRY_OFFSET   0
#define ENTRY_DATA_LEN 2
#define ENTRY_NAME_LEN 4

#define MAGIC     "OABLVARS"
#define MAGIC_LEN 8

_Static_assert (SETTINGS_STORE_HEAP_SIZE > 0, "heap");

STATIC UINT8 *
Entry (
  IN CONST UINT8 *Store,
  IN UINTN        Index
)
{
  return (UINT8 *)Store + SETTINGS_STORE_HEADER_SIZE +
         Index * SETTINGS_STORE_ENTRY_SIZE;
}

STATIC UINTN
Count (IN CONST UINT8 *Store)
{
  return ReadLe16 (Store + OFF_COUNT);
}

STATIC UINTN
HeapUsed (IN CONST UINT8 *Store)
{
  return ReadLe16 (Store + OFF_HEAP_USED);
}

STATIC BOOLEAN
HasMagic (IN CONST UINT8 *Store)
{
  return CompareMem (Store + OFF_MAGIC, MAGIC, MAGIC_LEN) == 0;
}

// The CRC-32 of everything after the CRC field.
STATIC UINT32
StoreCrc (IN CONST UINT8 *Store)
{
  return CalculateCrc32 (
    Store + OFF_CRC + 4, SETTINGS_STORE_SIZE - OFF_CRC - 4
  );
}

// The magic, the size and the CRC, which every version keeps.
STATIC BOOLEAN
Sealed (IN CONST UINT8 *Store)
{
  return HasMagic (Store) &&
         ReadLe16 (Store + OFF_SIZE) == SETTINGS_STORE_SIZE &&
         ReadLe32 (Store + OFF_CRC) == StoreCrc (Store);
}

STATIC OABL_BYTES
NameAt (
  IN CONST UINT8 *Store,
  IN UINTN        Index
)
{
  CONST UINT8 *At = Entry (Store, Index);

  return BytesOf (
    Store + SETTINGS_STORE_HEAP_START + ReadLe16 (At + ENTRY_OFFSET),
    At[ENTRY_NAME_LEN]
  );
}

STATIC OABL_BYTES
DataAt (
  IN CONST UINT8 *Store,
  IN UINTN        Index
)
{
  CONST UINT8 *At = Entry (Store, Index);

  return BytesOf (
    Store + SETTINGS_STORE_HEAP_START + ReadLe16 (At + ENTRY_OFFSET) +
      At[ENTRY_NAME_LEN],
    ReadLe16 (At + ENTRY_DATA_LEN)
  );
}

STATIC BOOLEAN
BytesEq (
  IN OABL_BYTES A,
  IN OABL_BYTES B
)
{
  return A.Len == B.Len && CompareMem (A.Ptr, B.Ptr, A.Len) == 0;
}

// Finds Name's entry. Returns Count if it isn't there.
STATIC UINTN
Find (
  IN CONST UINT8 *Store,
  IN OABL_BYTES   Name
)
{
  UINTN Index;

  for (Index = 0; Index < Count (Store); Index++) {
    if (BytesEq (NameAt (Store, Index), Name)) {
      break;
    }
  }
  return Index;
}

BOOLEAN
SettingsStoreValid (IN CONST UINT8 Store[SETTINGS_STORE_SIZE])
{
  UINTN Entries = Count (Store);
  UINTN Used    = HeapUsed (Store);

  if (!Sealed (Store) ||
      ReadLe16 (Store + OFF_VERSION) != SETTINGS_STORE_VERSION ||
      Entries > SETTINGS_STORE_ENTRIES || Used > SETTINGS_STORE_HEAP_SIZE) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Entries; Index++) {
    CONST UINT8 *At      = Entry (Store, Index);
    UINTN        NameLen = At[ENTRY_NAME_LEN];

    if (NameLen == 0 || NameLen > SETTINGS_STORE_NAME_MAX ||
        (UINTN)ReadLe16 (At + ENTRY_OFFSET) + NameLen +
            ReadLe16 (At + ENTRY_DATA_LEN) >
          Used) {
      return FALSE;
    }
    // Names are unique.
    for (UINTN Other = 0; Other < Index; Other++) {
      if (BytesEq (NameAt (Store, Other), NameAt (Store, Index))) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

VOID
SettingsStoreFormat (OUT UINT8 Store[SETTINGS_STORE_SIZE])
{
  ZeroMem (Store, SETTINGS_STORE_SIZE);
  CopyMem (Store + OFF_MAGIC, MAGIC, MAGIC_LEN);
  WriteLe16 (Store + OFF_VERSION, SETTINGS_STORE_VERSION);
  WriteLe16 (Store + OFF_SIZE, SETTINGS_STORE_SIZE);
}

// TRUE if every byte of the store is Fill.
STATIC BOOLEAN
AllOf (
  IN CONST UINT8 *Store,
  IN UINT8        Fill
)
{
  for (UINTN Index = 0; Index < SETTINGS_STORE_SIZE; Index++) {
    if (Store[Index] != Fill) {
      return FALSE;
    }
  }
  return TRUE;
}

SETTINGS_STORE_STATE
SettingsStoreOpen (IN OUT UINT8 Store[SETTINGS_STORE_SIZE])
{
  if (SettingsStoreValid (Store)) {
    return SETTINGS_STORE_FOUND;
  }
  // A newer version's data stays as it is.
  if (Sealed (Store) &&
      ReadLe16 (Store + OFF_VERSION) > SETTINGS_STORE_VERSION) {
    return SETTINGS_STORE_NEWER;
  }
  if (HasMagic (Store)) {
    SettingsStoreFormat (Store);
    return SETTINGS_STORE_DAMAGED;
  }
  if (AllOf (Store, 0x00) || AllOf (Store, 0xFF)) {
    SettingsStoreFormat (Store);
    return SETTINGS_STORE_BLANK;
  }
  return SETTINGS_STORE_FOREIGN;
}

BOOLEAN
SettingsStoreNameValid (IN CONST CHAR8 *Name)
{
  UINTN Length = AsciiStrLen (Name);

  if (Length == 0 || Length > SETTINGS_STORE_NAME_MAX) {
    return FALSE;
  }
  for (UINTN Index = 0; Index < Length; Index++) {
    if ((UINT8)Name[Index] < 0x21 || (UINT8)Name[Index] > 0x7e) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
SettingsStoreGet (
  IN CONST UINT8  Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
)
{
  UINTN Index = Find (Store, BytesOf (Name, AsciiStrLen (Name)));

  if (Index == Count (Store)) {
    return FALSE;
  }
  *Value = DataAt (Store, Index);
  return TRUE;
}

// The heap bytes the records use, leaving out entry Skip.
STATIC UINTN
LiveBytes (
  IN CONST UINT8 *Store,
  IN UINTN        Skip
)
{
  UINTN Live = 0;

  for (UINTN Index = 0; Index < Count (Store); Index++) {
    if (Index != Skip) {
      Live += NameAt (Store, Index).Len + DataAt (Store, Index).Len;
    }
  }
  return Live;
}

// Packs the heap, keeping the records in index order.
STATIC VOID
Pack (IN OUT UINT8 *Store)
{
  UINT8 Heap[SETTINGS_STORE_HEAP_SIZE];
  UINTN Used = 0;

  for (UINTN Index = 0; Index < Count (Store); Index++) {
    UINT8     *At     = Entry (Store, Index);
    OABL_BYTES Name   = NameAt (Store, Index);
    UINTN      Record = Name.Len + ReadLe16 (At + ENTRY_DATA_LEN);

    CopyMem (Heap + Used, Name.Ptr, Record);
    WriteLe16 (At + ENTRY_OFFSET, (UINT16)Used);
    Used += Record;
  }
  ZeroMem (Store + SETTINGS_STORE_HEAP_START, SETTINGS_STORE_HEAP_SIZE);
  CopyMem (Store + SETTINGS_STORE_HEAP_START, Heap, Used);
  WriteLe16 (Store + OFF_HEAP_USED, (UINT16)Used);
}

// Removes entry Index and packs the heap.
STATIC VOID
Remove (
  IN OUT UINT8 *Store,
  IN UINTN      Index
)
{
  UINTN Entries = Count (Store);

  for (UINTN Later = Index + 1; Later < Entries; Later++) {
    CopyMem (
      Entry (Store, Later - 1), Entry (Store, Later), SETTINGS_STORE_ENTRY_SIZE
    );
  }
  Entries--;
  ZeroMem (Entry (Store, Entries), SETTINGS_STORE_ENTRY_SIZE);
  WriteLe16 (Store + OFF_COUNT, (UINT16)Entries);
  Pack (Store);
}

BOOLEAN
SettingsStoreSet (
  IN OUT UINT8    Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
)
{
  UINTN      NameLen;
  UINTN      Entries = Count (Store);
  UINTN      Index;
  UINTN      Used;
  OABL_BYTES Old;
  UINT8     *At;

  if (!SettingsStoreNameValid (Name)) {
    return FALSE;
  }
  NameLen = AsciiStrLen (Name);
  Index   = Find (Store, BytesOf (Name, NameLen));
  if (Size == 0) {
    if (Index < Entries) {
      Remove (Store, Index);
    }
    return TRUE;
  }
  if (Index < Entries) {
    Old = DataAt (Store, Index);
    // The same size: in place.
    if (Old.Len == Size) {
      CopyMem ((UINT8 *)Old.Ptr, Data, Size);
      return TRUE;
    }
  } else if (Entries == SETTINGS_STORE_ENTRIES) {
    return FALSE;
  }
  // Check the fit before anything is removed, so a failure changes nothing.
  if (Size > SETTINGS_STORE_HEAP_SIZE ||
      LiveBytes (Store, Index) + NameLen + Size > SETTINGS_STORE_HEAP_SIZE) {
    return FALSE;
  }
  if (Index < Entries) {
    Remove (Store, Index);
    Entries--;
  } else if (HeapUsed (Store) + NameLen + Size > SETTINGS_STORE_HEAP_SIZE) {
    Pack (Store);
  }
  Used = HeapUsed (Store);
  At   = Entry (Store, Entries);
  ZeroMem (At, SETTINGS_STORE_ENTRY_SIZE);
  WriteLe16 (At + ENTRY_OFFSET, (UINT16)Used);
  WriteLe16 (At + ENTRY_DATA_LEN, (UINT16)Size);
  At[ENTRY_NAME_LEN] = (UINT8)NameLen;
  CopyMem (Store + SETTINGS_STORE_HEAP_START + Used, Name, NameLen);
  CopyMem (Store + SETTINGS_STORE_HEAP_START + Used + NameLen, Data, Size);
  WriteLe16 (Store + OFF_COUNT, (UINT16)(Entries + 1));
  WriteLe16 (Store + OFF_HEAP_USED, (UINT16)(Used + NameLen + Size));
  return TRUE;
}

VOID
SettingsStoreSeal (IN OUT UINT8 Store[SETTINGS_STORE_SIZE])
{
  WriteLe32 (Store + OFF_CRC, StoreCrc (Store));
}
