// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "prompt.h"

#if NO_READLINE

const char *PROMPT_NORMAL = "~~> ";
const char *PROMPT_CONT = "_...> ";

void prompt_begin(void)
{
}

char *prompt_read_line(const char *prompt, bool save)
{
    (void) save;

    fputs(prompt, stderr);

    char *buf = NULL;
    size_t nbuf = 128;
    ssize_t r = getline(&buf, &nbuf, stdin);
    if (r > 0) {
        return buf;
    } else {
        fputc('\n', stderr);
        free(buf);
        return NULL;
    }
}

void prompt_free(char *s)
{
    free(s);
}

void prompt_end(void)
{
}

#else

#if READLINE_VIA_LIBEDIT
# include <editline/readline.h>
# include <editline/history.h>
#else
# include <readline/readline.h>
# include <readline/history.h>
#endif

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

#endif
