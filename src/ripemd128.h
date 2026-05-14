#ifndef RIPEMD128_H
#define RIPEMD128_H

#include <stdint.h>

void ripemd128(const uint8_t *msg, uint32_t msg_len, uint8_t *hash);

#endif
