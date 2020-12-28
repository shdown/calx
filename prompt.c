// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "prompt.h"

#include <readline/readline.h>
#include <readline/history.h>

const char *PROMPT_NORMAL = "≈≈> ";
const char *PROMPT_CONT = "×⋅⋅⋅> ";

void prompt_begin(void)
{
    using_history();
}

char *prompt_read_line(const char *prompt, bool save)
{
    char *s = readline(prompt);
    if (!s) {
        fputc('\n', stderr);
        return NULL;
    }
    if (save)
        add_history(s);
    return s;
}

void prompt_free(char *s)
{
    free(s);
}

void prompt_end(void)
{
}
