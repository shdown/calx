// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

UU_INHEADER uint32_t hash_str_concat(uint32_t old, const char *s, size_t ns)
{
    uint32_t ret = old;
    for (size_t i = 0; i < ns; ++i) {
        ret ^= (unsigned char) s[i];
        ret *= 16777619;
    }
    return ret;
}

UU_INHEADER uint32_t hash_str(const char *s, size_t ns)
{
    return hash_str_concat(2166136261u, s, ns);
}
