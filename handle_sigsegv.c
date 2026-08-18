// (c) 2026 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "handle_sigsegv.h"
#include <signal.h>

#if NO_HANDLE_SIGSEGV

void install_sigsegv_handler(void)
{
}

void uninstall_sigsegv_handler(void)
{
}

#else

static const char MSG[] =
    "Caught SIGSEGV!\n"
    "\n"
    "calx crashed. This might indicate some problem in calx, but also can be\n"
    "expected in the following situations:\n"
    "\n"
    " * program's code has too many levels of nested things, e.g. ((((...))));\n"
    "\n"
    " * program has created too many nested objects in runtime.\n"
    "\n"
    "If you think neither of these is the case, please report this problem to\n"
    "the upstream.\n"
    "\n"
    "Bye!\n"
    ;
static const size_t MSG_LEN = sizeof(MSG) - 1;

static void full_write(int fd, const char *data, size_t ndata)
{
    size_t nwritten = 0;
    while (nwritten != ndata) {
        ssize_t w = write(fd, data + nwritten, ndata - nwritten);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        nwritten += w;
    }
}

static void my_sighandler_for_sigsegv(int signo)
{
    (void) signo;

    full_write(2, MSG, MSG_LEN);

    abort();
}

static void do_warn(void)
{
    fprintf(stderr, "WARNING: something went wrong with installing signal handler for SIGSEGV.\n");
}

static void *sp;

static void do_set_handler(
    void (*new_handler)(int),
    int flags)
{
    struct sigaction my_sa = {
        .sa_handler = new_handler,
        .sa_flags = flags,
    };
    if (sigemptyset(&my_sa.sa_mask) < 0) {
        perror("sigemptyset");
        do_warn();
        return;
    }
    if (sigaction(SIGSEGV, &my_sa, NULL) < 0) {
        perror("sigaction");
        do_warn();
        return;
    }
}

void install_sigsegv_handler(void)
{
    size_t sp_size = SIGSTKSZ;

    sp = uu_xmalloc(sp_size, 1);

    stack_t my_altstack = {
        .ss_sp = sp,
        .ss_flags = 0,
        .ss_size = sp_size,
    };
    if (sigaltstack(&my_altstack, NULL) < 0) {
        perror("sigaltstack");
        do_warn();
        return;
    }

    do_set_handler(my_sighandler_for_sigsegv, SA_ONSTACK);
}

void uninstall_sigsegv_handler(void)
{
    do_set_handler(SIG_DFL, 0);

    stack_t my_altstack = {
        .ss_flags = SS_DISABLE,
    };
    if (sigaltstack(&my_altstack, NULL) < 0) {
        perror("sigaltstack");
    }

    free(sp);
}

#endif
