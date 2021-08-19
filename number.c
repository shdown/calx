// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "number.h"

#include "fancy.h"
#include "compare.h"

#define SWAP(X_, Y_) \
    do { \
        __typeof__(X_) swap_tmp_ = (X_); \
        (X_) = (Y_); \
        (Y_) = swap_tmp_; \
    } while (0)

static Number *unsafe_reallocate(Number *a, size_t nwords)
{
    size_t n;
    if (UU_UNLIKELY(__builtin_mul_overflow(nwords, sizeof(deci_UWORD), &n)))
        goto oom;
    if (UU_UNLIKELY(__builtin_add_overflow(n, sizeof(Number), &n)))
        goto oom;
    a = realloc(a, n);
    if (UU_UNLIKELY(!a))
        goto oom;
    a->nwords = nwords;
    return a;
oom:
    UU_PANIC_OOM();
}

static Number *allocate(char sign, size_t nwords, size_t scale)
{
    Number *a = unsafe_reallocate(NULL, nwords);
    a->gc_hdr = (GcHeader) {.nrefs = 1, .kind = VK_NUM};
    a->sign = sign;
    a->scale = scale;
    return a;
}

static __attribute__((noinline))
Number *unsafe_push_word(Number *a, deci_UWORD w)
{
    size_t nwa = a->nwords;
    a = unsafe_reallocate(a, nwa + 1);
    a->words[nwa] = w;
    return a;
}

Number *number_new_from_zu(size_t x)
{
    enum { NBUF = 4 };

    deci_UWORD buf[NBUF];
    size_t nwords = 0;

    Number *a;

    size_t y = x;
    while (y) {
        if (UU_UNLIKELY(nwords == NBUF))
            goto slow_way;

        if (y < DECI_BASE) {
            buf[nwords++] = y;
            break;
        } else {
            buf[nwords++] = y % DECI_BASE;
            y /= DECI_BASE;
        }
    }

    a = allocate(/*sign=*/0, /*nwords=*/nwords, /*scale=*/0);
    deci_memcpy(/*dst=*/a->words, /*src=*/buf, /*n=*/nwords);
    return a;

slow_way:
    a = allocate(/*sign=*/0, /*nwords=*/0, /*scale=*/0);
    while (x) {
        a = unsafe_push_word(a, x % DECI_BASE);
        x /= DECI_BASE;
    }
    return a;
}

static inline char *span_memchr(const char *s, const char *s_end, char c)
{
    size_t ns = s_end - s;
    return ns ? memchr(s, c, ns) : NULL;
}

static char *copy_remove_sq(const char *s, const char *s_end, char *r)
{
    char *r_end = r;

    for (const char *next_quote;
         (next_quote = span_memchr(s, s_end, '\''));
         s = next_quote + 1)
    {
        size_t nsegment = next_quote - s;
        memcpy(r_end, s, nsegment);
        r_end += nsegment;
    }

    if (s != s_end) {
        size_t nsegment = s_end - s;
        memcpy(r_end, s, nsegment);
        r_end += nsegment;
    }

    return r_end;
}

static inline uint8_t decode(char c)
{
    switch (c) {
    case '0' ... '9': return c - '0';
    case 'a' ... 'z': return c - 'a' + 10;
    case 'A' ... 'Z': return c - 'A' + 10;
    default: return -1;
    }
}

bool number_parse_base_validate(const char *s, const char *s_end, uint8_t base)
{
    if (s != s_end && *s == '-')
        ++s;

    bool seen_digit = false;
    bool seen_dot = false;

    for (; s != s_end; ++s) {
        char c = *s;
        switch (c) {
        case '.':
            if (seen_dot)
                goto reject;
            seen_dot = true;
            break;
        case '\'':
            break;
        default:
            if (decode(c) >= base)
                goto reject;
            seen_digit = true;
            break;
        }
    }
    if (!seen_digit)
        goto reject;
    return true;

reject:
    return false;
}

