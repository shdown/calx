// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

const char *text_nth_line(const char *text, size_t ntext, size_t lineno, size_t *out_len);

void text_putnc(FILE *out, int c, size_t n);

void text_show_line(FILE *out, const char *text, size_t ntext, size_t lineno);

void text_show_line_segment(
    FILE *out, const char *text, size_t ntext, size_t lineno,
    size_t seg_offset, size_t seg_len);
