/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Variables - OpenABL's settings, kept in the misc partition.

  Core/Settings has the names and encodings, Core/SettingsStore the layout in
  the last 4 KiB of misc. Every device keeps its settings there, so they
  don't depend on XBL's variable services or on devinfo.

  The store is read once into memory. Gets and puts work on that copy, and
  VariablesFlush writes it back, so a put is kept only once it is flushed.
*/

#ifndef __EFI_VARIABLES_H__
#define __EFI_VARIABLES_H__

#include "Base/OablBase.h"

#include "Efi/Efi.h"

#include "Base/Buffer.h"
#include "Efi/Serial.h"

// Reads the store from misc if it isn't read yet, logging to Log from now
// on. FALSE if misc can't be found or read. After that only a call with a
// Log tries again; gets and puts load with LOG_QUIET.
BOOLEAN
VariablesLoad (IN LOG_SINK Log);

// Reads a setting's value. FALSE if it is unset or the store can't be read.
// Caller frees Out with BufferFree.
BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
);

// Reads a setting into Buffer from the copy in memory, without reading misc
// or allocating, so it works at ExitBootServices. FALSE if the store isn't
// read yet, the setting is unset or longer than Capacity.
BOOLEAN
VariablesPeek (
  IN CONST CHAR8 *Name,
  OUT UINT8      *Buffer,
  IN UINTN        Capacity,
  OUT UINTN      *Length
);

// Sets a setting, without flushing. Length 0 deletes it, and deleting a
// missing setting succeeds. EFI_NOT_FOUND without a usable misc,
// EFI_WRITE_PROTECTED when the end of misc holds something else's data,
// EFI_INVALID_PARAMETER for a name the store can't hold and
// EFI_OUT_OF_RESOURCES when the store is full.
EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
);

// Writes the store to misc if a put changed it. EFI_SUCCESS when there was
// nothing to write.
EFI_STATUS
VariablesFlush (VOID);

// Once, when the store holds no settings yet: marks it with the settings'
// schema, so the menu can change them, and flushes. Run early at each boot.
VOID
VariablesSetUp (IN LOG_SINK Log);

#endif
