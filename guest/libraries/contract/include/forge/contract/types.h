#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FORGE_CONTRACT_ALIGNED(type) __attribute__((aligned(16))) type

typedef uint64_t capi_name;
typedef unsigned __int128 uint128_t;
typedef __int128 int128_t;

struct capi_public_key {
   char data[34];
};

struct capi_signature {
   uint8_t data[66];
};

struct FORGE_CONTRACT_ALIGNED(capi_checksum256) {
   uint8_t hash[32];
};

struct FORGE_CONTRACT_ALIGNED(capi_checksum160) {
   uint8_t hash[20];
};

struct FORGE_CONTRACT_ALIGNED(capi_checksum512) {
   uint8_t hash[64];
};

#undef FORGE_CONTRACT_ALIGNED
