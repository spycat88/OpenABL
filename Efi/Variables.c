/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Variables - OpenABL's settings, kept in the misc partition.
*/

#include "Efi/Variables.h"

#include "Base/Memory.h"
#include "Base/Text.h"
#include "Core/Settings.h"
#include "Core/SettingsStore.h"
#include "Efi/Uninstall.h"

#define STATUS_TEXT(Status) StatusText ((Status), (CHAR8[STATUS_TEXT_SIZE]){0})

// The store as read from misc, and where it is. Static, so a peek at
// ExitBootServices needs no allocation.
STATIC struct {
  BOOLEAN                Loaded;
  // FALSE when the end of misc holds something else's data.
  BOOLEAN                Writable;
  // A put changed Store since the last flush.
  BOOLEAN                Dirty;
  // A load failed, and was logged.
  BOOLEAN                Missed;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  // The region's first block.
  UINT64                 Lba;
  UINT8                  Store[SETTINGS_STORE_SIZE];
} mStore;

STATIC LOG_SINK mLog;

// Finds the region at the end of misc. FALSE, logging why, if there is no
// single misc or it is too small for the store after Android's space.
STATIC BOOLEAN
Locate (VOID)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo;
  UINT64                 First;
  UINT64                 Last;
  UINTN                  BlockSize;

  if (!UninstallFindMisc (&BlockIo, &First, &Last, mLog)) {
    if (!mStore.Missed) {
      LogLine (mLog, "settings: no single misc partition");
    }
    return FALSE;
  }
  BlockSize = BlockIo->Media->BlockSize;
  if (BlockSize == 0 || BlockSize > SETTINGS_STORE_SIZE ||
      SETTINGS_STORE_SIZE % BlockSize != 0 || Last < First ||
      (Last - First + 1) * BlockSize < SETTINGS_STORE_MISC_MIN) {
    if (!mStore.Missed) {
      LogPrint (
        mLog, "settings: misc is %llu blocks of %llu bytes: too small",
        Last < First ? 0 : Last - First + 1, (UINT64)BlockSize
      );
    }
    return FALSE;
  }
  mStore.BlockIo = BlockIo;
  mStore.Lba     = Last + 1 - SETTINGS_STORE_SIZE / BlockSize;
  return TRUE;
}

BOOLEAN
VariablesLoad (IN LOG_SINK Log)
{
  OABL_BUFFER     Raw = {0};
  SETTINGS_STORE_STATE State;

  if (Log.Fn) {
    mLog = Log;
  }
  if (mStore.Loaded) {
    return TRUE;
  }
  // Only an explicit load looks for misc again after a miss, so the gets
  // that follow don't each scan the disks.
  if (mStore.Missed && !Log.Fn) {
    return FALSE;
  }
  if (!Locate ()) {
    mStore.Missed = TRUE;
    return FALSE;
  }
  if (!UninstallRead (mStore.BlockIo, mStore.Lba, SETTINGS_STORE_SIZE, &Raw) ||
      Raw.Len != SETTINGS_STORE_SIZE) {
    if (!mStore.Missed) {
      LogLine (mLog, "settings: reading misc failed");
    }
    mStore.Missed = TRUE;
    BufferFree (&Raw);
    return FALSE;
  }
  CopyMem (mStore.Store, Raw.Data, SETTINGS_STORE_SIZE);
  BufferFree (&Raw);
  State = SettingsStoreOpen (mStore.Store);
  mStore.Loaded   = TRUE;
  mStore.Writable = State == SETTINGS_STORE_FOUND ||
                    State == SETTINGS_STORE_BLANK ||
                    State == SETTINGS_STORE_DAMAGED;
  switch (State) {
  case SETTINGS_STORE_FOUND:
    LogLine (mLog, "settings: in misc");
    break;
  case SETTINGS_STORE_BLANK:
    LogLine (mLog, "settings: none in misc yet");
    break;
  case SETTINGS_STORE_DAMAGED:
    LogLine (mLog, "settings: misc's copy is damaged: starting again");
    break;
  case SETTINGS_STORE_NEWER:
    LogLine (
      mLog, "settings: misc has a newer OpenABL's settings: left alone"
    );
    break;
  default:
    LogLine (
      mLog, "settings: the end of misc holds other data: left alone"
    );
    break;
  }
  return TRUE;
}

