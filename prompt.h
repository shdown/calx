// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

extern const char *PROMPT_NORMAL;

extern const char *PROMPT_CONT;

void prompt_begin(void);

char *prompt_read_line(const char *prompt, bool save);

void prompt_free(char *);

void prompt_end(void);