static inline deci_UWORD parse_word(const char *s, const char *s_end)
{
    deci_UWORD r = 0;
    for (; s != s_end; ++s) {
        r *= 10;
        r += *s - '0';
    }
    return r;
}

static inline deci_UWORD parse_word_pad(const char *s, const char *s_end)
{
    size_t npad = DECI_BASE_LOG - (s_end - s);

    deci_UWORD r = 0;
    for (; s != s_end; ++s) {
        r *= 10;
        r += *s - '0';
    }
    for (; npad; --npad)
        r *= 10;
    return r;
}

static inline size_t parse_nchars_to_nwords(size_t n)
{
    return n / DECI_BASE_LOG + !!(n % DECI_BASE_LOG);
}

static void parse_int_part(
        const char *s, const char *s_end,
        deci_UWORD *out, deci_UWORD *out_end)
{
    if (out == out_end)
        return;

    --out_end;
    for (; out != out_end; ++out) {
        const char *p = s_end - DECI_BASE_LOG;
        *out = parse_word(p, s_end);
        s_end = p;
    }
    *out = parse_word(s, s_end);
}

static void parse_frac_part(
        const char *s, const char *s_end,
        deci_UWORD *out, deci_UWORD *out_end)
{
    if (out == out_end)
        return;
    --out_end;
    for (; out_end != out; --out_end) {
        const char *p = s + DECI_BASE_LOG;
        *out_end = parse_word(s, p);
        s = p;
    }
    *out_end = parse_word_pad(s, s_end);
}

static Number *parse_no_sq(const char *s, const char *s_end)
{
    char sign = 0;
    if (*s == '-') {
        sign = 1;
        ++s;
    }

    while (s != s_end && *s == '0')
        ++s;

    const char *period = span_memchr(s, s_end, '.');

    if (period) {

        while (s_end[-1] == '0')
            --s_end;

        size_t int_nchars = period - s;
        size_t frac_nchars = s_end - (period + 1);
        size_t int_nwords = parse_nchars_to_nwords(int_nchars);
        size_t frac_nwords = parse_nchars_to_nwords(frac_nchars);
        size_t nwords = int_nwords + frac_nwords;

        Number *a = allocate(/*sign=*/sign, /*nwords=*/nwords, /*scale=*/frac_nwords);
        parse_frac_part(
            /*s=*/period + 1, /*s_end=*/s_end,
            /*out=*/a->words, /*out_end=*/a->words + frac_nwords);
        parse_int_part(
            /*s=*/s, /*s_end=*/period,
            /*out=*/a->words + frac_nwords, /*out_end=*/a->words + nwords);
        return a;

    } else {
        size_t nwords = parse_nchars_to_nwords(s_end - s);
        Number *a = allocate(/*sign=*/sign, /*nwords=*/nwords, /*scale=*/0);
        parse_int_part(
            /*s=*/s, /*s_end=*/s_end,
            /*out=*/a->words, /*out_end=*/a->words + nwords);
        return a;
    }
}

Number *number_parse(const char *s, const char *s_end)
{
    if ((span_memchr(s, s_end, '\''))) {
        char *copy = uu_xmalloc(s_end - s, 1);
        char *copy_end = copy_remove_sq(s, s_end, copy);
        Number *a = parse_no_sq(copy, copy_end);
        free(copy);
        return a;
    }
    return parse_no_sq(s, s_end);
}

Number *number_parse_base(const char *s, const char *s_end, uint8_t base, NumberTruncateParams ntp)
{
    Number *a = number_new_from_zu(0);

    bool negate = false;
    if (*s == '-') {
        negate = true;
        ++s;
    }

    // 'base_scale' is zero if no dot seen, otherwise the number of digits after the dot.
    size_t base_scale = 0;
    bool seen_dot = false;
    for (; s != s_end; ++s) {
        char c = *s;
        switch (c) {
        case '.':
            seen_dot = true;
            break;
        case '\'':
            break;
        default:
            if (seen_dot)
                ++base_scale;
            a = number_mul_uword(a, base);
            a = number_abs_add_uword(a, decode(c));
            break;
        }
    }

    if (base_scale) {
        Number *divisor = number_pow_zu(
            number_new_from_zu(base),
            base_scale);
        a = number_div(a, divisor, ntp);
    }

    if (negate)
        a = number_negate(a);
    return a;
}

