// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

#include "vm.h"
#include "position.h"

typedef struct {
    Position pos;

    // This might be '(size_t) -1', which means no size (nor position) is available.
    size_t size;

    // Allocated as if with 'malloc()'.
    char *msg;

    // Whether the error could, in theory, be recovered by appending something to the source.
    bool need_more;
} ParseError;

typedef struct {
    union {
        Func *func;
        ParseError error;
    } as;
    bool ok;
} ParseResult;

ParseResult parse(State *state, const char *source, size_t nsource, const char *origin);