BOOLEAN
VariablesGet (
  IN CONST CHAR8  *Name,
  OUT OABL_BUFFER *Out
)
{
  OABL_BYTES Value;

  *Out = (OABL_BUFFER){0};
  if (!VariablesLoad (LOG_QUIET) || !mStore.Writable ||
      !SettingsStoreGet (mStore.Store, Name, &Value)) {
    return FALSE;
  }
  return BufferFrom (Out, Value.Ptr, Value.Len);
}

BOOLEAN
VariablesPeek (
  IN CONST CHAR8 *Name,
  OUT UINT8      *Buffer,
  IN UINTN        Capacity,
  OUT UINTN      *Length
)
{
  OABL_BYTES Value;

  if (!mStore.Loaded || !mStore.Writable ||
      !SettingsStoreGet (mStore.Store, Name, &Value) || Value.Len > Capacity) {
    return FALSE;
  }
  CopyMem (Buffer, Value.Ptr, Value.Len);
  *Length = Value.Len;
  return TRUE;
}

EFI_STATUS
VariablesPut (
  IN CONST CHAR8 *Name,
  IN CONST UINT8 *Data OPTIONAL,
  IN UINTN        Length
)
{
  OABL_BYTES Old;
  BOOLEAN    Found;

  if (!VariablesLoad (LOG_QUIET)) {
    return EFI_NOT_FOUND;
  }
  if (!mStore.Writable) {
    return EFI_WRITE_PROTECTED;
  }
  if (!SettingsStoreNameValid (Name)) {
    return EFI_INVALID_PARAMETER;
  }
  Found = SettingsStoreGet (mStore.Store, Name, &Old);
  // Nothing to change, so nothing to write.
  if (Length == 0 ? !Found
                  : Found && Old.Len == Length &&
                      CompareMem (Old.Ptr, Data, Length) == 0) {
    return EFI_SUCCESS;
  }
  if (!SettingsStoreSet (mStore.Store, Name, Data, Length)) {
    return EFI_OUT_OF_RESOURCES;
  }
  mStore.Dirty = TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS
VariablesFlush (VOID)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo = mStore.BlockIo;
  EFI_STATUS             Status;

  if (!mStore.Dirty) {
    return EFI_SUCCESS;
  }
  if (!mStore.Loaded || !mStore.Writable) {
    return EFI_WRITE_PROTECTED;
  }
  SettingsStoreSeal (mStore.Store);
  Status =
    UninstallWrite (BlockIo, mStore.Lba, mStore.Store, SETTINGS_STORE_SIZE);
  if (Status == EFI_SUCCESS) {
    Status = BlockIo->FlushBlocks (BlockIo);
  }
  if (Status != EFI_SUCCESS) {
    LogPrint (mLog, "settings: writing misc failed: %s", STATUS_TEXT (Status));
    return Status;
  }
  mStore.Dirty = FALSE;
  return EFI_SUCCESS;
}

VOID
VariablesSetUp (IN LOG_SINK Log)
{
  STATIC CONST UINT8 Schema = SETTINGS_SCHEMA;
  OABL_BYTES         Held;
  EFI_STATUS         Status;

  if (!VariablesLoad (Log) || !mStore.Writable ||
      SettingsStoreGet (mStore.Store, SETTINGS_KEY_SCHEMA, &Held)) {
    return;
  }
  Status = VariablesPut (SETTINGS_KEY_SCHEMA, &Schema, 1);
  if (Status == EFI_SUCCESS) {
    Status = VariablesFlush ();
  }
  if (Status != EFI_SUCCESS) {
    LogPrint (
      mLog, "settings: setting up misc failed: %s", STATUS_TEXT (Status)
    );
    return;
  }
  LogLine (mLog, "settings: set up in misc");
}