static inline void span_reverse(char *s, char *s_end)
{
    for (size_t n = ((size_t) (s_end - s)) / 2; n; --n) {
        --s_end;
        SWAP(*s, *s_end);
        ++s;
    }
}

static inline size_t finalize_tostring(char *s, char *s_end)
{
    size_t n = s_end - s;
    if (n == 2 && memcmp(s, "-0", 2) == 0) {
        s[0] = '0';
        n = 1;
    }
    return n;
}

size_t number_tostring_size(Number *a)
{
    return uu_add_zu_or_saturate(
        3, // three extra bytes for '.', '0', '-' that we may insert
        uu_mul_zu_or_saturate(a->nwords, DECI_BASE_LOG));
}

size_t number_tostring(Number *a, char *r)
{
    size_t nwa = a->nwords;
    size_t sa = a->scale;

    // Points to the *next* char to be written to the result buffer.
    char *p = r;

    // Write all the significant digits.
    size_t i = 0;
    for (; i < nwa; ++i) {
        if (i == sa)
            *p++ = '.';

        deci_UWORD x = a->words[i];
        for (int j = 0; j < DECI_BASE_LOG; ++j) {
            *p++ = '0' + x % 10;
            x /= 10;
        }
    }

    if (i == sa) {
        // No digits in the integer part.
        *p++ = '.';
        *p++ = '0';
    } else {
        // Remove *leading* zeros (in the integer part).
        // We have already produced at least a '.', so it's safe to dereference 'p[-1]'.
        while (p[-1] == '0')
            --p;
    }

    if (a->sign)
        *p++ = '-';

    span_reverse(r, p);

    // Remove *trailing* zeros (in the fractional part).
    // We have already produced at least a '.', so it's safe to dereference 'p[-1]'.
    while (p[-1] == '0')
        --p;

    if (p[-1] == '.')
        --p;

    return finalize_tostring(r, p);
}

static inline void word2str(deci_UWORD x, char *r)
{
    for (int i = DECI_BASE_LOG - 1; i >= 0; --i) {
        r[i] = '0' + x % 10;
        x /= 10;
    }
}

static void write_leading(deci_UWORD x, void *userdata, NumberWriter writer)
{
    char buf[DECI_BASE_LOG];
    word2str(x, buf);

    int i = 0;
    while (i < DECI_BASE_LOG && buf[i] == '0')
        ++i;
    writer(userdata, buf + i, sizeof(buf) - i);
}

static void write_trailing(deci_UWORD x, void *userdata, NumberWriter writer)
{
    char buf[DECI_BASE_LOG];
    word2str(x, buf);

    int n = sizeof(buf);
    while (n > 0 && buf[n - 1] == '0')
        --n;
    writer(userdata, buf, n);
}

static void write_seq(deci_UWORD *wa, deci_UWORD *wa_end, void *userdata, NumberWriter writer)
{
    char buf[DECI_BASE_LOG];

    while (wa_end != wa) {
        --wa_end;
        word2str(*wa_end, buf);
        writer(userdata, buf, sizeof(buf));
    }
}

void number_write(Number *a, void *userdata, NumberWriter writer)
{
    deci_UWORD *wa     = a->words;
    deci_UWORD *wa_int = wa + a->scale;
    deci_UWORD *wa_end = wa + a->nwords;

    if (a->sign && !number_is_zero(a))
        writer(userdata, "-", 1);

    if (wa_end == wa_int) {
        writer(userdata, "0", 1);
    } else {
        --wa_end;
        write_leading(*wa_end, userdata, writer);
        write_seq(wa_int, wa_end, userdata, writer);
    }

    wa = deci_skip0(wa, wa_int);

    if (wa != wa_int) {
        writer(userdata, ".", 1);
        write_seq(wa + 1, wa_int, userdata, writer);
        write_trailing(*wa, userdata, writer);
    }
}

