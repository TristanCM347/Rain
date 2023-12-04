#include <stdio.h>
#include <stdint.h>

// use it to calculate droplet hashes (subsets 1, 2, and 3)

//
// The djb2-xor hash: hash(i) = hash(i - 1) * 33 ^ str[i];
// For more information  http://www.cse.yorku.ca/~oz/hash.html
//
// droplet_hash(), given the current hash value and a byte value, returns the new hash value
// An droplet hash is calculated by calling droplet_hash once for each byte in the droplet except the last byte
// current_hash_value should be zero, for the first byte in the droplet

uint8_t droplet_hash(uint8_t current_hash_value, uint8_t byte_value) {
    return ((current_hash_value * 33) & 0xff) ^ byte_value;
}
