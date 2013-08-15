
#include "hash.h"

#include <stddef.h>

static HashCode crc_table[256];

void init_hashing()
{
    for (int i = 0; i < 256; i++) {
        HashCode c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
}

// TODO: better hash?
// TODO: add current player? other state?
HashCode compute_hash_crc(const void* buffer, int len, HashCode seed)
{
    HashCode c = ~seed;
    const char* buf = (const char*)buffer;
    for (size_t i = 0; i < len; i++) {
        c = crc_table[((c ^ buf[i])+seed) & 0xFF] ^ (c >> 8);
    }
    //JDEBUG("%x %d (%x %x %x %x) %x -> %x\n", seed, len, buf[0], buf[1], buf[2], buf[3], current_hash, c);
    return c;
}

//-----------------------------------------------------------------------------
// MurmurHash2, by Austin Appleby

// Note - This code makes a few assumptions about how your machine behaves -

// 1. We can read a 4-byte value from any address without crashing
// 2. sizeof(int) == 4

// And it has a few limitations -

// 1. It will not work incrementally.
// 2. It will not produce the same results on little-endian and big-endian
//    machines.

// TODO: handle alignment issues

HashCode compute_hash_murmur2( const void * key, int len, HashCode seed )
{
        // 'm' and 'r' are mixing constants generated offline.
        // They're not really 'magic', they just happen to work well.

        const unsigned int m = 0x5bd1e995;
        const int r = 24;

        // Initialize the hash to a 'random' value

        unsigned int h = seed ^ len ^ 0x9747b28c;

        // Mix 4 bytes at a time into the hash

        const unsigned char * data = (const unsigned char *)key;

        while(len >= 4)
        {
                unsigned int k = *(unsigned int *)data;

                k *= m; 
                k ^= k >> r; 
                k *= m; 
                
                h *= m; 
                h ^= k;

                data += 4;
                len -= 4;
        }
        
        // Handle the last few bytes of the input array

        switch(len)
        {
        case 3: h ^= data[2] << 16;
        case 2: h ^= data[1] << 8;
        case 1: h ^= data[0];
                h *= m;
        };

        // Do a few final mixes of the hash to ensure the last few
        // bytes are well-incorporated.

        h ^= h >> 13;
        h *= m;
        h ^= h >> 15;

        return h;
} 