size_t number_tostring_base_size(Number *a, uint8_t base, size_t nfrac)
{
    // 'dpw' is max digits, in base 'base', per word.
    size_t dpw = 0;
    for (deci_DOUBLE_UWORD x = 1; x < DECI_BASE; x *= base)
        ++dpw;

    return uu_add_zu_or_saturate(
        3, // three extra bytes for '.', '0', '-', that we may insert
        uu_add_zu_or_saturate(
            nfrac,
            uu_mul_zu_or_saturate(a->nwords, dpw)));
}

size_t number_tostring_base(Number *a, uint8_t base, size_t nfrac, char *r)
{
    static const char *CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    deci_UWORD *wx = uu_xmemdup(a->words, a->nwords * sizeof(deci_UWORD));
    deci_UWORD *wx_int = wx + a->scale;
    deci_UWORD *wx_end = wx + a->nwords;

    // Points to the *next* char to be written to the result buffer.
    char *p = r;

    // Write the digits of the integer part.
    while (wx_end != wx_int) {
        deci_UWORD digit = deci_divmod_uword(wx_int, wx_end, base);
        *p++ = CHARS[digit];
        wx_end = deci_normalize(wx_int, wx_end);
    }

    if (p == r) {
        // No digits in the integer part.
        *p++ = '0';
    }

    if (a->sign)
        *p++ = '-';

    span_reverse(r, p);

    *p++ = '.';

    // Write the digits of the fractional part.
    for (size_t i = 0; i < nfrac; ++i) {
        deci_UWORD digit = deci_mul_uword(wx, wx_int, base);
        *p++ = CHARS[digit];
    }

    // Remove *trailing* zeros (in the fractional part).
    // We have already produced at least a '.', so it's safe to dereference 'p[-1]'.
    while (p[-1] == '0')
        --p;

    if (p[-1] == '.')
        --p;

    free(wx);

    return finalize_tostring(r, p);
}

static inline Number *unsafe_strip(Number *a, size_t nwr, size_t sr)
{
    size_t nwa = a->nwords;
    size_t sa = a->scale;

    if (nwa == nwr && sa == sr)
        return a;

    deci_UWORD *wa = a->words;

    size_t ds = sa - sr;
    if (ds) {
        deci_memmove(/*dst=*/wa, /*src=*/wa + ds, /*n=*/nwr);
        a->scale = sr;
    }

    return unsafe_reallocate(a, nwr);
}

static inline Number *unsafe_normalize_full(Number *a)
{
    size_t nwa = a->nwords;
    size_t sa = a->scale;

    size_t ds = deci_skip0_n(a->words, sa);
    size_t new_nintpart = deci_normalize_n(a->words + sa, nwa - sa);
    size_t new_scale = sa - ds;
    return unsafe_strip(a, new_scale + new_nintpart, new_scale);
}

static Number *unsafe_normalize_after_div(Number *a, size_t nwr, NumberTruncateParams ntp)
{
    size_t sa = a->scale;

    size_t new_nwa;
    if (nwr > sa) {
        new_nwa = deci_normalize(a->words + sa, a->words + nwr) - a->words;
    } else {
        deci_zero_out(a->words + nwr, a->words + sa);
        new_nwa = sa;
    }

    if (sa >= ntp.scale) {
        a = unsafe_strip(a, new_nwa - sa + ntp.scale, ntp.scale);
        if (ntp.scale)
            a->words[0] -= a->words[0] % ntp.submod;
    } else {
        a = unsafe_strip(a, new_nwa, sa);
    }
    return a;
}

static Number *unsafe_normalize_after_idiv(Number *a, size_t nwr)
{
    size_t new_nwa = deci_normalize_n(a->words, nwr);
    return unsafe_strip(a, new_nwa, 0);
}

