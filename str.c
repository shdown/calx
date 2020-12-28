// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "str.h"
#include "hash.h"

static String *unsafe_reallocate(String *s, size_t new_capacity)
{
    size_t n;
    if (UU_UNLIKELY(__builtin_add_overflow(new_capacity, sizeof(String), &n)))
        goto oom;
    s = realloc(s, n);
    if (UU_UNLIKELY(!s))
        goto oom;
    s->capacity = new_capacity;
    return s;
oom:
    UU_PANIC_OOM();
}

String *string_new_with_capacity(const char *x, size_t nx, size_t capacity)
{
    String *s = unsafe_reallocate(NULL, capacity);
    s->gc_hdr = (GcHeader) {.nrefs = 1, .kind = VK_STR};
    s->size = nx;
    s->hash = hash_str(x, nx);
    if (nx)
        memcpy(s->data, x, nx);
    return s;
}

String *string_hot_append_begin(String *s, size_t n)
{
    // Calculate the minimum required capacity as (s->size + n).
    size_t req_capacity;
    if (UU_UNLIKELY(__builtin_add_overflow(s->size, n, &req_capacity)))
        goto oom;

    if (UU_UNLIKELY(req_capacity > SIZE_MAX / 2))
        goto oom;
    size_t c = s->capacity;
    if (!c)
        c = 1;
    while (c < req_capacity)
        c *= 2;

    if (s->gc_hdr.nrefs == 1) {
        if (c > s->capacity) {
            s = unsafe_reallocate(s, c);
        }
        return s;

    } else {
        String *t = string_new_with_capacity(s->data, s->size, c);
        value_unref((Value) s);
        return t;
    }

oom:
    UU_PANIC_OOM();
}

char *string_hot_append_buf(String *s)
{
    return s->data + s->size;
}

String *string_hot_append_end(String *s, size_t n)
{
    s->hash = hash_str_concat(s->hash, s->data + s->size, n);
    s->size += n;
    return s;
}

int string_compare(String *s, String *t)
{
    size_t ns = s->size;
    size_t nt = t->size;

    size_t nmin = ns < nt ? ns : nt;
    if (nmin) {
        int r = memcmp(s->data, t->data, nmin);
        if (r != 0) {
            return r < 0 ? COMPARE_LESS : COMPARE_GREATER;
        }
    }

    if (ns < nt)
        return COMPARE_LESS;
    if (ns == nt)
        return COMPARE_EQ;
    return COMPARE_GREATER;
}

bool string_equal(String *s, String *t)
{
    size_t ns = s->size;
    size_t nt = t->size;

    if (ns != nt)
        return false;
    if (s->hash != t->hash)
        return false;
    return ns == 0 || memcmp(s->data, t->data, ns) == 0;
}
