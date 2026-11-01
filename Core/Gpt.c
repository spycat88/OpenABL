/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026-present Open Handheld Collective
 *   https://github.com/OpenHandheldCollective
 */

/**
  Gpt - GPT parsing and rewriting for Uninstall CFW.
*/

#include "Core/Gpt.h"

#include "Base/Memory.h"

CONST UINT8 gGptAndroidUserdata[16] = {0xe6, 0xe7, 0x81, 0x1b, 0x0d, 0xf5,
                                       0x9b, 0x41, 0xa7, 0x39, 0x2a, 0xee,
                                       0xf8, 0xda, 0x33, 0x35};
CONST UINT8 gGptLinuxFs[16] = {0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                               0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};

STATIC CONST CHAR8 mUserdata[] = "userdata";
// Entry name: 36 UTF-16 code units at byte 56.
#define NAME_AT   56
#define NAME_LEN  72
#define MIN_ENTRY 128
// Well above any real table; 128 entries of 128 bytes take 16 KiB.
#define MAX_ARRAY ((UINT64)1 << 20)

STATIC BOOLEAN
BytesEq (
  IN CONST UINT8 *A,
  IN CONST UINT8 *B,
  IN UINTN        Size
)
{
  for (UINTN Index = 0; Index < Size; Index++) {
    if (A[Index] != B[Index]) {
      return FALSE;
    }
  }
  return TRUE;
}

BOOLEAN
GptParse (
  IN CONST UINT8 *Block,
  IN UINTN        Len,
  OUT GPT_HEADER *Out
)
{
  GPT_HEADER Header;

  if (Len < 92 || !BytesEq (Block, (CONST UINT8 *)GPT_SIGNATURE, 8)) {
    return FALSE;
  }
  Header.HeaderSize   = ReadLe32 (Block + 12);
  Header.MyLba        = ReadLe64 (Block + 24);
  Header.AlternateLba = ReadLe64 (Block + 32);
  Header.FirstUsable  = ReadLe64 (Block + 40);
  Header.LastUsable   = ReadLe64 (Block + 48);
  Header.EntryLba     = ReadLe64 (Block + 72);
  Header.Entries      = ReadLe32 (Block + 80);
  Header.EntrySize    = ReadLe32 (Block + 84);
  if (Header.HeaderSize < 92 || Header.HeaderSize > Len ||
      Header.EntrySize < MIN_ENTRY || Header.EntrySize % 8 ||
      (UINT64)Header.Entries * Header.EntrySize > MAX_ARRAY) {
    return FALSE;
  }
  *Out = Header;
  return TRUE;
}

UINTN
GptArraySize (IN CONST GPT_HEADER *Header)
{
  return (UINTN)Header->Entries * Header->EntrySize;
}

// Number of whole entries in ArrayLen bytes, at most the header's count.
// Entries under 128 bytes are too short for the fields read, so 0.
STATIC UINTN
EntryCount (
  IN CONST GPT_HEADER *Header,
  IN UINTN             ArrayLen
)
{
  UINTN Count;

  if (Header->EntrySize < MIN_ENTRY) {
    return 0;
  }
  Count = ArrayLen / Header->EntrySize;
  return Count < Header->Entries ? Count : Header->Entries;
}

