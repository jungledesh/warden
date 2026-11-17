/* Write the map hash into the empty slot, or put those bytes back to zero.
   warden-stamp apply <map> <binary>
   warden-stamp clear <binary>
   warden-stamp self-test
   The write is 32 bytes over the bytes already reserved. The file does not grow.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "note.h"
#include "sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define EI_NIDENT 16u
#define ET_EXEC 2u
#define ET_DYN 3u
#define ELFMAG0 0x7fu
#define ELFCLASS32 1u
#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define PT_LOAD 1u
#define PT_NOTE 4u

struct MapBytes {
  uint8_t *Data;
  size_t Len;
  int Fd;
};

static void say(const char *Msg) {
  fprintf(stderr, "warden-stamp: %s\n", Msg);
}

static void die(const char *Msg) __attribute__((noreturn));
static void die(const char *Msg) {
  say(Msg);
  exit(2);
}

static int hex_nibble(int C) {
  if (C >= '0' && C <= '9')
    return C - '0';
  if (C >= 'a' && C <= 'f')
    return C - 'a' + 10;
  return -1;
}

static int decode_hex(const uint8_t *In, size_t N, uint8_t *Out, size_t *OutLen) {
  size_t I;
  if (N == 0 || (N & 1u) != 0 || N / 2u > WARDEN_BUILD_ID_MAX)
    return 0;
  for (I = 0; I < N; I += 2u) {
    int Hi = hex_nibble(In[I]);
    int Lo = hex_nibble(In[I + 1u]);
    if (Hi < 0 || Lo < 0)
      return 0;
    Out[I / 2u] = (uint8_t)((Hi << 4) | Lo);
  }
  *OutLen = N / 2u;
  return 1;
}

/* First line is "build-id <hex>". */
static int map_build_id(const uint8_t *Data, size_t Len, uint8_t *Out,
                        size_t *OutLen) {
  size_t I;
  static const char Prefix[] = "build-id ";
  if (Len < sizeof(Prefix) - 1u)
    return 0;
  if (memcmp(Data, Prefix, sizeof(Prefix) - 1u) != 0)
    return 0;
  I = sizeof(Prefix) - 1u;
  while (I < Len && Data[I] != '\n')
    I++;
  if (I >= Len || Data[I] != '\n')
    return 0;
  return decode_hex(Data + (sizeof(Prefix) - 1u), I - (sizeof(Prefix) - 1u), Out,
                    OutLen);
}

static uint64_t read_off(const uint8_t *P, int Class64, size_t O) {
  if (Class64) {
    uint64_t V = 0;
    int I;
    for (I = 7; I >= 0; I--)
      V = (V << 8) | P[O + (size_t)I];
    return V;
  }
  return (uint64_t)P[O] | ((uint64_t)P[O + 1] << 8) | ((uint64_t)P[O + 2] << 16) |
         ((uint64_t)P[O + 3] << 24);
}

static int range_has(uint64_t Off, uint64_t Len, uint64_t Start, uint64_t Size) {
  if (Size > UINT64_MAX - Start)
    return 0;
  if (Off < Start)
    return 0;
  if (Len > UINT64_MAX - Off)
    return 0;
  return Off + Len <= Start + Size;
}