static inline Number *mkuniq(Number *a)
{
    if (a->gc_hdr.nrefs == 1)
        return a;

    Number *r = allocate(/*sign=*/a->sign, /*nwords=*/a->nwords, /*scale=*/a->scale);
    deci_memcpy(/*dst=*/r->words, /*src=*/a->words, /*n=*/a->nwords);

    value_unref((Value) a);
    return r;
}

static Number *mkuniq_scale_down(Number *a, size_t sr)
{
    size_t sa = a->scale;
    if (a->gc_hdr.nrefs == 1 && sa == sr)
        return a;

    size_t nwa = a->nwords;
    size_t nintpart = nwa - sa;
    size_t nwr = nintpart + sr;

    Number *r = allocate(/*sign=*/a->sign, /*nwords=*/nwr, /*scale=*/sr);
    deci_memcpy(/*dst=*/r->words, /*src=*/a->words + nwa - nwr, /*n=*/nwr);

    value_unref((Value) a);
    return r;
}

static Number *mkuniq_scale_up(Number *a, size_t sr)
{
    size_t sa = a->scale;
    if (a->gc_hdr.nrefs == 1 && sa == sr)
        return a;

    size_t nwa = a->nwords;
    size_t nintpart = nwa - sa;
    size_t nwr = uu_add_zu_or_saturate(nintpart, sr);

    Number *r = allocate(/*sign=*/a->sign, /*nwords=*/nwr, /*scale=*/sr);
    size_t d = sr - sa;
    deci_zero_out_n(/*wa=*/r->words, /*n=*/d);
    deci_memcpy(/*dst=*/r->words + d, /*src=*/a->words, /*n=*/nwa);

    value_unref((Value) a);
    return r;
}

static Number *mkuniq_extend(Number *a, size_t new_nwords)
{
    size_t nwa = a->nwords;

    if (a->gc_hdr.nrefs == 1) {
        if (nwa != new_nwords) {
            a = unsafe_reallocate(a, new_nwords);
            deci_zero_out_n(/*wa=*/a->words + nwa, /*n=*/new_nwords - nwa);
        }
        return a;
    }

    Number *r = allocate(/*sign=*/a->sign, /*nwords=*/new_nwords, /*scale=*/a->scale);
    deci_memcpy(/*dst=*/r->words, /*src=*/a->words, /*n=*/nwa);
    deci_zero_out_n(/*wa=*/r->words + nwa, /*n=*/new_nwords - nwa);

    value_unref((Value) a);
    return r;
}

// Steals (takes move references to):
//   * 'a';
//   * 'b'.
static Number *do_add_or_sub(Number *a, Number *b, bool add)
{
    size_t nwa = a->nwords;
    size_t nwb = b->nwords;
    bool negate_result = false;

    if (nwa < nwb || (nwa == nwb && b->gc_hdr.nrefs == 1)) {
        SWAP(a, b);
        SWAP(nwa, nwb);
        negate_result = !add;
    }

    size_t sa = a->scale;
    size_t sb = b->scale;

    size_t ninta = nwa - sa;
    size_t nintb = nwb - sb;

    if (ninta < nintb) {
        nwa = sa + nintb;
        ninta = nintb;
        a = mkuniq_extend(a, nwa);

    } else {
        if (sa < sb) {
            nwa = sb + ninta;
            sa = sb;
            a = mkuniq_scale_up(a, sb);

        } else {
            a = mkuniq(a);
        }
    }

    deci_UWORD *wa     = a->words;
    deci_UWORD *wa_end = wa + nwa;

    deci_UWORD *wb     = b->words;
    deci_UWORD *wb_end = wb + nwb;

    if (a->sign ^ b->sign ^ add) {
        if (deci_add(
                wa + sa - sb,   wa_end,
                wb,             wb_end))
        {
            a = unsafe_push_word(a, 1);
        }
    } else {
        if (deci_sub_raw(
                wa + sa - sb,   wa_end,
                wb,             wb_end))
        {
            deci_uncomplement(wa, wa_end);
            negate_result ^= true;
        }
        if (nwa != sa && wa_end[-1] == 0) {
            nwa = deci_normalize(wa + sa, wa_end) - wa;
            a = unsafe_reallocate(a, nwa);
        }
    }

    a->sign ^= negate_result;

    value_unref((Value) b);
    return a;
}