// An unused entry has an all-zero type.
STATIC BOOLEAN
InUse (IN CONST UINT8 *Entry)
{
  for (INT32 Index = 0; Index < 16; Index++) {
    if (Entry[Index]) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC BOOLEAN
Named (
  IN CONST UINT8 *Entry,
  IN CONST CHAR8 *Name
)
{
  UINTN Len = AsciiStrLen (Name);

  for (UINTN Index = 0; Index < NAME_LEN / 2; Index++) {
    UINT16 Unit = ReadLe16 (Entry + NAME_AT + 2 * Index);
    UINT16 Want = Index < Len ? (UINT8)Name[Index] : 0;
    if (Unit != Want) {
      return FALSE;
    }
    if (Unit == 0) {
      return TRUE;
    }
  }
  // All 36 units used, with no terminator.
  return Len == NAME_LEN / 2;
}

// Finds the one in-use userdata entry. FALSE if there is none, or more than
// one, as then which to grow is a guess.
STATIC BOOLEAN
UserdataIndex (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  OUT UINTN           *Found
)
{
  UINTN   Count = EntryCount (Header, ArrayLen);
  BOOLEAN Seen  = FALSE;

  for (UINTN Index = 0; Index < Count; Index++) {
    CONST UINT8 *Entry = Array + Index * Header->EntrySize;
    if (InUse (Entry) && Named (Entry, mUserdata)) {
      if (Seen) {
        return FALSE;
      }
      Seen   = TRUE;
      *Found = Index;
    }
  }
  return Seen;
}

// Whether an entry is Linux's: in use and on disk after userdata, which
// stock Android leaves last. The disk order counts, not the entry order.
STATIC BOOLEAN
AfterUserdata (
  IN CONST UINT8 *Entry,
  IN UINT64       UserdataEnd
)
{
  return InUse (Entry) && ReadLe64 (Entry + 32) > UserdataEnd;
}

BOOLEAN
GptCfwAfterUserdata (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen
)
{
  return GptCfwParts (Header, Array, ArrayLen, 0, NULL, 0) > 0;
}

UINTN
GptCfwParts (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             BlockSize,
  OUT GPT_CFW_PART    *Out,
  IN UINTN             Max
)
{
  UINTN  Count = EntryCount (Header, ArrayLen);
  UINTN  Found = 0;
  UINTN  Userdata;
  UINT64 End;

  if (!UserdataIndex (Header, Array, ArrayLen, &Userdata)) {
    return 0;
  }
  End = ReadLe64 (Array + Userdata * Header->EntrySize + 40);
  for (UINTN Index = 0; Index < Count; Index++) {
    CONST UINT8  *Entry = Array + Index * Header->EntrySize;
    GPT_CFW_PART *Part;
    UINT64        First;
    UINT64        Last;
    UINTN         Len = 0;

    if (!AfterUserdata (Entry, End)) {
      continue;
    }
    if (Found++ >= Max) {
      continue;
    }
    Part  = &Out[Found - 1];
    First = ReadLe64 (Entry + 32);
    Last  = ReadLe64 (Entry + 40);
    Part->Bytes =
      Last >= First ? (Last - First + 1) * (UINT64)BlockSize : 0;
    // ASCII as is, anything else as '?'; names are ASCII in practice.
    for (UINTN Unit = 0; Unit < NAME_LEN / 2; Unit++) {
      UINT16 Char = ReadLe16 (Entry + NAME_AT + 2 * Unit);
      if (Char == 0) {
        break;
      }
      Part->Name[Len++] = Char >= 0x20 && Char < 0x7f ? (CHAR8)Char : '?';
    }
    Part->Name[Len] = 0;
  }
  return Found;
}

BOOLEAN
GptEntryNamed (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  IN CONST CHAR8      *Name
)
{
  CONST UINT8 *Entry;

  if (Index >= EntryCount (Header, ArrayLen)) {
    return FALSE;
  }
  Entry = Array + Index * Header->EntrySize;
  return InUse (Entry) && Named (Entry, Name);
}

VOID
GptWritesFree (IN OUT GPT_WRITES *Writes)
{
  for (UINTN Index = 0; Index < Writes->Count; Index++) {
    BufferFree (&Writes->Items[Index].Data);
  }
  VecFree (Writes);
}

// Sets the header CRC over its first Size bytes, with the CRC field zeroed.
STATIC VOID
SetCrc (
  IN OUT UINT8 *Header,
  IN UINTN      Size
)
{
  WriteLe32 (Header + 16, 0);
  WriteLe32 (Header + 16, CalculateCrc32 (Header, Size));
}

STATIC GPT_WRITE
WriteOf (
  IN UINT64 Lba,
  IN UINT8 *Data,
  IN UINTN  Size
)
{
  return (GPT_WRITE){Lba, {Data, Size, Size}};
}

// Whether Blocks blocks from Lba fit in [Low, High).
STATIC BOOLEAN
Within (
  IN UINT64 Lba,
  IN UINT64 Blocks,
  IN UINT64 Low,
  IN UINT64 High
)
{
  return Lba >= Low && Lba < High && Blocks <= High - Lba;
}

// Builds the writes for both tables, taking ownership of Array. The backup
// goes first, entries before header, so losing power part way leaves one
// whole table: the new backup or the old primary. BackupEntries is where
// the backup's array is; both arrays must lie outside the usable blocks.
STATIC BOOLEAN
Tables (
  IN CONST UINT8      *Header,
  IN UINTN             HeaderLen,
  IN CONST GPT_HEADER *Parsed,
  IN UINT8            *Array,
  IN UINTN             ArrayLen,
  IN UINTN             BlockSize,
  IN UINT64            BackupEntries,
  OUT GPT_WRITES      *Out
)
{
  UINTN      Size   = GptArraySize (Parsed);
  UINT64     Blocks = (Size + BlockSize - 1) / BlockSize;
  UINT8     *Primary = NULL;
  UINT8     *Backup  = NULL;
  UINT8     *Array2  = NULL;
  GPT_WRITES Writes  = {0};

  if (ArrayLen != Blocks * BlockSize ||
      !Within (Parsed->EntryLba, Blocks, 2, Parsed->FirstUsable) ||
      Parsed->LastUsable == MAX_UINT64 ||
      !Within (
        BackupEntries, Blocks, Parsed->LastUsable + 1, Parsed->AlternateLba
      )) {
    goto Fail;
  }
  Primary = AllocateCopyPool (HeaderLen, Header);
  if (!Primary) {
    goto Fail;
  }
  WriteLe32 (Primary + 88, CalculateCrc32 (Array, Size));
  SetCrc (Primary, Parsed->HeaderSize);
  Backup = AllocateCopyPool (HeaderLen, Primary);
  Array2 = AllocateCopyPool (ArrayLen, Array);
  if (!Backup || !Array2 || !VecReserve (&Writes, 4)) {
    goto Fail;
  }
  WriteLe64 (Backup + 24, Parsed->AlternateLba);
  WriteLe64 (Backup + 32, 1);
  WriteLe64 (Backup + 72, BackupEntries);
  SetCrc (Backup, Parsed->HeaderSize);
  // These can't fail; four slots are reserved.
  (VOID)VecPush (&Writes, WriteOf (BackupEntries, Array, ArrayLen));
  (VOID)VecPush (&Writes, WriteOf (Parsed->AlternateLba, Backup, HeaderLen));
  (VOID)VecPush (&Writes, WriteOf (Parsed->EntryLba, Array2, ArrayLen));
  (VOID)VecPush (&Writes, WriteOf (1, Primary, HeaderLen));
  *Out = Writes;
  return TRUE;
Fail:
  FreePool (Primary);
  FreePool (Backup);
  FreePool (Array2);
  FreePool (Array);
  VecFree (&Writes);
  return FALSE;
}

BOOLEAN
GptUninstall (
  IN CONST UINT8 *Header,
  IN UINTN        HeaderLen,
  IN CONST UINT8 *Array,
  IN UINTN        ArrayLen,
  IN UINTN        BlockSize,
  IN UINT64       BackupEntryLba,
  OUT GPT_WRITES *Out
)
{
  GPT_HEADER Parsed;
  UINT8     *Copy;
  UINT8     *Userdata;
  UINTN      Count;
  UINTN      Index;
  UINT64     Start;
  UINT64     End;
  BOOLEAN    Removed = FALSE;

  if (!GptParse (Header, HeaderLen, &Parsed)) {
    return FALSE;
  }
  if (BlockSize == 0 || ArrayLen < GptArraySize (&Parsed) ||
      !UserdataIndex (&Parsed, Array, ArrayLen, &Index)) {
    return FALSE;
  }
  Copy = AllocateCopyPool (ArrayLen, Array);
  if (!Copy) {
    return FALSE;
  }
  Userdata = Copy + Index * Parsed.EntrySize;
  Start    = ReadLe64 (Userdata + 32);
  End      = ReadLe64 (Userdata + 40);
  Count    = EntryCount (&Parsed, ArrayLen);
  if (Start < Parsed.FirstUsable || End < Start || End > Parsed.LastUsable) {
    goto Fail;
  }
  for (UINTN Other = 0; Other < Count; Other++) {
    UINT8 *Entry = Copy + Other * Parsed.EntrySize;
    if (AfterUserdata (Entry, End)) {
      ZeroMem (Entry, Parsed.EntrySize);
      Removed = TRUE;
    } else if (Other != Index && InUse (Entry) &&
               ReadLe64 (Entry + 40) >= Start) {
      // What's left must end before userdata, which grows over the rest.
      goto Fail;
    }
  }
  if (!Removed) {
    goto Fail;
  }
  // Only undo an installer's retyping; an OEM's own userdata type stays.
  if (BytesEq (Userdata, gGptLinuxFs, 16)) {
    CopyMem (Userdata, gGptAndroidUserdata, 16);
  }
  WriteLe64 (Userdata + 40, Parsed.LastUsable);
  return Tables (
    Header, HeaderLen, &Parsed, Copy, ArrayLen, BlockSize, BackupEntryLba, Out
  );
Fail:
  FreePool (Copy);
  return FALSE;
}

BOOLEAN
GptVerify (
  IN CONST UINT8 *Block,
  IN UINTN        Len,
  IN CONST UINT8 *Array,
  IN UINTN        ArrayLen,
  OUT GPT_HEADER *Out
)
{
  GPT_HEADER Header;
  UINT8     *Check;
  UINT32     Crc;
  UINTN      Size;

  if (!GptParse (Block, Len, &Header)) {
    return FALSE;
  }
  // The header CRC is computed with its own field zeroed.
  Check = AllocateCopyPool (Header.HeaderSize, Block);
  if (!Check) {
    return FALSE;
  }
  WriteLe32 (Check + 16, 0);
  Crc = CalculateCrc32 (Check, Header.HeaderSize);
  FreePool (Check);
  Size = GptArraySize (&Header);
  if (Crc != ReadLe32 (Block + 16) || ArrayLen < Size ||
      CalculateCrc32 (Array, Size) != ReadLe32 (Block + 88)) {
    return FALSE;
  }
  *Out = Header;
  return TRUE;
}

BOOLEAN
GptCopiesAgree (
  IN CONST GPT_HEADER *Primary,
  IN CONST GPT_HEADER *Backup,
  IN UINT64            LastBlock
)
{
  return Primary->MyLba == 1 && Backup->MyLba == Primary->AlternateLba &&
         Backup->AlternateLba == Primary->MyLba &&
         Primary->AlternateLba > Primary->MyLba &&
         Primary->AlternateLba <= LastBlock &&
         Backup->FirstUsable == Primary->FirstUsable &&
         Backup->LastUsable == Primary->LastUsable &&
         Backup->Entries == Primary->Entries &&
         Backup->EntrySize == Primary->EntrySize &&
         Primary->FirstUsable <= Primary->LastUsable &&
         Primary->LastUsable < Primary->AlternateLba;
}

BOOLEAN
GptExtent (
  IN CONST GPT_HEADER *Header,
  IN CONST UINT8      *Array,
  IN UINTN             ArrayLen,
  IN UINTN             Index,
  OUT UINT64          *Start,
  OUT UINT64          *End
)
{
  CONST UINT8 *Entry;

  if (Index >= EntryCount (Header, ArrayLen)) {
    return FALSE;
  }
  Entry = Array + Index * Header->EntrySize;
  if (!InUse (Entry)) {
    return FALSE;
  }
  *Start = ReadLe64 (Entry + 32);
  *End   = ReadLe64 (Entry + 40);
  return TRUE;
}
