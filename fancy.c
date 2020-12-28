// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "fancy.h"
#include "libdeci-kara/decikara.h"
#include "libdeci-ntt/decintt.h"
#include "libdeci-newt/decinewt.h"

enum {
    MUL_BASECASE_CUTOFF = 30,
    MUL_KARATSUBA_CUTOFF = 75,

    DIV_BASECASE_CUTOFF = 75,
};

void fancy_mul(
    deci_UWORD *wa, size_t nwa,
    deci_UWORD *wb, size_t nwb,
    deci_UWORD *out)
{
    size_t min_n = nwa < nwb ? nwa : nwb;
    if (min_n < MUL_BASECASE_CUTOFF) {
        deci_zero_out_n(out, nwa + nwb);
        deci_mul(wa, wa + nwa, wb, wb + nwb, out);
    } else if (min_n < MUL_KARATSUBA_CUTOFF) {
        size_t nscratch = decikara_nscratch(nwa, nwb, MUL_BASECASE_CUTOFF);
        deci_UWORD *scratch = uu_xmalloc(sizeof(deci_UWORD), nscratch);
        decikara_mul(wa, nwa, wb, nwb, scratch, out, MUL_BASECASE_CUTOFF);
        free(scratch);
    } else {
        if (wa == wb && nwa == nwb) {
            size_t nbytes = decintt_sqr_nscratch_bytes(nwa);
            void *scratch = uu_xmalloc(1, nbytes);
            decintt_sqr(wa, nwa, out, scratch);
            free(scratch);
        } else {
            size_t nbytes = decintt_mul_nscratch_bytes(nwa, nwb);
            void *scratch = uu_xmalloc(1, nbytes);
            decintt_mul(wa, nwa, wb, nwb, out, scratch);
            free(scratch);
        }
    }
}

static int mul_callback(
        void *userdata,
        deci_UWORD *wa, size_t nwa,
        deci_UWORD *wb, size_t nwb,
        deci_UWORD *out)
{
    (void) userdata;

    if (out == wa) {
        deci_UWORD *wa_copy = uu_xmemdup(wa, nwa * sizeof(deci_UWORD));
        fancy_mul(wa_copy, nwa, wb, nwb, out);
        free(wa_copy);
    } else if (out == wb) {
        deci_UWORD *wb_copy = uu_xmemdup(wb, nwb * sizeof(deci_UWORD));
        fancy_mul(wa, nwa, wb_copy, nwb, out);
        free(wb_copy);
    } else {
        fancy_mul(wa, nwa, wb, nwb, out);
    }

    return 0;
}

static inline size_t quotient_or_remainder(
        deci_UWORD *wa, size_t nwa,
        deci_UWORD *wb, size_t nwb,
        bool quotient)
{
    nwa = deci_normalize_n(wa, nwa);
    nwb = deci_normalize_n(wb, nwb);

    if (nwa < nwb)
        goto basecase;

    size_t ndelta = nwa - nwb + 1;
    size_t min_n = ndelta < nwb ? ndelta : nwb;
    if (min_n < DIV_BASECASE_CUTOFF)
        goto basecase;

    if (nwb < DECINEWT_MIN)
        goto basecase;
    size_t nscratch = decinewt_div_nscratch(nwa, nwb);
    deci_UWORD *scratch = uu_xmalloc(sizeof(deci_UWORD), nscratch);
    int r = decinewt_div(wa, nwa, wb, nwb, scratch, NULL, mul_callback);
    if (UU_UNLIKELY(r < 0)) {
        UU_PANIC("unexpected failure in decinewt_div");
    }
    size_t retval;
    if (quotient) {
        deci_memcpy(wa, scratch + nwa + 1, ndelta);
        retval = ndelta;
    } else {
        (void) deci_sub_raw(wa, wa + nwa, scratch, scratch + nwa);
        retval = nwa;
    }
    free(scratch);
    return retval;

basecase:
    if (quotient) {
        return deci_div(wa, wa + nwa, wb, wb + nwb);
    } else {
        return deci_mod(wa, wa + nwa, wb, wb + nwb);
    }
}

size_t fancy_div(
        deci_UWORD *wa, size_t nwa,
        deci_UWORD *wb, size_t nwb)
{
    return quotient_or_remainder(wa, nwa, wb, nwb, true);
}

size_t fancy_mod(
        deci_UWORD *wa, size_t nwa,
        deci_UWORD *wb, size_t nwb)
{
    return quotient_or_remainder(wa, nwa, wb, nwb, false);
}
