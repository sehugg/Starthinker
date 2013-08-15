
#ifndef _AI_HASH_H
#define _AI_HASH_H

#include <stdint.h>

typedef uint32_t HashCode;

void init_hashing();

HashCode compute_hash_crc(const void* buf, int len, HashCode seed);

HashCode compute_hash_murmur2(const void* buf, int len, HashCode seed);

//#define compute_hash compute_hash_crc
#define compute_hash compute_hash_murmur2

#endif