Number *number_add(Number *a, Number *b)
{
    return do_add_or_sub(a, b, true);
}

Number *number_sub(Number *a, Number *b)
{
    return do_add_or_sub(a, b, false);
}

Number *number_abs_add_uword(Number *a, deci_UWORD b)
{
    if (!b)
        return a;

    a = mkuniq(a);

    size_t sa = a->scale;
    size_t nwa = a->nwords;

    if (sa == nwa) {
        a = unsafe_push_word(a, b);
        return a;
    }

    deci_UWORD *wb = &b;
    bool push1 = deci_add(
        /*wa=*/a->words + sa, /*wa_end=*/a->words + nwa,
        /*wb=*/wb,            /*wb_end=*/wb + 1);
    if (push1)
        a = unsafe_push_word(a, 1);
    return a;
}

Number *number_mul(Number *a, Number *b)
{
    size_t nwa = a->nwords;
    size_t nwb = b->nwords;

    size_t nwr = uu_add_zu_or_saturate(nwa, nwb);
    size_t sr = a->scale + b->scale;

    Number *r = allocate(/*sign=*/a->sign ^ b->sign, /*nwords=*/nwr, /*scale=*/sr);
    fancy_mul(a->words, nwa, b->words, nwb, r->words);

    r = unsafe_normalize_full(r);

    value_unref((Value) a);
    value_unref((Value) b);

    return r;
}

Number *number_mul_uword(Number *a, deci_UWORD b)
{
    if (!b) {
        value_unref((Value) a);
        return number_new_from_zu(0);
    }

    a = mkuniq(a);

    deci_UWORD hi = deci_mul_uword(a->words, a->words + a->nwords, b);
    if (hi)
        a = unsafe_push_word(a, hi);
    return a;
}

Number *number_pow_zu(Number *b, size_t e)
{
    if (!e) {
        value_unref((Value) b);
        return number_new_from_zu(1);
    }

    size_t h = 1;
    for (size_t tmp = e >> 1; tmp; tmp >>= 1)
        h <<= 1;

    b = mkuniq(b);
    b = unsafe_normalize_full(b);

    value_ref((Value) b);
    Number *s = b;

    while (h > 1) {
        h >>= 1;

        value_ref((Value) s);
        s = number_mul(s, s);

        if (e & h) {
            value_ref((Value) b);
            s = number_mul(s, b);
        }
    }

    value_unref((Value) b);
    return s;
}

bool number_abs_eq_uword(Number *a, deci_UWORD w)
{
    if (!w)
        return number_is_zero(a);

    if (!number_is_fzero(a))
        return false;

    size_t nwa = a->nwords;
    size_t sa = a->scale;

    return nwa - sa == 1 && a->words[sa] == w;
}

Number *number_pow(Number *b, Number *e)
{
    size_t x = number_to_zu(e);
    if (x == (size_t) -1) {

        if (number_is_zero(b)) {
            value_unref((Value) e);
            return b;
        }

        if (number_abs_eq_uword(b, 1)) {
            if (b->sign && (number_to_u32(e) & 1)) {
                value_unref((Value) e);
                return b;
            } else {
                value_unref((Value) e);
                value_unref((Value) b);
                return number_new_from_zu(1);
            }
        }

        UU_PANIC("exponent is too large");
    }

    value_unref((Value) e);
    return number_pow_zu(b, x);
}

// Steals (takes move references to):
//   * 'a'.
static Number *div_prepare(Number *a, size_t mul_base_pow, size_t min_scale)
{
    size_t sa = a->scale;
    size_t sr = min_scale > sa ? min_scale : sa;
    a = mkuniq_scale_up(a, uu_add_zu_or_saturate(sr, mul_base_pow));
    a->scale = sr;
    return a;
}

