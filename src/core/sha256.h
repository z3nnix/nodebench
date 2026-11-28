#ifndef NB_CORE_SHA256_H
#define NB_CORE_SHA256_H

#include <stddef.h>
#include <stdint.h>

void sha256_compute(const void* data, size_t size, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);

#endif
