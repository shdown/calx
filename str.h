// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "vm.h"

typedef struct {
    GcHeader gc_hdr;
    size_t size;
    size_t capacity;
    uint32_t hash;
    char data[];
} String;

String *string_new_with_capacity(const char *x, size_t nx, size_t capacity);

String *string_hot_append_begin(String *s, size_t n);

char *string_hot_append_buf(String *s);

String *string_hot_append_end(String *s, size_t n);

UU_INHEADER String *string_new(const char *x, size_t nx)
{
    return string_new_with_capacity(x, nx, nx);
}

UU_INHEADER String *string_append(String *s, const char *x, size_t nx)
{
    s = string_hot_append_begin(s, nx);
    if (nx) {
        char *hot_buf = string_hot_append_buf(s);
        memcpy(hot_buf, x, nx);
    }
    s = string_hot_append_end(s, nx);
    return s;
}

int string_compare(String *s, String *t);

bool string_equal(String *s, String *t);

UU_INHEADER void string_destroy(String *s)
{
    free(s);
}