Number *number_div(Number *a, Number *b, NumberTruncateParams ntp)
{
    a = div_prepare(a, /*mul_base_pow=*/b->scale, /*min_scale=*/ntp.scale);
    a->sign ^= b->sign;

    size_t nwr = fancy_div(
        /*wa=*/a->words, /*nwa=*/a->nwords,
        /*wb=*/b->words, /*nwb=*/b->nwords);

    value_unref((Value) b);
    return unsafe_normalize_after_div(a, nwr, ntp);
}

Number *number_imod(Number *a, Number *b)
{
    a = mkuniq_scale_down(a, 0);

    size_t nwr = fancy_mod(
        a->words,            a->nwords,
        b->words + b->scale, b->nwords - b->scale);

    value_unref((Value) b);
    return unsafe_normalize_after_idiv(a, nwr);
}

Number *number_idiv(Number *a, Number *b)
{
    a = mkuniq_scale_down(a, 0);

    a->sign ^= b->sign;

    size_t nwr = fancy_div(
        a->words,            a->nwords,
        b->words + b->scale, b->nwords - b->scale);

    value_unref((Value) b);
    return unsafe_normalize_after_idiv(a, nwr);
}

Number *number_negate(Number *a)
{
    a = mkuniq(a);
    a->sign ^= 1;
    return a;
}

Number *number_trunc(Number *a)
{
    return mkuniq_scale_down(a, 0);
}

Number *number_floor(Number *a)
{
    if (a->sign && !number_is_fzero(a))
        a = number_abs_add_uword(a, 1);
    return mkuniq_scale_down(a, 0);
}

Number *number_ceil(Number *a)
{
    if (!a->sign && !number_is_fzero(a))
        a = number_abs_add_uword(a, 1);
    return mkuniq_scale_down(a, 0);
}

Number *number_round(Number *a)
{
    if (a->scale && a->words[a->scale - 1] >= DECI_BASE / 2)
        a = number_abs_add_uword(a, 1);
    return mkuniq_scale_down(a, 0);
}

Number *number_frac(Number *a)
{
    a = mkuniq(a);
    return unsafe_strip(a, a->scale, a->scale);
}

// Borrows (takes regular references to):
//   * 'a';
//   * 'b'.
static int compare_abs(Number *a, Number *b)
{
    size_t nwa = a->nwords;
    size_t nwb = b->nwords;

    size_t sa = a->scale;
    size_t sb = b->scale;

    size_t ninta = nwa - sa;
    size_t nintb = nwb - sb;

    if (ninta != nintb)
        return ninta < nintb ? COMPARE_LESS : COMPARE_GREATER;

    int xor_result_with = 0;
    deci_UWORD *w1 = a->words;
    deci_UWORD *w1_end = w1 + nwa;
    deci_UWORD *w2 = b->words;
    deci_UWORD *w2_end = w2 + nwb;

    if (sa < sb) {
        xor_result_with = COMPARE_LESS | COMPARE_GREATER;
        SWAP(w1, w2);
        SWAP(w1_end, w2_end);
    }

    while (w2_end != w2) {
        deci_UWORD x = *--w1_end;
        deci_UWORD y = *--w2_end;
        if (x < y)
            goto less;
        if (x > y)
            goto greater;
    }
    while (w1_end != w1)
        if (*--w1_end)
            goto greater;
    return COMPARE_EQ;
less:
    return xor_result_with ^ COMPARE_LESS;
greater:
    return xor_result_with ^ COMPARE_GREATER;
}

int number_compare(Number *a, Number *b)
{
    char ca = a->sign;
    char cb = b->sign;
    if (ca == cb) {
        if (ca)
            SWAP(a, b);
        return compare_abs(a, b);
    } else {
        if (number_is_zero(a) && number_is_zero(b))
            return COMPARE_EQ;
        return ca ? COMPARE_LESS : COMPARE_GREATER;
    }
}