/* On success, Set->HashOff is the file offset of the 32-byte slot. */
static int find_slot(const uint8_t *File, size_t Len, struct WardenNoteSet *Set) {
  int Class64;
  uint64_t Phoff;
  uint64_t Phent;
  uint64_t Phnum;
  uint64_t I;
  uint16_t Type;
  struct Load {
    uint64_t Off;
    uint64_t Filesz;
  } Loads[128];
  size_t NLoads = 0;

  memset(Set, 0, sizeof(*Set));
  if (Len < EI_NIDENT + 16u)
    return 0;
  if (File[0] != ELFMAG0 || File[1] != 'E' || File[2] != 'L' || File[3] != 'F')
    return 0;
  if (File[5] != ELFDATA2LSB || File[6] != 1u)
    return 0;
  if (File[4] == ELFCLASS64)
    Class64 = 1;
  else if (File[4] == ELFCLASS32)
    Class64 = 0;
  else
    return 0;
  if (Class64) {
    if (Len < 64u)
      return 0;
    Type = (uint16_t)(File[16] | (File[17] << 8));
    Phoff = read_off(File, 1, 32);
    Phent = (uint64_t)File[54] | ((uint64_t)File[55] << 8);
    Phnum = (uint64_t)File[56] | ((uint64_t)File[57] << 8);
    if (Phent < 56u)
      return 0;
  } else {
    if (Len < 52u)
      return 0;
    Type = (uint16_t)(File[16] | (File[17] << 8));
    Phoff = read_off(File, 0, 28);
    Phent = (uint64_t)File[42] | ((uint64_t)File[43] << 8);
    Phnum = (uint64_t)File[44] | ((uint64_t)File[45] << 8);
    if (Phent < 32u)
      return 0;
  }
  if (Type != ET_EXEC && Type != ET_DYN)
    return 0;
  if (Phnum == 0 || Phnum > 128u)
    return 0;
  if (Phoff > Len || Phent > (Len - Phoff) / Phnum)
    return 0;

  for (I = 0; I < Phnum; I++) {
    const uint8_t *Ph = File + Phoff + I * Phent;
    uint32_t PType = warden_read32(Ph);
    uint64_t Off;
    uint64_t Filesz;
    if (Class64) {
      Off = read_off(Ph, 1, 8);
      Filesz = read_off(Ph, 1, 32);
    } else {
      Off = read_off(Ph, 0, 4);
      Filesz = read_off(Ph, 0, 16);
    }
    if (PType == PT_LOAD) {
      if (NLoads >= 128u)
        return 0;
      Loads[NLoads].Off = Off;
      Loads[NLoads].Filesz = Filesz;
      NLoads++;
    }
  }

  for (I = 0; I < Phnum; I++) {
    const uint8_t *Ph = File + Phoff + I * Phent;
    uint32_t PType = warden_read32(Ph);
    uint64_t Off;
    uint64_t Filesz;
    size_t L;
    int InLoad = 0;
    if (PType != PT_NOTE)
      continue;
    if (Class64) {
      Off = read_off(Ph, 1, 8);
      Filesz = read_off(Ph, 1, 32);
    } else {
      Off = read_off(Ph, 0, 4);
      Filesz = read_off(Ph, 0, 16);
    }
    if (Filesz > SIZE_MAX || Off > Len || Filesz > Len - Off)
      return 0;
    for (L = 0; L < NLoads; L++) {
      if (range_has(Off, Filesz, Loads[L].Off, Loads[L].Filesz)) {
        InLoad = 1;
        break;
      }
    }
    if (!InLoad)
      return 2;
    if (!warden_notes_collect(File + Off, (size_t)Filesz, (size_t)Off, Set))
      return 0;
  }
  if (Set->HashCount == 1 && Set->Hash != NULL && Set->BuildIdCount == 1 &&
      Set->BuildId != NULL && !Set->BuildIdTooLong)
    return 1;
  return 0;
}

static void require_slot(const uint8_t *Bin, size_t BinLen, struct WardenNoteSet *Set) {
  int St = find_slot(Bin, BinLen, Set);
  if (St == 2)
    die("a record in the binary is not loaded into memory");
  if (St != 1)
    die("hash slot or build id was not found in the binary");
}

