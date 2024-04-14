// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define UU_LIKELY(E)   __builtin_expect(E, 1)
#define UU_UNLIKELY(E) __builtin_expect(E, 0)

#define UU_INHEADER static inline __attribute__((unused))
#define UU_ALWAYS_INLINE __attribute__((always_inline))

#define UU_PANIC(S) \
    do { \
        fprintf(stderr, "uu: %s at %s:%d.\n", S, __FILE__, __LINE__); \
        abort(); \
    } while (0)

#define UU_PANIC_OOM() UU_PANIC("Out of memory")

void *uu_xcalloc(size_t n, size_t m);

void *uu_xrealloc(void *p, size_t n, size_t m);

void *uu_x2realloc(void *p, size_t *n, size_t m);

void *uu_xmemdup(const void *p, size_t n);

char *uu_xstrdup(const char *s);

__attribute__((format(printf, 1, 2)))
char *uu_xstrf(const char *fmt, ...);

__attribute__((format(printf, 1, 0)))
char *uu_xstrvf(const char *fmt, va_list vl);

UU_INHEADER void *uu_xmalloc(size_t n, size_t m)
{
    return uu_xrealloc(NULL, n, m);
}

UU_INHEADER size_t uu_add_zu_or_saturate(size_t a, size_t b)
{
    size_t r;
    if (UU_UNLIKELY(__builtin_add_overflow(a, b, &r)))
        return -1;
    return r;
}

UU_INHEADER size_t uu_mul_zu_or_saturate(size_t a, size_t b)
{
    size_t r;
    if (UU_UNLIKELY(__builtin_mul_overflow(a, b, &r)))
        return -1;
    return r;
}
