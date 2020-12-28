// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "ntp.h"

static inline deci_UWORD word_from_pow10(size_t p)
{
    deci_UWORD r = 1;
    for (; p; --p)
        r *= 10;
    return r;
}

static inline int word_ctz(deci_UWORD x)
{
    return __builtin_choose_expr(
        sizeof(deci_UWORD) <= sizeof(int),
        __builtin_ctz(x),
        __builtin_choose_expr(
            sizeof(deci_UWORD) <= sizeof(long),
            __builtin_ctzl(x),
            __builtin_ctzll(x)));
}

NumberTruncateParams ntp_from_prec(size_t prec)
{
    size_t q = prec / DECI_BASE_LOG;
    size_t r = prec % DECI_BASE_LOG;

    if (r == 0) {
        return (NumberTruncateParams) {
            .scale = q,
            .submod = 1,
        };
    } else {
        return (NumberTruncateParams) {
            .scale = q + 1,
            .submod = word_from_pow10(DECI_BASE_LOG - r),
        };
    }
}

size_t ntp_to_prec(NumberTruncateParams ntp)
{
    return ntp.scale * DECI_BASE_LOG - word_ctz(ntp.submod);
}
