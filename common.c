// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "common.h"

void *uu_xcalloc(size_t n, size_t m)
{
    void *p = calloc(n, m);
    if (UU_UNLIKELY(!p && n && m))
        UU_PANIC_OOM();
    return p;
}

void *uu_xrealloc(void *p, size_t n, size_t m)
{
    size_t sz;
    if (UU_UNLIKELY(__builtin_mul_overflow(n, m, &sz)))
        UU_PANIC_OOM();
    void *r = realloc(p, sz);
    if (UU_UNLIKELY(!r && sz))
        UU_PANIC_OOM();
    return r;
}

void *uu_x2realloc(void *p, size_t *n, size_t m)
{
    if (*n) {
        if (UU_UNLIKELY(__builtin_mul_overflow(*n, 2u, n)))
            UU_PANIC_OOM();
    } else {
        *n = 1;
    }
    return uu_xrealloc(p, *n, m);
}

void *uu_xmemdup(const void *p, size_t n)
{
    void *r = uu_xmalloc(n, 1);
    if (n)
        memcpy(r, p, n);
    return r;
}

char *uu_xstrdup(const char *s)
{
    return uu_xmemdup(s, strlen(s) + 1);
}

char *uu_xstrvf(const char *fmt, va_list vl)
{
    va_list vl2;
    va_copy(vl2, vl);

    int n = vsnprintf(NULL, 0, fmt, vl);
    if (UU_UNLIKELY(n < 0))
        goto fail;

    size_t nr = ((size_t) n) + 1;
    char *r = uu_xmalloc(nr, 1);
    if (UU_UNLIKELY(vsnprintf(r, nr, fmt, vl2) < 0))
        goto fail;

    va_end(vl2);
    return r;

fail:
    UU_PANIC("vsnprintf() failed");
}

char *uu_xstrf(const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    char *r = uu_xstrvf(fmt, vl);
    va_end(vl);
    return r;
}
