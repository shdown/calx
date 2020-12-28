// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "libdeci/deci.h"

void fancy_mul(
    deci_UWORD *wa, size_t nwa,
    deci_UWORD *wb, size_t nwb,
    deci_UWORD *out);

size_t fancy_div(
    deci_UWORD *wa, size_t nwa,
    deci_UWORD *wb, size_t nwb);

size_t fancy_mod(
    deci_UWORD *wa, size_t nwa,
    deci_UWORD *wb, size_t nwb);
