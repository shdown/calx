// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "vm.h"
#include "ntp.h"
#include "libdeci/deci.h"

typedef struct {
    GcHeader gc_hdr;
    char sign;
    // Invariants:
    //  1. nwords >= scale.
    //  2. Span (words[scale] ... words[nwords - 1]) is normalized.
    size_t nwords;
    size_t scale;
    deci_UWORD words[];
} Number;

typedef void (*NumberWriter)(void *userdata, const char *buf, size_t nbuf);

Number *number_new_from_zu(size_t x);

bool number_parse_base_validate(const char *s, const char *s_end, uint8_t base);

Number *number_parse(const char *s, const char *s_end);

Number *number_parse_base(const char *s, const char *s_end, uint8_t base, NumberTruncateParams ntp);

size_t number_tostring_size(Number *a);

size_t number_tostring(Number *a, char *buf);

size_t number_tostring_base_size(Number *a, uint8_t base, size_t nfrac);

size_t number_tostring_base(Number *a, uint8_t base, size_t nfrac, char *buf);

void number_write(Number *a, void *userdata, NumberWriter writer);

Number *number_add(Number *a, Number *b);

Number *number_sub(Number *a, Number *b);

Number *number_abs_add_uword(Number *a, deci_UWORD b);

Number *number_mul(Number *a, Number *b);

Number *number_mul_uword(Number *a, deci_UWORD b);

Number *number_pow(Number *b, Number *e);

Number *number_pow_zu(Number *b, size_t e);

Number *number_div(Number *a, Number *b, NumberTruncateParams ntp);

Number *number_idiv(Number *a, Number *b);

Number *number_imod(Number *a, Number *b);

Number *number_negate(Number *a);

int number_compare(Number *a, Number *b);

bool number_abs_eq_uword(Number *a, deci_UWORD b);

Number *number_trunc(Number *a);

Number *number_floor(Number *a);

Number *number_ceil(Number *a);

Number *number_round(Number *a);

Number *number_frac(Number *a);

size_t number_nintdigits(Number *a);

size_t number_nfracdigits(Number *a);

Number *number_bit_and(Number *a, Number *b);

Number *number_bit_or(Number *a, Number *b);

Number *number_bit_xor(Number *a, Number *b);

Number *number_bit_shl(Number *a, Number *b);

Number *number_bit_lshr(Number *a, Number *b);

// Divide by 10^n.
Number *number_scale_down(Number *a, size_t n);

// Multiply by 10^n.
Number *number_scale_up(Number *a, size_t n);

UU_INHEADER void number_destroy(Number *a)
{
    free(a);
}

UU_INHEADER bool number_is_zero(Number *a)
{
    size_t nwa = a->nwords;
    size_t sa = a->scale;
    return sa == nwa && deci_is_zero_n(a->words, sa);
}

UU_INHEADER bool number_is_izero(Number *a)
{
    return a->nwords == a->scale;
}

UU_INHEADER bool number_is_fzero(Number *a)
{
    return deci_is_zero_n(a->words, a->scale);
}

UU_INHEADER size_t number_to_zu(Number *a)
{
    if (a->sign) {
        if (number_is_zero(a))
            return 0;
        goto overflow;
    }

    deci_UWORD *wa     = a->words + a->scale;
    deci_UWORD *wa_end = a->words + a->nwords;
    size_t r = 0;
    while (wa_end != wa) {
        --wa_end;
        if (__builtin_mul_overflow(r, DECI_BASE, &r))
            goto overflow;
        if (__builtin_add_overflow(r, *wa_end, &r))
            goto overflow;
    }
    return r;
overflow:
    return -1;
}

UU_INHEADER uint32_t number_to_u32(Number *a)
{
    deci_UWORD *wa     = a->words + a->scale;
    deci_UWORD *wa_end = a->words + a->nwords;
    uint32_t r = 0;
    while (wa_end != wa) {
        --wa_end;
        r *= DECI_BASE;
        r += *wa_end;
    }

    if (a->sign)
        r = -r;
    return r;
}
