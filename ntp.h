// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

#include "libdeci/deci.h"

typedef struct {
    size_t scale;
    deci_UWORD submod;
} NumberTruncateParams;

NumberTruncateParams ntp_from_prec(size_t prec);

size_t ntp_to_prec(NumberTruncateParams ntp);
