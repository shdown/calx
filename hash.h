// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

#ifndef HASH_LARGE
# define HASH_LARGE (__SIZEOF_SIZE_T__ > 4)
#endif

#if HASH_LARGE
typedef uint64_t HashType_Int;
# define HASH__FNV_OFFSET UINT64_C(14695981039346656037)
# define HASH__FNV_PRIME  UINT64_C(1099511628211)

#else
typedef uint32_t HashType_Int;
# define HASH__FNV_OFFSET UINT32_C(2166136261)
# define HASH__FNV_PRIME  UINT32_C(16777619)
#endif

typedef struct {
    HashType_Int v;
} HashType;

#define HASH_TYPE_WRAP(H_) ((HashType) {(H_)})
#define HASH_TYPE_UNWRAP(X_) ((X_).v)
#define HASH_TYPE_EQ(X_, Y_) ((X_).v == (Y_).v)

UU_INHEADER HashType hash_str_concat(HashType old, const char *s, size_t ns)
{
    HashType_Int ret = HASH_TYPE_UNWRAP(old);
    for (size_t i = 0; i < ns; ++i) {
        ret ^= (unsigned char) s[i];
        ret *= HASH__FNV_PRIME;
    }
    return HASH_TYPE_WRAP(ret);
}

UU_INHEADER HashType hash_str(const char *s, size_t ns)
{
    return hash_str_concat(HASH_TYPE_WRAP(HASH__FNV_OFFSET), s, ns);
}
