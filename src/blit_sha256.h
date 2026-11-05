#ifndef BLIT_SHA256_H
#define BLIT_SHA256_H

/* SHA-256 (FIPS 180-4) of a buffer, as 64 lowercase hex digits. Blit uses it
   for one thing: the revision of the icon catalogue (ADR 0212), which is the
   hash fonts.source.json pins, so a client and the package agree on it. The
   update path hashes a package with sha256sum; a request cannot afford a
   process, and the catalogue is read whole anyway. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint32_t blit_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define BLIT_SHA256_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void blit_sha256_block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64], v[8];
    int t;
    for (t = 0; t < 16; ++t)
        w[t] = (uint32_t)p[4 * t] << 24 | (uint32_t)p[4 * t + 1] << 16 |
               (uint32_t)p[4 * t + 2] << 8 | (uint32_t)p[4 * t + 3];
    for (t = 16; t < 64; ++t) {
        uint32_t s0 = BLIT_SHA256_ROR(w[t - 15], 7) ^ BLIT_SHA256_ROR(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = BLIT_SHA256_ROR(w[t - 2], 17) ^ BLIT_SHA256_ROR(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    memcpy(v, h, sizeof(v));
    for (t = 0; t < 64; ++t) {
        uint32_t s1 = BLIT_SHA256_ROR(v[4], 6) ^ BLIT_SHA256_ROR(v[4], 11) ^ BLIT_SHA256_ROR(v[4], 25);
        uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t t1 = v[7] + s1 + ch + blit_sha256_k[t] + w[t];
        uint32_t s0 = BLIT_SHA256_ROR(v[0], 2) ^ BLIT_SHA256_ROR(v[0], 13) ^ BLIT_SHA256_ROR(v[0], 22);
        uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + s0 + maj;
    }
    for (t = 0; t < 8; ++t) h[t] += v[t];
}

static void blit_sha256_hex(const void *data, size_t length, char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const unsigned char *bytes = (const unsigned char *)data;
    unsigned char tail[128];
    size_t whole = length / 64 * 64, rest = length - whole, padded, i;
    uint64_t bits = (uint64_t)length * 8;
    for (i = 0; i < whole; i += 64) blit_sha256_block(h, bytes + i);
    memset(tail, 0, sizeof(tail));
    memcpy(tail, bytes + whole, rest);
    tail[rest] = 0x80;
    padded = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; ++i) tail[padded - 1 - i] = (unsigned char)(bits >> (8 * i));
    blit_sha256_block(h, tail);
    if (padded == 128) blit_sha256_block(h, tail + 64);
    for (i = 0; i < 32; ++i) {
        unsigned char byte = (unsigned char)(h[i / 4] >> (24 - 8 * (i % 4)));
        hex[2 * i] = digits[byte >> 4];
        hex[2 * i + 1] = digits[byte & 15];
    }
    hex[64] = '\0';
}

#endif
