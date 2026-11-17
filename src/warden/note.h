#ifndef WARDEN_NOTE_H_
#define WARDEN_NOTE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ELF note linked into the program. The descriptor is the SHA-256 of the map.
   namesz includes the trailing NUL. The name field is padded to 4 bytes. */

#define WARDEN_NOTE_NAMESZ 7u
#define WARDEN_NOTE_TYPE 1u
#define WARDEN_HASH_LEN 32u
#define WARDEN_BUILD_ID_MAX 64u
#define WARDEN_MAP_MAX (1u << 20)
#define WARDEN_ROW_MAX 65536u
#define WARDEN_NT_GNU_BUILD_ID 3u

struct WardenNoteSet {
  const uint8_t *Hash;
  size_t HashOff;
  const uint8_t *BuildId;
  uint32_t BuildIdLen;
  int HashCount;
  int BuildIdCount;
  int BuildIdTooLong;
};

static inline uint32_t warden_read32(const uint8_t *P) {
  return (uint32_t)P[0] | ((uint32_t)P[1] << 8) | ((uint32_t)P[2] << 16) |
         ((uint32_t)P[3] << 24);
}

/* Walk one note blob. Pointers in Set alias Base. Returns 0 if malformed. */
static inline int warden_notes_collect(const uint8_t *Base, size_t Len,
                                       size_t BaseOff, struct WardenNoteSet *Set) {
  size_t Off = 0;
  while (Off + 12u <= Len) {
    uint32_t Namesz = warden_read32(Base + Off);
    uint32_t Descsz = warden_read32(Base + Off + 4u);
    uint32_t Type = warden_read32(Base + Off + 8u);
    size_t NameOff = Off + 12u;
    size_t NamePad;
    size_t DescOff;
    size_t DescPad;
    if (Namesz > Len - NameOff)
      return 0;
    NamePad = ((size_t)Namesz + 3u) & ~(size_t)3u;
    if (NamePad < (size_t)Namesz || NamePad > Len - NameOff)
      return 0;
    DescOff = NameOff + NamePad;
    if (Descsz > Len - DescOff)
      return 0;
    DescPad = ((size_t)Descsz + 3u) & ~(size_t)3u;
    if (DescPad < (size_t)Descsz || DescPad > Len - DescOff)
      return 0;
    if (Namesz == WARDEN_NOTE_NAMESZ && Type == WARDEN_NOTE_TYPE &&
        memcmp(Base + NameOff, "WARDEN", 6) == 0 && Base[NameOff + 6] == 0) {
      Set->HashCount++;
      if (Descsz == WARDEN_HASH_LEN && Set->HashCount == 1) {
        Set->Hash = Base + DescOff;
        Set->HashOff = BaseOff + DescOff;
      }
    } else if (Namesz == 4u && Type == WARDEN_NT_GNU_BUILD_ID &&
               memcmp(Base + NameOff, "GNU", 4) == 0) {
      Set->BuildIdCount++;
      if (Descsz == 0 || Descsz > WARDEN_BUILD_ID_MAX) {
        Set->BuildIdTooLong = 1;
      } else if (Set->BuildIdCount == 1) {
        Set->BuildId = Base + DescOff;
        Set->BuildIdLen = Descsz;
      }
    }
    Off = DescOff + DescPad;
  }
  return 1;
}

#endif