size_t number_nintdigits(Number *a)
{
    size_t n = a->nwords - a->scale;
    if (!n)
        return 0;
    --n;

    uint8_t summand = 0;
    deci_UWORD hi = a->words[a->nwords - 1];
    for (; hi; hi /= 10)
        ++summand;

    return uu_add_zu_or_saturate(
        summand,
        uu_mul_zu_or_saturate(n, DECI_BASE_LOG));
}

size_t number_nfracdigits(Number *a)
{
    deci_UWORD *wa     = a->words;
    deci_UWORD *wa_end = a->words + a->scale;

    wa = deci_skip0(wa, wa_end);

    if (wa == wa_end)
        return 0;

    uint8_t summand = DECI_BASE_LOG;
    for (deci_UWORD lo = *wa; lo % 10 == 0; lo /= 10)
        --summand;

    ++wa;
    size_t n = wa_end - wa;

    return uu_add_zu_or_saturate(
        summand,
        uu_mul_zu_or_saturate(n, DECI_BASE_LOG));
}

Number *number_bit_and(Number *a, Number *b)
{
    uint32_t x = number_to_u32(a);
    uint32_t y = number_to_u32(b);

    value_unref((Value) a);
    value_unref((Value) b);

    uint32_t z = x & y;

    return number_new_from_zu(z);
}

Number *number_bit_or(Number *a, Number *b)
{
    uint32_t x = number_to_u32(a);
    uint32_t y = number_to_u32(b);

    value_unref((Value) a);
    value_unref((Value) b);

    uint32_t z = x | y;

    return number_new_from_zu(z);
}

Number *number_bit_xor(Number *a, Number *b)
{
    uint32_t x = number_to_u32(a);
    uint32_t y = number_to_u32(b);

    value_unref((Value) a);
    value_unref((Value) b);

    uint32_t z = x ^ y;

    return number_new_from_zu(z);
}

Number *number_bit_shl(Number *a, Number *b)
{
    uint32_t x = number_to_u32(a);
    uint32_t y = number_to_u32(b);

    value_unref((Value) a);
    value_unref((Value) b);

    uint32_t z = (y < 32) ? (x << y) : 0;

    return number_new_from_zu(z);
}

Number *number_bit_lshr(Number *a, Number *b)
{
    uint32_t x = number_to_u32(a);
    uint32_t y = number_to_u32(b);

    value_unref((Value) a);
    value_unref((Value) b);

    uint32_t z = (y < 32) ? (x >> y) : 0;

    return number_new_from_zu(z);
}

Number *number_scale_down(Number *a, size_t n)
{
    size_t q = n / DECI_BASE_LOG;
    size_t r = n % DECI_BASE_LOG;

    size_t s = uu_add_zu_or_saturate(a->scale, q);
    a = mkuniq_extend(a, a->nwords > s ? a->nwords : s);
    a->scale += q;

    if (r) {
        deci_UWORD f = 1;
        for (size_t i = r; i; --i) {
            f *= 10;
        }
        deci_UWORD m = deci_divmod_uword(a->words, a->words + a->nwords, f);
        if (m) {
            a = mkuniq_scale_up(a, a->scale + 1);
            for (size_t i = DECI_BASE_LOG - r; i; --i) {
                m *= 10;
            }
            a->words[0] = m;
        }
    }

    a = unsafe_normalize_full(a);

    return a;
}

Number *number_scale_up(Number *a, size_t n)
{
    size_t q = n / DECI_BASE_LOG;
    size_t r = n % DECI_BASE_LOG;

    a = mkuniq_scale_up(a, a->scale > q ? a->scale : q);
    a->scale -= q;

    a = unsafe_normalize_full(a);

    if (r) {
        deci_UWORD f = 1;
        for (size_t i = r; i; --i) {
            f *= 10;
        }
        a = number_mul_uword(a, f);
    }
    return a;
}
