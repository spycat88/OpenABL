/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  BootEntry - a boot entry the loader can start by itself.

  Each Boot Loader Specification Type #1 entry file becomes a BOOT_ENTRY. An
  entry owns its strings; optional ones are NULL.
*/

#ifndef __CORE_BOOT_ENTRY_H__
#define __CORE_BOOT_ENTRY_H__

#include "Base/OablBase.h"

#include "Base/Buffer.h"
#include "Base/Text.h"

// The +LEFT-DONE try counter in an entry's file name. Defined here so this
// header doesn't need Bls.h.
typedef struct _BLS_COUNTER {
  UINT32 Left;
  UINT32 Done;
} BLS_COUNTER;

typedef struct _BOOT_ENTRY_STRINGS
OABL_VEC (CHAR8 *) BOOT_ENTRY_STRINGS;

VOID
BootEntryStringsFree (IN OUT BOOT_ENTRY_STRINGS *Strings);

typedef enum {
  // A Linux kernel with the EFI stub. The command line becomes its load
  // options and the initrds are served, concatenated, through the
  // LINUX_EFI_INITRD_MEDIA LoadFile2 handle.
  BOOT_ENTRY_ACTION_LINUX,
  // The efi program of an entry, started with Options as its load options.
  BOOT_ENTRY_ACTION_EFI,
} BOOT_ENTRY_ACTION_KIND;

// Fields of the kind not in use are NULL or empty.
typedef struct _BOOT_ENTRY_ACTION {
  BOOT_ENTRY_ACTION_KIND Kind;
  // Linux
  CHAR8             *Kernel;
  BOOT_ENTRY_STRINGS Initrds;
  // Never NULL.
  CHAR8             *Cmdline;
  BOOT_ENTRY_STRINGS Overlays;
  // Efi
  CHAR8 *Path;
  CHAR8 *Options;
  // Both
  CHAR8 *Devicetree;
} BOOT_ENTRY_ACTION;

typedef struct _BOOT_ENTRY {
  // Boot Loader Specification id: the file name without its boot counter
  // (debian-6.12.conf).
  CHAR8            *Id;
  CHAR8            *Title;
  CHAR8            *Version;
  CHAR8            *SortKey;
  CHAR8            *MachineId;
  BOOLEAN           HasCounter;
  BLS_COUNTER       Counter;
  // The file that describes the entry. Boot counting renames it.
  CHAR8            *File;
  BOOT_ENTRY_ACTION Action;
} BOOT_ENTRY;

typedef struct _BOOT_ENTRY_LIST OABL_VEC (BOOT_ENTRY) BOOT_ENTRY_LIST;

VOID
BootEntryActionFree (IN OUT BOOT_ENTRY_ACTION *Action);

VOID
BootEntryFree (IN OUT BOOT_ENTRY *Entry);

VOID
BootEntryListFree (IN OUT BOOT_ENTRY_LIST *Entries);

// Deep-copies Entry. Out is left empty on failure.
BOOLEAN
BootEntryClone (
  IN CONST BOOT_ENTRY *Entry,
  OUT BOOT_ENTRY      *Out
);

// Returns TRUE if the entry has no tries left.
BOOLEAN
BootEntryIsBad (IN CONST BOOT_ENTRY *Entry);

// Turns a specification path (/vmlinuz, EFI/fedora/grubaa64.efi) into a
// UEFI path from the filesystem root (\vmlinuz).
CHAR8 *
BootEntryUefiPath (IN OABL_TEXT Path);

// Applies BootEntryUefiPath to each string. Out is left empty on failure.
BOOLEAN
BootEntryUefiPaths (
  IN CONST BOOT_ENTRY_STRINGS *In,
  OUT BOOT_ENTRY_STRINGS      *Out
);

// Returns the directory part of a UEFI path: \loader\entries\a.conf gives
// \loader\entries.
OABL_TEXT
BootEntryParent (IN CONST CHAR8 *Path);

#endif