static int open_map(const char *Path, struct MapBytes *Map) {
  struct stat St;
  ssize_t N;
  size_t Got = 0;
  int Fd = open(Path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (Fd < 0)
    die("cannot open map");
  if (fstat(Fd, &St) != 0 || !S_ISREG(St.st_mode))
    die("map is not a regular file");
  if (St.st_size <= 0 || (uint64_t)St.st_size > WARDEN_MAP_MAX)
    die("map size is not allowed");
  Map->Data = malloc((size_t)St.st_size);
  if (!Map->Data)
    die("out of memory");
  Map->Len = (size_t)St.st_size;
  Map->Fd = Fd;
  while (Got < Map->Len) {
    N = read(Fd, Map->Data + Got, Map->Len - Got);
    if (N < 0) {
      if (errno == EINTR)
        continue;
      die("cannot read map");
    }
    if (N == 0)
      die("short map read");
    Got += (size_t)N;
  }
  return 1;
}

static uint8_t *map_binary(const char *Path, size_t *Len, int *Fd) {
  struct stat St;
  uint8_t *P;
  int F = open(Path, O_RDWR | O_CLOEXEC);
  if (F < 0)
    die("cannot open binary");
  if (fstat(F, &St) != 0 || !S_ISREG(St.st_mode))
    die("binary is not a regular file");
  if (St.st_size <= 0 || (uint64_t)St.st_size > (uint64_t)SIZE_MAX)
    die("binary size is not allowed");
  P = mmap(NULL, (size_t)St.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, F, 0);
  if (P == MAP_FAILED)
    die("cannot map binary");
  *Len = (size_t)St.st_size;
  *Fd = F;
  return P;
}

static void write_slot(uint8_t *Bin, size_t BinLen, size_t HashOff,
                       const uint8_t *Bytes) {
  if (HashOff > BinLen || WARDEN_HASH_LEN > BinLen - HashOff)
    die("slot is outside the binary");
  memcpy(Bin + HashOff, Bytes, WARDEN_HASH_LEN);
  if (msync(Bin, BinLen, MS_SYNC) != 0)
    die("cannot sync binary");
  if (!warden_memeq(Bin + HashOff, Bytes, WARDEN_HASH_LEN))
    die("slot write mismatch");
}

static int ids_match(const struct WardenNoteSet *Set, const uint8_t *Id, size_t IdLen) {
  if (Set->BuildIdLen != IdLen)
    return 0;
  return warden_memeq(Set->BuildId, Id, IdLen);
}

static int hex_eq(const uint8_t Got[32], const char *Expect) {
  uint8_t Dec[32];
  size_t N = 0;
  if (!decode_hex((const uint8_t *)Expect, 64, Dec, &N) || N != 32)
    return 0;
  return warden_memeq(Got, Dec, 32);
}

static int self_test(void) {
  uint8_t Out[32];
  uint8_t Elf[512];
  struct WardenNoteSet Set;
  size_t Phoff = 64;
  const char *Abc =
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const char *Empty =
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

  warden_sha256((const uint8_t *)"", 0, Out);
  if (!hex_eq(Out, Empty))
    die("sha256 empty mismatch");
  warden_sha256((const uint8_t *)"abc", 3, Out);
  if (!hex_eq(Out, Abc))
    die("sha256 abc mismatch");

  memset(Elf, 0, sizeof(Elf));
  Elf[0] = ELFMAG0;
  Elf[1] = 'E';
  Elf[2] = 'L';
  Elf[3] = 'F';
  Elf[4] = ELFCLASS64;
  Elf[5] = ELFDATA2LSB;
  Elf[6] = 1;
  Elf[16] = ET_DYN;
  Elf[32] = (uint8_t)Phoff;
  Elf[54] = 56;
  Elf[56] = 2;
  {
    uint8_t *Load = Elf + Phoff;
    uint8_t *Note = Elf + Phoff + 56;
    uint8_t *Blob = Elf + Phoff + 112;
    size_t BlobLen;
    uint8_t *G = Blob;
    uint8_t *W;
    G[0] = 4;
    G[8] = WARDEN_NT_GNU_BUILD_ID;
    G[4] = 20;
    G[12] = 'G';
    G[13] = 'N';
    G[14] = 'U';
    memset(G + 16, 0xab, 20);
    W = G + 12 + 4 + 20;
    W[0] = WARDEN_NOTE_NAMESZ;
    W[4] = WARDEN_HASH_LEN;
    W[8] = WARDEN_NOTE_TYPE;
    memcpy(W + 12, "WARDEN", 7);
    BlobLen = (size_t)((W + 12 + 8 + 32) - Blob);
    if (Phoff + 112 + BlobLen > sizeof(Elf) || Phoff + 112 > 255u || BlobLen > 255u)
      die("self-test elf does not fit");
    Load[0] = PT_LOAD;
    Load[4] = 4; /* PF_R */
    /* p_offset */
    Load[8] = (uint8_t)(Phoff + 112);
    /* p_filesz */
    Load[32] = (uint8_t)BlobLen;
    Note[0] = PT_NOTE;
    Note[4] = 4;
    Note[8] = (uint8_t)(Phoff + 112);
    Note[32] = (uint8_t)BlobLen;
    if (find_slot(Elf, Phoff + 112 + BlobLen, &Set) != 1)
      die("self-test did not find the slot");
    if (Set.BuildIdLen != 20 || Set.BuildId[0] != 0xab)
      die("self-test build id mismatch");
    if (Set.HashOff != (size_t)(W + 20 - Elf))
      die("self-test slot offset mismatch");
    /* A note that is not inside a load must be refused. */
    Note[32] = (uint8_t)BlobLen;
    Load[32] = 0;
    if (find_slot(Elf, Phoff + 112 + BlobLen, &Set) == 1)
      die("self-test accepted a slot that is not loaded");
  }
  fprintf(stderr, "warden-stamp: self-test ok\n");
  return 0;
}

int main(int Argc, char **Argv) {
  const char *MapPath;
  const char *BinPath;
  struct MapBytes Map;
  uint8_t Hash[WARDEN_HASH_LEN];
  uint8_t Id[WARDEN_BUILD_ID_MAX];
  size_t IdLen = 0;
  size_t BinLen = 0;
  int BinFd = -1;
  uint8_t *Bin;
  struct WardenNoteSet Set;

  if (Argc == 2 && strcmp(Argv[1], "self-test") == 0)
    return self_test();
  if (Argc == 3 && strcmp(Argv[1], "clear") == 0) {
    uint8_t Zeros[WARDEN_HASH_LEN];
    BinPath = Argv[2];
    memset(Zeros, 0, sizeof(Zeros));
    Bin = map_binary(BinPath, &BinLen, &BinFd);
    require_slot(Bin, BinLen, &Set);
    write_slot(Bin, BinLen, Set.HashOff, Zeros);
    munmap(Bin, BinLen);
    close(BinFd);
    say("hash slot cleared");
    return 0;
  }
  if (Argc != 4 || strcmp(Argv[1], "apply") != 0)
    die("usage: warden-stamp apply map binary");
  MapPath = Argv[2];
  BinPath = Argv[3];

  memset(&Map, 0, sizeof(Map));
  open_map(MapPath, &Map);
  if (!map_build_id(Map.Data, Map.Len, Id, &IdLen))
    die("map has no build id");
  warden_sha256(Map.Data, Map.Len, Hash);

  Bin = map_binary(BinPath, &BinLen, &BinFd);
  require_slot(Bin, BinLen, &Set);
  if (!ids_match(&Set, Id, IdLen))
    die("build id mismatch");
  write_slot(Bin, BinLen, Set.HashOff, Hash);
  munmap(Bin, BinLen);
  close(BinFd);
  say("hash written");
  free(Map.Data);
  close(Map.Fd);
  return 0;
}
