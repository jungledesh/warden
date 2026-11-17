//===-- warden.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform.h"

#if SCUDO_LINUX

#include "common.h"
#include "warden/note.h"
#include "warden/sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <elf.h>
#include <string.h>

#ifndef ElfW
#if UINTPTR_MAX > 0xffffffffu
#define ElfW(type) Elf64_##type
#else
#define ElfW(type) Elf32_##type
#endif
#endif

namespace scudo {
namespace {

struct Row {
  uptr Start;
  uptr Size;
};

const Row *RowsPtr = nullptr;
uptr RowCount = 0;
int Active = 0;

enum class Fail : u8 {
  None,
  FileMissing,
  PathInvalid,
  OpenFailed,
  ReadFailed,
  Invalid,
  NotesUnreadable,
  SlotMissing,
  SlotInvalid,
  Hash,
  BuildId,
  Both,
  KeepRows,
  NoteUnmapped,
};

void logFail(Fail Why) {
  const char *Line = nullptr;
  switch (Why) {
  case Fail::FileMissing:
    Line = "warden: map file missing\n";
    break;
  case Fail::PathInvalid:
    Line = "warden: map path invalid\n";
    break;
  case Fail::OpenFailed:
    Line = "warden: cannot open map file\n";
    break;
  case Fail::ReadFailed:
    Line = "warden: cannot read map file\n";
    break;
  case Fail::Invalid:
    Line = "warden: map invalid\n";
    break;
  case Fail::NotesUnreadable:
    Line = "warden: cannot read the build id or the hash slot\n";
    break;
  case Fail::SlotMissing:
    Line = "warden: hash slot missing\n";
    break;
  case Fail::SlotInvalid:
    Line = "warden: hash slot invalid\n";
    break;
  case Fail::Hash:
    Line = "warden: map hash mismatch\n";
    break;
  case Fail::BuildId:
    Line = "warden: build id mismatch\n";
    break;
  case Fail::Both:
    Line = "warden: map hash mismatch, build id mismatch\n";
    break;
  case Fail::KeepRows:
    Line = "warden: cannot keep rows\n";
    break;
  case Fail::NoteUnmapped:
    Line = "warden: a record in the binary is not loaded into memory, map ignored\n";
    break;
  case Fail::None:
    Line = "warden: hash and build id match\n";
    break;
  }
  if (!Line)
    return;
  uptr N = 0;
  while (Line[N] != '\0')
    N++;
  (void)write(2, Line, N);
}

int hexVal(u8 C) {
  if (C >= '0' && C <= '9')
    return C - '0';
  if (C >= 'a' && C <= 'f')
    return C - 'a' + 10;
  return -1;
}

bool parseHex(const u8 *P, uptr N, u8 *Out, uptr *OutLen) {
  if (N == 0 || (N & 1) != 0 || N / 2 > WARDEN_BUILD_ID_MAX)
    return false;
  for (uptr I = 0; I < N; I += 2) {
    int Hi = hexVal(P[I]);
    int Lo = hexVal(P[I + 1]);
    if (Hi < 0 || Lo < 0)
      return false;
    Out[I / 2] = static_cast<u8>((Hi << 4) | Lo);
  }
  *OutLen = N / 2;
  return true;
}

bool parseFixed(const u8 *P, uptr N, uptr *Out) {
  uptr V = 0;
  for (uptr I = 0; I < N; I++) {
    int D = hexVal(P[I]);
    if (D < 0)
      return false;
    if (V > (~(uptr)0 >> 4))
      return false;
    V = (V << 4) | static_cast<uptr>(D);
  }
  *Out = V;
  return true;
}

const u8 *eol(const u8 *P, const u8 *End) {
  while (P < End && *P != '\n') {
    if (*P == '\r')
      return nullptr;
    P++;
  }
  if (P >= End)
    return nullptr;
  return P;
}

Fail imageNotes(WardenNoteSet *Set, uptr *Bias) {
  unsigned long PhdrAddr = getauxval(AT_PHDR);
  unsigned long Phnum = getauxval(AT_PHNUM);
  unsigned long Phent = getauxval(AT_PHENT);
  if (PhdrAddr == 0 || Phnum == 0 || Phnum > 128 || Phent != sizeof(ElfW(Phdr)))
    return Fail::NotesUnreadable;
  const ElfW(Phdr) *Phdrs = reinterpret_cast<const ElfW(Phdr) *>(PhdrAddr);
  uptr LoadBias = 0;
  bool HaveBias = false;
  for (unsigned long I = 0; I < Phnum; I++) {
    if (Phdrs[I].p_type != PT_PHDR)
      continue;
    if (PhdrAddr < Phdrs[I].p_vaddr)
      return Fail::NotesUnreadable;
    LoadBias = static_cast<uptr>(PhdrAddr - Phdrs[I].p_vaddr);
    HaveBias = true;
  }
  if (!HaveBias)
    return Fail::NotesUnreadable;

  struct Load {
    uptr Addr;
    uptr Size;
  } Loads[128];
  uptr NLoads = 0;
  for (unsigned long I = 0; I < Phnum; I++) {
    if (Phdrs[I].p_type != PT_LOAD)
      continue;
    if (Phdrs[I].p_memsz > ~static_cast<ElfW(Addr)>(0) - Phdrs[I].p_vaddr)
      return Fail::NotesUnreadable;
    if (NLoads >= 128)
      return Fail::NotesUnreadable;
    Loads[NLoads].Addr = static_cast<uptr>(LoadBias + Phdrs[I].p_vaddr);
    Loads[NLoads].Size = static_cast<uptr>(Phdrs[I].p_memsz);
    NLoads++;
  }

  memset(Set, 0, sizeof(*Set));
  for (unsigned long I = 0; I < Phnum; I++) {
    if (Phdrs[I].p_type != PT_NOTE)
      continue;
    if (Phdrs[I].p_filesz > SIZE_MAX)
      return Fail::NotesUnreadable;
    if (Phdrs[I].p_filesz > ~static_cast<ElfW(Addr)>(0) - Phdrs[I].p_vaddr)
      return Fail::NotesUnreadable;
    uptr Addr = static_cast<uptr>(LoadBias + Phdrs[I].p_vaddr);
    uptr Size = static_cast<uptr>(Phdrs[I].p_filesz);
    bool InLoad = false;
    for (uptr L = 0; L < NLoads; L++) {
      uptr Off;
      if (Addr < Loads[L].Addr)
        continue;
      Off = Addr - Loads[L].Addr;
      if (Off > Loads[L].Size || Size > Loads[L].Size - Off)
        continue;
      InLoad = true;
      break;
    }
    if (!InLoad)
      return Fail::NoteUnmapped;
    if (!warden_notes_collect(reinterpret_cast<const u8 *>(Addr), Size, 0, Set))
      return Fail::NotesUnreadable;
  }
  *Bias = LoadBias;
  return Fail::None;
}

bool rowsOk(const u8 *P, const u8 *End, uptr Bias, uptr *Count) {
  uptr N = 0;
  uptr Prev = 0;
  uptr PrevSize = 0;
  bool Have = false;
  while (P < End) {
    const u8 *Nl = eol(P, End);
    uptr Start = 0;
    uptr Size = 0;
    if (!Nl || Nl - P != 33 || P[16] != ' ')
      return false;
    if (!parseFixed(P, 16, &Start) || !parseFixed(P + 17, 16, &Size))
      return false;
    if (Size == 0 || Size > ~static_cast<uptr>(0) - Start)
      return false;
    if (Have && Start < Prev + PrevSize)
      return false;
    if (Start > ~static_cast<uptr>(0) - Bias)
      return false;
    /* Start + Bias fits. Size must fit after that shift too. */
    if (Size > ~static_cast<uptr>(0) - (Start + Bias))
      return false;
    Prev = Start;
    PrevSize = Size;
    Have = true;
    N++;
    if (N > WARDEN_ROW_MAX)
      return false;
    P = Nl + 1;
  }
  *Count = N;
  return true;
}

void fillRows(const u8 *P, const u8 *End, uptr Bias, Row *Out) {
  uptr I = 0;
  while (P < End) {
    const u8 *Nl = eol(P, End);
    uptr Start = 0;
    uptr Size = 0;
    parseFixed(P, 16, &Start);
    parseFixed(P + 17, 16, &Size);
    Out[I].Start = Start + Bias;
    Out[I].Size = Size;
    I++;
    P = Nl + 1;
  }
}

Fail loadMap(const u8 *Data, uptr Len, const WardenNoteSet *Notes, uptr Bias) {
  static const char Prefix[] = "build-id ";
  static const char Header[] = "offset size";
  const u8 *P = Data;
  const u8 *End = Data + Len;
  const u8 *Nl;
  u8 MapId[WARDEN_BUILD_ID_MAX];
  uptr MapIdLen = 0;
  u8 Hash[WARDEN_HASH_LEN];
  bool HashOk;
  bool BuildOk;
  uptr Count = 0;
  Row *Table = nullptr;

  if (Len < sizeof(Prefix) || memcmp(P, Prefix, sizeof(Prefix) - 1) != 0)
    return Fail::Invalid;
  P += sizeof(Prefix) - 1;
  Nl = eol(P, End);
  if (!Nl || !parseHex(P, static_cast<uptr>(Nl - P), MapId, &MapIdLen))
    return Fail::Invalid;
  P = Nl + 1;
  Nl = eol(P, End);
  if (!Nl || Nl - P != sizeof(Header) - 1 ||
      memcmp(P, Header, sizeof(Header) - 1) != 0)
    return Fail::Invalid;
  P = Nl + 1;

  warden_sha256(Data, Len, Hash);
  if (Notes->HashCount == 0)
    return Fail::SlotMissing;
  if (Notes->HashCount != 1 || Notes->Hash == nullptr)
    return Fail::SlotInvalid;
  HashOk = warden_memeq(Hash, Notes->Hash, WARDEN_HASH_LEN) != 0;
  BuildOk = !Notes->BuildIdTooLong && Notes->BuildIdCount == 1 &&
            Notes->BuildId != nullptr && Notes->BuildIdLen == MapIdLen &&
            warden_memeq(Notes->BuildId, MapId, MapIdLen) != 0;
  if (!HashOk && !BuildOk)
    return Fail::Both;
  if (!HashOk)
    return Fail::Hash;
  if (!BuildOk)
    return Fail::BuildId;
  if (!rowsOk(P, End, Bias, &Count))
    return Fail::Invalid;
  if (Count == 0) {
    __atomic_store_n(&RowCount, static_cast<uptr>(0), __ATOMIC_RELAXED);
    __atomic_store_n(&RowsPtr, static_cast<const Row *>(nullptr),
                     __ATOMIC_RELAXED);
    __atomic_store_n(&Active, 1, __ATOMIC_RELEASE);
    return Fail::None;
  }
  {
    uptr Bytes = Count * sizeof(Row);
    void *Mem = mmap(nullptr, Bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (Mem == MAP_FAILED)
      return Fail::KeepRows;
    Table = static_cast<Row *>(Mem);
    fillRows(P, End, Bias, Table);
    if (mprotect(Table, Bytes, PROT_READ) != 0) {
      munmap(Table, Bytes);
      return Fail::KeepRows;
    }
  }
  __atomic_store_n(&RowCount, Count, __ATOMIC_RELAXED);
  __atomic_store_n(&RowsPtr, static_cast<const Row *>(Table), __ATOMIC_RELAXED);
  __atomic_store_n(&Active, 1, __ATOMIC_RELEASE);
  return Fail::None;
}

__attribute__((constructor(101), used)) void wardenInit() {
  const char *Path = getEnv("WARDEN_MAP");
  uptr PathLen = 0;
  int Fd = -1;
  struct stat St;
  u8 *Data = nullptr;
  uptr Len = 0;
  WardenNoteSet Notes;
  uptr Bias = 0;
  Fail Why = Fail::None;

  if (!Path || Path[0] == '\0') {
    logFail(Fail::FileMissing);
    return;
  }
  for (; Path[PathLen] != '\0'; PathLen++) {
    if (Path[PathLen] == '\n' || PathLen >= 4096) {
      logFail(Fail::PathInvalid);
      return;
    }
  }
  Fd = open(Path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (Fd < 0) {
    logFail(errno == ENOENT ? Fail::FileMissing : Fail::OpenFailed);
    return;
  }
  if (fstat(Fd, &St) != 0 || !S_ISREG(St.st_mode)) {
    close(Fd);
    logFail(Fail::OpenFailed);
    return;
  }
  if (St.st_size <= 0 || static_cast<uint64_t>(St.st_size) > WARDEN_MAP_MAX) {
    close(Fd);
    logFail(Fail::Invalid);
    return;
  }
  Len = static_cast<uptr>(St.st_size);
  {
    void *Mapped = mmap(nullptr, Len, PROT_READ, MAP_PRIVATE, Fd, 0);
    close(Fd);
    if (Mapped == MAP_FAILED) {
      logFail(Fail::ReadFailed);
      return;
    }
    Data = static_cast<u8 *>(Mapped);
  }
  Why = imageNotes(&Notes, &Bias);
  if (Why != Fail::None) {
    munmap(Data, Len);
    logFail(Why);
    return;
  }
  Why = loadMap(Data, Len, &Notes, Bias);
  munmap(Data, Len);
  logFail(Why);
}

} // namespace
} // namespace scudo

#else

#endif
