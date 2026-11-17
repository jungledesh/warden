#ifndef WARDEN_SHA256_H_
#define WARDEN_SHA256_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One-shot SHA-256. out is 32 bytes. No allocation. */

static inline uint32_t warden_rotr(uint32_t X, uint32_t N) {
  return (X >> N) | (X << (32U - N));
}

static inline uint32_t warden_load_be(const uint8_t *P) {
  return ((uint32_t)P[0] << 24) | ((uint32_t)P[1] << 16) |
         ((uint32_t)P[2] << 8) | (uint32_t)P[3];
}

static inline void warden_store_be(uint8_t *P, uint32_t V) {
  P[0] = (uint8_t)(V >> 24);
  P[1] = (uint8_t)(V >> 16);
  P[2] = (uint8_t)(V >> 8);
  P[3] = (uint8_t)V;
}

static inline void warden_sha256_block(uint32_t S[8], const uint8_t Block[64]) {
  static const uint32_t K[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t W[64];
  uint32_t A, B, C, D, E, F, G, H;
  uint32_t I;
  for (I = 0; I < 16; I++)
    W[I] = warden_load_be(Block + I * 4U);
  for (I = 16; I < 64; I++) {
    uint32_t S0 = warden_rotr(W[I - 15], 7) ^ warden_rotr(W[I - 15], 18) ^
                  (W[I - 15] >> 3);
    uint32_t S1 = warden_rotr(W[I - 2], 17) ^ warden_rotr(W[I - 2], 19) ^
                  (W[I - 2] >> 10);
    W[I] = W[I - 16] + S0 + W[I - 7] + S1;
  }
  A = S[0];
  B = S[1];
  C = S[2];
  D = S[3];
  E = S[4];
  F = S[5];
  G = S[6];
  H = S[7];
  for (I = 0; I < 64; I++) {
    uint32_t S1 = warden_rotr(E, 6) ^ warden_rotr(E, 11) ^ warden_rotr(E, 25);
    uint32_t Ch = (E & F) ^ ((~E) & G);
    uint32_t T1 = H + S1 + Ch + K[I] + W[I];
    uint32_t S0 = warden_rotr(A, 2) ^ warden_rotr(A, 13) ^ warden_rotr(A, 22);
    uint32_t Maj = (A & B) ^ (A & C) ^ (B & C);
    uint32_t T2 = S0 + Maj;
    H = G;
    G = F;
    F = E;
    E = D + T1;
    D = C;
    C = B;
    B = A;
    A = T1 + T2;
  }
  S[0] += A;
  S[1] += B;
  S[2] += C;
  S[3] += D;
  S[4] += E;
  S[5] += F;
  S[6] += G;
  S[7] += H;
}

static inline void warden_sha256(const uint8_t *Data, size_t Len,
                                 uint8_t Out[32]) {
  uint32_t S[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  uint8_t Block[64];
  size_t Off = 0;
  uint64_t Bits = (uint64_t)Len * 8u;
  while (Len - Off >= 64u) {
    warden_sha256_block(S, Data + Off);
    Off += 64u;
  }
  {
    size_t Rem = Len - Off;
    size_t I;
    memset(Block, 0, sizeof(Block));
    if (Rem)
      memcpy(Block, Data + Off, Rem);
    Block[Rem] = 0x80u;
    if (Rem >= 56u) {
      warden_sha256_block(S, Block);
      memset(Block, 0, sizeof(Block));
    }
    for (I = 0; I < 8; I++)
      Block[63u - I] = (uint8_t)(Bits >> (8u * I));
    warden_sha256_block(S, Block);
  }
  {
    uint32_t I;
    for (I = 0; I < 8; I++)
      warden_store_be(Out + I * 4u, S[I]);
  }
}

static inline int warden_memeq(const uint8_t *A, const uint8_t *B, size_t N) {
  size_t I;
  unsigned Diff = 0;
  for (I = 0; I < N; I++)
    Diff |= (unsigned)A[I] ^ (unsigned)B[I];
  return Diff == 0u;
}

#endif
