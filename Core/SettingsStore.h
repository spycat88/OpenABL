/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  SettingsStore - named variables in the last 4 KiB of the misc partition.

  Android owns the first 64 KiB of misc: the bootloader message at 0, the
  vendor space from 2 KiB, the wipe package from 16 KiB and the system
  space (virtual A/B and others) from 32 KiB. Misc is usually 1 MiB, so its
  last 4 KiB is free. The store is only written there when misc is larger
  than SETTINGS_STORE_MISC_MIN, and only once the region is blank or already
  holds the store, so nothing else's data is overwritten.

  The store, all fields little-endian:

  - Header, 32 bytes:
      0  Magic "OABLVARS"
      8  UINT16 Version (1)
     10  UINT16 Size (4096)
     12  UINT32 CRC-32 of bytes 16 to the end
     16  UINT16 Count of index entries in use
     18  UINT16 HeapUsed, bytes of the heap in use
     20  12 reserved bytes (zero)
    Later versions keep bytes 0-15 as they are.
  - Index, 64 entries of 8 bytes from offset 32. The first Count are in
    use:
      0  UINT16 Offset of the record in the heap
      2  UINT16 DataLen
      4  UINT8  NameLen (1 to SETTINGS_STORE_NAME_MAX)
      5  3 reserved bytes (zero)
  - Heap, from offset 544 to the end. A record is the name (ASCII, no NUL)
    followed by the data.

  A write that is cut short fails the CRC, and the store starts again
  empty.

  Like UEFI variables, a value is never empty: setting one to zero bytes
  deletes it.
*/

#ifndef __CORE_SETTINGS_STORE_H__
#define __CORE_SETTINGS_STORE_H__

#include "Base/OablBase.h"

#include "Base/Text.h"

// The store at the end of misc.
#define SETTINGS_STORE_SIZE 4096

#define SETTINGS_STORE_VERSION     1
#define SETTINGS_STORE_HEADER_SIZE 32
#define SETTINGS_STORE_ENTRY_SIZE  8
#define SETTINGS_STORE_ENTRIES     64
#define SETTINGS_STORE_HEAP_START                                              \
  (SETTINGS_STORE_HEADER_SIZE +                                                \
   SETTINGS_STORE_ENTRIES * SETTINGS_STORE_ENTRY_SIZE)
#define SETTINGS_STORE_HEAP_SIZE                                               \
  (SETTINGS_STORE_SIZE - SETTINGS_STORE_HEAP_START)
#define SETTINGS_STORE_NAME_MAX 64

// Android's 64 KiB at the start of misc, then the store.
#define SETTINGS_STORE_MISC_MIN (64 * 1024 + SETTINGS_STORE_SIZE)

// What SettingsStoreOpen found.
typedef enum {
  // A valid store.
  SETTINGS_STORE_FOUND,
  // All 0x00 or all 0xFF: now an empty store.
  SETTINGS_STORE_BLANK,
  // The magic, but not valid: a write was cut short or the store was
  // damaged. Now an empty store.
  SETTINGS_STORE_DAMAGED,
  // A valid store of a later version. Leave it alone.
  SETTINGS_STORE_NEWER,
  // Something else's data. Leave it alone.
  SETTINGS_STORE_FOREIGN,
} SETTINGS_STORE_STATE;

// Checks the 4 KiB read from the end of misc, making it an empty store when
// it is blank or damaged.
SETTINGS_STORE_STATE
SettingsStoreOpen (IN OUT UINT8 Store[SETTINGS_STORE_SIZE]);

// Makes Store an empty store.
VOID
SettingsStoreFormat (OUT UINT8 Store[SETTINGS_STORE_SIZE]);

// TRUE if Store is a valid store of this version.
BOOLEAN
SettingsStoreValid (IN CONST UINT8 Store[SETTINGS_STORE_SIZE]);

// TRUE if Name can be stored: 1 to SETTINGS_STORE_NAME_MAX printable ASCII
// characters.
BOOLEAN
SettingsStoreNameValid (IN CONST CHAR8 *Name);

// Finds variable Name. Value points into Store.
BOOLEAN
SettingsStoreGet (
  IN CONST UINT8  Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  OUT OABL_BYTES *Value
);

// Sets variable Name, or deletes it if Size is 0 (deleting a missing one
// succeeds). FALSE, leaving Store as it was, if the name isn't valid or the
// value doesn't fit.
BOOLEAN
SettingsStoreSet (
  IN OUT UINT8    Store[SETTINGS_STORE_SIZE],
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Size
);

// Sets the CRC, ready for writing.
VOID
SettingsStoreSeal (IN OUT UINT8 Store[SETTINGS_STORE_SIZE]);

#endif
