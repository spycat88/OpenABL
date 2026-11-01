/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  AndroidBcb - the bootloader message in Android's misc partition.
*/

#include "Core/AndroidBcb.h"

#include "Base/Memory.h"
#include "Base/Text.h"

VOID
AndroidBcbCommand (
  IN CONST CHAR8 *Command,
  OUT UINT8       Out[ANDROID_BCB_COMMAND_LEN]
)
{
  UINTN Length = AsciiStrLen (Command);

  if (Length > ANDROID_BCB_COMMAND_LEN - 1) {
    Length = ANDROID_BCB_COMMAND_LEN - 1;
  }
  for (UINTN Index = 0; Index < ANDROID_BCB_COMMAND_LEN; Index++) {
    Out[Index] = Index < Length ? (UINT8)Command[Index] : 0;
  }
}

VOID
AndroidBcbWipe (OUT UINT8 Out[ANDROID_BCB_SIZE])
{
  STATIC CONST CHAR8 Arguments[] = "recovery\n--wipe_data\n";

  ZeroMem (Out, ANDROID_BCB_SIZE);
  AndroidBcbCommand ("boot-recovery", Out);
  CopyMem (Out + ANDROID_BCB_RECOVERY_AT, Arguments, sizeof (Arguments) - 1);
}
