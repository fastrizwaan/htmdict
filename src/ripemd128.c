#include "ripemd128.h"
#include <string.h>
#include <stdlib.h>

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static uint32_t f(int j, uint32_t x, uint32_t y, uint32_t z) {
    if (j < 16) return x ^ y ^ z;
    if (j < 32) return (x & y) | (z & ~x);
    if (j < 48) return (x | ~y) ^ z;
    return (x & z) | (y & ~z);
}

static uint32_t K(int j) {
    if (j < 16) return 0x00000000;
    if (j < 32) return 0x5a827999;
    if (j < 48) return 0x6ed9eba1;
    return 0x8f1bbcdc;
}

static uint32_t Kp(int j) {
    if (j < 16) return 0x50a28be6;
    if (j < 32) return 0x5c4dd124;
    if (j < 48) return 0x6d703ef3;
    return 0x00000000;
}

static const int r[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
    3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
    1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2
};

static const int rp[] = {
    5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
    6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
    15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
    8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14
};

static const int s[] = {
    11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
    7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
    11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
    11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12
};

static const int sp[] = {
    8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
    9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
    9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
    15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8
};

void ripemd128(const uint8_t *msg, uint32_t msg_len, uint8_t *hash) {
    uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;
    
    uint32_t padded_len = ((msg_len + 8) / 64 + 1) * 64;
    uint8_t *padded = (uint8_t*)calloc(1, padded_len);
    memcpy(padded, msg, msg_len);
    padded[msg_len] = 0x80;
    
    uint64_t bit_len = (uint64_t)msg_len * 8;
    memcpy(padded + padded_len - 8, &bit_len, 8);
    
    for (uint32_t i = 0; i < padded_len; i += 64) {
        uint32_t X[16];
        memcpy(X, padded + i, 64);
        
        uint32_t a = h0, b = h1, c = h2, d = h3;
        uint32_t ap = h0, bp = h1, cp = h2, dp = h3;
        
        for (int j = 0; j < 64; j++) {
            uint32_t t = ROL(a + f(j, b, c, d) + X[r[j]] + K(j), s[j]);
            a = d; d = c; c = b; b = t;
            
            t = ROL(ap + f(63 - j, bp, cp, dp) + X[rp[j]] + Kp(j), sp[j]);
            ap = dp; dp = cp; cp = bp; bp = t;
        }
        
        uint32_t t = h1 + c + dp;
        h1 = h2 + d + ap;
        h2 = h3 + a + bp;
        h3 = h0 + b + cp;
        h0 = t;
    }
    free(padded);
    memcpy(hash, &h0, 4);
    memcpy(hash + 4, &h1, 4);
    memcpy(hash + 8, &h2, 4);
    memcpy(hash + 12, &h3, 4);
}
