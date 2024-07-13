// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "common.h"
#include <time.h>

#include "vm.h"
#include "dasm.h"
#include "dict.h"
#include "parse.h"
#include "str.h"
#include "list.h"
#include "number.h"
#include "text_manip.h"
#include "prompt.h"

static bool debug_flag = false;
static char *calx_path = NULL;

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} TextBuf;

static inline TextBuf text_buf_new(void)
{
    return (TextBuf) {NULL, 0, 0};
}

static inline TextBuf text_buf_new_copy(const char *buf, size_t nbuf)
{
    return (TextBuf) {uu_xmemdup(buf, nbuf), nbuf, nbuf};
}

static inline void text_buf_ensure(TextBuf *tb, size_t n)
{
    while (tb->capacity < n) {
        tb->data = uu_x2realloc(tb->data, &tb->capacity, sizeof(char));
    }
}

static inline void text_buf_append(TextBuf *tb, const char *buf, size_t nbuf)
{
    text_buf_ensure(tb, tb->size + nbuf);
    if (nbuf)
        memcpy(tb->data + tb->size, buf, nbuf);
    tb->size += nbuf;
}

static inline void text_buf_destroy(TextBuf *tb)
{
    free(tb->data);
}

static void print_parse_error(
        const char *source, size_t nsource,
        const char *origin,
        const ParseError *e)
{
    if (e->size != (size_t) -1) {
        fprintf(stderr, ">>> Parse error at %s:%zu:%zu:\n", origin, e->pos.line, e->pos.column);

        text_show_line_segment(
            stderr, source, nsource,
            /*lineno=*/e->pos.line - 1,
            /*seg_off=*/e->pos.column - 1,
            /*seg_len=*/e->size);

        fprintf(stderr, " %s\n", e->msg);

    } else {
        fprintf(stderr, ">>> Parse error in %s: %s\n", origin, e->msg);
    }
}

static Func *load_string(
        State *state,
        const char *source, size_t nsource,
        const char *origin,
        int *more)
{
    ParseResult pr = parse(state, source, nsource, origin);

    if (!pr.ok) {
        if (more) {
            if (pr.as.error.need_more) {
                *more = 1;
                return NULL;
            }
            *more = 0;
        }
        print_parse_error(source, nsource, origin, &pr.as.error);
        free(pr.as.error.msg);
        return NULL;
    }

    Func *f = pr.as.func;

    if (debug_flag) {
        fprintf(stderr, "--- dump of %s ---\n", origin);
        dasm(f->ip, func_shape(f).offset, stderr);
        fprintf(stderr, "--- end of dump ---\n");
    }

    return f;
}

static Func *load_fd(State *state, int fd, const char *origin)
{
    TextBuf buf = text_buf_new();
    for (;;) {
        text_buf_ensure(&buf, buf.size + 1);
        ssize_t r = read(fd, buf.data + buf.size, buf.capacity - buf.size);
        if (UU_UNLIKELY(r < 0))
            goto io_error;
        if (r == 0)
            break;
        buf.size += r;
    }
    Func *f = load_string(state, buf.data, buf.size, origin, /*more=*/NULL);
    text_buf_destroy(&buf);
    return f;

io_error:
    perror(origin);
    text_buf_destroy(&buf);
    return NULL;
}

static Func *load_file(State *state, const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(path);
        return NULL;
    }
    Func *f = load_fd(state, fd, path);
    close(fd);
    return f;
}

static void guardn(State *state, size_t actual, size_t expect)
{
    if (UU_UNLIKELY(actual != expect))
        state_throw(state, "# of arguments: expected %zu, got %zu", expect, actual);
}

static void guardn_range(State *state, size_t actual, size_t expect_min, size_t expect_max)
{
    if (UU_UNLIKELY(actual < expect_min || actual > expect_max))
        state_throw(
            state, "# of arguments: expected %zu to %zu, got %zu",
            expect_min, expect_max, actual);
}

static Value guardv(State *state, Value *args, size_t i, char kind)
{
    Value v = args[i];

    if (UU_UNLIKELY(v->kind != kind))
        state_throw(
            state, "argument #%zu: expected %s, got %s",
            i + 1, value_kind_name_long(kind), value_kind_name_long(v->kind));
    return v;
}

static MaybeValue guardv_opt(State *state, Value *args, size_t i, char kind)
{
    Value v = args[i];

    if (v->kind == VK_NIL)
        return NULL;

    if (UU_UNLIKELY(v->kind != kind))
        state_throw(
            state, "argument #%zu: expected %s or nil, got %s",
            i + 1, value_kind_name_long(kind), value_kind_name_long(v->kind));
    return v;
}

// Borrows (takes regular references to):
//   * 'x'.
static size_t guard_scale(State *state, Number *x)
{
    size_t r = number_to_zu(x);
    if (UU_UNLIKELY(r == SIZE_MAX)) {
        if (x->sign)
            state_throw(state, "scale is negative");
        else
            state_throw(state, "scale is too big");
    }
    return r;
}

// Borrows (takes regular references to):
//   * 'x'.
static uint8_t guard_base(State *state, Number *x)
{
    size_t r = number_to_zu(x);
    if (UU_UNLIKELY(r < 2 || r > 36)) {
        state_throw(state, "invalid base");
    }
    return r;
}

// Borrows (takes regular references to):
//   * 'x'.
static size_t guard_magnitude(State *state, Number *x)
{
    size_t r = number_to_zu(x);
    if (UU_UNLIKELY(r == SIZE_MAX)) {
        if (x->sign)
            state_throw(state, "scale magnitude is negative");
        else
            state_throw(state, "scale magnitude is too big");
    }
    return r;
}

static Value X_Dasm(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Func *f = (Func *) guardv(state, args, 0, VK_FUNC);

    dasm(f->ip, func_shape(f).offset, stderr);
    return mk_nil();
}

static Value X_Kind(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Value v = args[0];

    const char *s = value_kind_name(v->kind);
    return (Value) string_new(s, strlen(s));
}

static Value X_Pop(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    List *list = (List *) guardv(state, args, 0, VK_LIST);

    if (!list->size)
        state_throw(state, "the list is empty");

    return list_pop(list);
}

static Value X_Input(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 0);
    (void) args;

    char *s = prompt_read_line("Input() -> ", /*save=*/false);
    if (!s)
        return (Value) string_new(NULL, 0);
    String *r = string_new(s, strlen(s));
    prompt_free(s);
    return (Value) r;
}

static Value X_Ord(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    if (s->size != 1)
        state_throw(state, "can only be applied to a single-character string");

    return (Value) number_new_from_zu((unsigned char) s->data[0]);
}

static Value X_Chr(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);

    char c = number_to_u32(n);
    return (Value) string_new(&c, 1);
}

static Value X_Error(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    size_t ns = s->size;
    if (ns > 8192)
        ns = 8192;

    state_throw(state, "%.*s", (int) ns, s->data);
}

static Value X_RawRead(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    if (s->size != 1)
        goto invalid_arg;

    switch (s->data[0]) {
    case 'L':
        {
            char *buf;
            size_t nbuf;
            ssize_t n = getline(&buf, &nbuf, stdin);
            if (n < 0)
                n = 0;
            String *x = string_new(buf, n);
            free(buf);
            return (Value) x;
        }
    case 's':
        {
            char *buf;
            size_t nbuf;
            ssize_t n = getline(&buf, &nbuf, stdin);
            if (n < 0) {
                n = 0;
            } else {
                if (n && buf[n - 1] == '\n')
                    --n;
            }
            String *x = string_new(buf, n);
            free(buf);
            return (Value) x;
        }
    case 'B':
        {
            int v = getchar();
            if (v == EOF)
                return (Value) string_new(NULL, 0);
            char c = v;
            return (Value) string_new(&c, 1);
        }
    default:
        goto invalid_arg;
    }
invalid_arg:
    state_throw(state, "invalid argument; expected either of: \"L\", \"s\", \"B\"");
}

static Value X_RawWrite(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    if (s->size)
        fwrite(s->data, 1, s->size, stdout);
    return mk_nil();
}

static Value X_Clock(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 0);
    (void) args;

    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        state_throw(state, "clock_gettime() failed");

    char buf[64];
    snprintf(
        buf, sizeof(buf),
        "%" PRIu64 ".%09" PRIu32,
        (uint64_t) ts.tv_sec, (uint32_t) ts.tv_nsec);
    return (Value) number_parse(buf, buf + strlen(buf));
}

static Value X_Scale(State *state, Value *args, uint32_t nargs)
{
    guardn_range(state, nargs, 0, 1);
    if (nargs == 0) {
        // get scale
        NumberTruncateParams ntp = state_get_ntp(state);
        size_t p = ntp_to_prec(ntp);
        return (Value) number_new_from_zu(p);
    }
    // set scale
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);
    size_t p = guard_scale(state, n);
    state_set_ntp(state, ntp_from_prec(p));
    return mk_nil();
}

static Value X_Where(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 0);
    (void) args;

    state_print_traceback(state);
    return mk_nil();
}

static Value X_Random32(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 0);
    (void) args;

    enum { NBUF = 128 };
    static const char *RAND_PATH = "/dev/urandom";

    static int fd = -1;
    static uint32_t buf[NBUF];
    static uint32_t *buf_ptr = buf + NBUF;

    if (UU_UNLIKELY(fd < 0)) {
        fd = open(RAND_PATH, O_RDONLY | O_CLOEXEC);
        if (UU_UNLIKELY(fd < 0)) {
            perror(RAND_PATH);
            state_throw(state, "cannot open random device");
        }
    }
    if (UU_UNLIKELY(buf_ptr == buf + NBUF)) {
        ssize_t r = read(fd, buf, sizeof(buf));
        if (UU_UNLIKELY(r != sizeof(buf)))
            state_throw(state, "truncated or failed read from random device");
        buf_ptr = buf;
    }
    return (Value) number_new_from_zu(*buf_ptr++);
}

static Value X_Trunc(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);

    value_ref((Value) n);
    return (Value) number_trunc(n);
}

static Value X_Floor(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);

    value_ref((Value) n);
    return (Value) number_floor(n);
}

static Value X_Ceil(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);

    value_ref((Value) n);
    return (Value) number_ceil(n);
}

static Value X_Round(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);

    value_ref((Value) n);
    return (Value) number_round(n);
}

static Value X_Frac(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    Number *n = (Number *) guardv(state, args, 0, VK_NUM);

    value_ref((Value) n);
    return (Value) number_frac(n);
}

static Value X_LoadString(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    Func *f = load_string(state, s->data, s->size, "(LoadString() arg)", /*more=*/NULL);
    if (!f)
        state_throw(state, "compilation failed");
    return (Value) f;
}

static Value X_Require(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    if (!calx_path)
        state_throw(state, "cannot Require(): CALX_PATH was not set");

    size_t ns = s->size;
    if (!ns)
        state_throw(state, "empty string passed");
    for (size_t i = 0; i < ns; ++i) {
        switch (s->data[i]) {
        case '\0':
        case '.':
        case '/':
            state_throw(state, "module name contains prohibited symbol");
        }
    }

    if (ns > 8192)
        state_throw(state, "module name is too long");

    char *path = uu_xstrf("%s/%.*s.calx", calx_path, (int) ns, s->data);
    Func *f = load_file(state, path);
    free(path);

    if (!f)
        state_throw(state, "cannot load module");

    MaybeValue r = state_eval(state, f);
    if (!r)
        state_throw(state, "module evaluation failed");
    return r;
}

static Value X_NextKey(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 2);
    Dict *d = (Dict *) guardv(state, args, 0, VK_DICT);
    String *s = (String *) guardv_opt(state, args, 1, VK_STR);
    uint32_t key_idx;
    if (!s) {
        key_idx = xht_indexed_first(&d->xht, /*start_bucket=*/0);
    } else {
        key_idx = xht_indexed_next(&d->xht, s->data, s->size, s->hash);
    }

    if (key_idx == (uint32_t) -1)
        return mk_nil();

    size_t nk;
    const char *k = xht_indexed_key(&d->xht, key_idx, &nk);
    return (Value) string_new(k, nk);
}

static Value X_RemoveKey(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 2);
    Dict *d = (Dict *) guardv(state, args, 0, VK_DICT);
    String *s = (String *) guardv(state, args, 1, VK_STR);

    (void) dict_remove(d, s);
    return mk_nil();
}

static Value X_ToNumber(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    String *s = (String *) guardv(state, args, 0, VK_STR);

    const char *begin = s->data;
    const char *end = begin + s->size;

    if (!number_parse_base_validate(begin, end, /*base=*/10))
        state_throw(state, "invalid number format");

    return (Value) number_parse(begin, end);
}

static Value X_Encode(State *state, Value *args, uint32_t nargs)
{
    guardn_range(state, nargs, 2, 3);
    Number *a = (Number *) guardv(state, args, 0, VK_NUM);
    Number *b = (Number *) guardv(state, args, 1, VK_NUM);
    Number *s = NULL;
    if (nargs == 3)
        s = (Number *) guardv_opt(state, args, 2, VK_NUM);

    size_t scale = s ? guard_scale(state, s) : 0;
    uint8_t base = guard_base(state, b);

    size_t nr = number_tostring_base_size(a, base, scale);
    if (UU_UNLIKELY(nr == SIZE_MAX))
        UU_PANIC_OOM();

    String *r = string_new(NULL, 0);
    r = string_hot_append_begin(r, nr);
    char *buf = string_hot_append_buf(r);
    size_t nwritten = number_tostring_base(a, base, scale, buf);
    r = string_hot_append_end(r, nwritten);
    return (Value) r;
}

static Value X_Decode(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 2);
    String *s = (String *) guardv(state, args, 0, VK_STR);
    Number *b = (Number *) guardv(state, args, 1, VK_NUM);

    size_t base = guard_base(state, b);

    const char *begin = s->data;
    const char *end = begin + s->size;

    if (!number_parse_base_validate(begin, end, /*base=*/base))
        state_throw(state, "invalid number format");

    NumberTruncateParams ntp = state_get_ntp(state);
    return (Value) number_parse_base(begin, end, /*base=*/base, /*ntp=*/ntp);
}

static Value X_NumDigits(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 2);
    Number *x = (Number *) guardv(state, args, 0, VK_NUM);
    String *s = (String *) guardv(state, args, 1, VK_STR);

    if (s->size != 1)
        goto invalid_str;
    switch (s->data[0]) {
    case 'i':
        {
            size_t n = number_nintdigits(x);
            if (UU_UNLIKELY(n == SIZE_MAX))
                goto overflow;
            return (Value) number_new_from_zu(n);
        }
    case 'f':
        {
            size_t n = number_nfracdigits(x);
            if (UU_UNLIKELY(n == SIZE_MAX))
                goto overflow;
            return (Value) number_new_from_zu(n);
        }
    case '+':
        {
            size_t n1 = number_nintdigits(x);
            size_t n2 = number_nfracdigits(x);
            size_t n = uu_add_zu_or_saturate(n1, n2);
            if (UU_UNLIKELY(n == SIZE_MAX))
                goto overflow;
            return (Value) number_new_from_zu(n);
        }
    default:
        goto invalid_str;
    }
invalid_str:
    state_throw(state, "invalid second argument; expected either of: \"i\", \"f\", \"+\"");
overflow:
    state_throw(state, "overflow (result >= SIZE_MAX)");
}

static Value X_DownScale(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 2);
    Number *x = (Number *) guardv(state, args, 0, VK_NUM);
    Number *m = (Number *) guardv(state, args, 1, VK_NUM);

    size_t mag = guard_magnitude(state, m);

    value_ref((Value) x);
    return (Value) number_scale_down(x, mag);
}

static Value X_UpScale(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 2);
    Number *x = (Number *) guardv(state, args, 0, VK_NUM);
    Number *m = (Number *) guardv(state, args, 1, VK_NUM);

    size_t mag = guard_magnitude(state, m);

    value_ref((Value) x);
    return (Value) number_scale_up(x, mag);
}

static Value X_Wref(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);

    Value v = args[0];
    if (!value_kind_is_wrefable(v->kind))
        state_throw(state, "cannot make weakref to %s value", value_kind_name(v->kind));

    return (Value) wref_new((WeakRefable *) v);
}

static Value X_Wvalue(State *state, Value *args, uint32_t nargs)
{
    guardn(state, nargs, 1);
    WeakRef *w = (WeakRef *) guardv(state, args, 0, VK_WREF);

    MaybeValue target = (MaybeValue) w->target;
    if (target) {
        value_ref(target);
        return target;
    }
    return mk_nil();
}

static void init_globals(void)
{
    const char *s;

    if ((s = getenv("CALX_DEBUG")))
        debug_flag = (strchr(s, '1'));

    if ((s = getenv("CALX_PATH")) && s[0] != '\0')
        calx_path = uu_xstrdup(s);
}

static void free_globals(void)
{
    free(calx_path);
}

static void inject_stdlib(State *state)
{
    const char *source =
#include "stdlib_calx.generated.inc"
    ;

    Func *f = load_string(state, source, strlen(source), "(stdlib)", /*more=*/NULL);
    if (UU_UNLIKELY(!f))
        UU_PANIC("cannot compile stdlib (see above)");

    MaybeValue r = state_eval(state, f);
    if (UU_UNLIKELY(!r))
        UU_PANIC("cannot evaluate stdlib (see above)");
    value_unref(r);
}

static State *make_state(void)
{
    State *state = state_new();

#define PAIR(S) S, strlen(S)
    state_steal_global(state, PAIR("Dasm"), mk_cfunc(X_Dasm));
    state_steal_global(state, PAIR("Kind"), mk_cfunc(X_Kind));
    state_steal_global(state, PAIR("Pop"), mk_cfunc(X_Pop));
    state_steal_global(state, PAIR("RemoveKey"), mk_cfunc(X_RemoveKey));
    state_steal_global(state, PAIR("Input"), mk_cfunc(X_Input));
    state_steal_global(state, PAIR("Ord"), mk_cfunc(X_Ord));
    state_steal_global(state, PAIR("Chr"), mk_cfunc(X_Chr));
    state_steal_global(state, PAIR("Error"), mk_cfunc(X_Error));
    state_steal_global(state, PAIR("RawRead"), mk_cfunc(X_RawRead));
    state_steal_global(state, PAIR("RawWrite"), mk_cfunc(X_RawWrite));
    state_steal_global(state, PAIR("Clock"), mk_cfunc(X_Clock));
    state_steal_global(state, PAIR("Scale"), mk_cfunc(X_Scale));
    state_steal_global(state, PAIR("Where"), mk_cfunc(X_Where));
    state_steal_global(state, PAIR("Random32"), mk_cfunc(X_Random32));
    state_steal_global(state, PAIR("trunc"), mk_cfunc(X_Trunc));
    state_steal_global(state, PAIR("floor"), mk_cfunc(X_Floor));
    state_steal_global(state, PAIR("ceil"), mk_cfunc(X_Ceil));
    state_steal_global(state, PAIR("round"), mk_cfunc(X_Round));
    state_steal_global(state, PAIR("frac"), mk_cfunc(X_Frac));
    state_steal_global(state, PAIR("LoadString"), mk_cfunc(X_LoadString));
    state_steal_global(state, PAIR("Require"), mk_cfunc(X_Require));
    state_steal_global(state, PAIR("NextKey"), mk_cfunc(X_NextKey));
    state_steal_global(state, PAIR("ToNumber"), mk_cfunc(X_ToNumber));
    state_steal_global(state, PAIR("Encode"), mk_cfunc(X_Encode));
    state_steal_global(state, PAIR("Decode"), mk_cfunc(X_Decode));
    state_steal_global(state, PAIR("NumDigits"), mk_cfunc(X_NumDigits));
    state_steal_global(state, PAIR("DownScale"), mk_cfunc(X_DownScale));
    state_steal_global(state, PAIR("UpScale"), mk_cfunc(X_UpScale));
    state_steal_global(state, PAIR("Wref"), mk_cfunc(X_Wref));
    state_steal_global(state, PAIR("Wvalue"), mk_cfunc(X_Wvalue));
#undef PAIR

    inject_stdlib(state);

    return state;
}

static void maybe_load_rc(State *state)
{
    if (!calx_path)
        return;

    char *path = uu_xstrf("%s/rc.calx", calx_path);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        goto open_error;

    Func *f = load_fd(state, fd, path);
    if (f) {
        MaybeValue r = state_eval(state, f);
        maybe_value_unref(r);
    }
    close(fd);
    free(path);
    return;

open_error:
    if (errno != ENOENT)
        perror(path);
    free(path);
}

static void interactive_mode(void)
{
    State *state = make_state();
    maybe_load_rc(state);

    prompt_begin();

    for (;;) {
        char *line = prompt_read_line(PROMPT_NORMAL, /*save=*/true);
        if (!line)
            break;
        TextBuf buf = text_buf_new_copy(line, strlen(line));
        prompt_free(line);

        for (;;) {
            // Try to compile the current contents of the buffer.
            int need_more;
            Func *f = load_string(state, buf.data, buf.size, "(input)", /*more=*/&need_more);
            if (!f) {
                if (need_more) {
                    // A failure, but a recoverable one; prompt for the continuation of the input.
                    char *cont = prompt_read_line(PROMPT_CONT, /*save=*/true);
                    if (!cont) {
                        // User refused to enter continuation; break the inner loop.
                        break;
                    }
                    // Append the continuation to the current buffer.
                    text_buf_append(&buf, "\n", 1);
                    text_buf_append(&buf, cont, strlen(cont));

                    prompt_free(cont);

                    // Continue the inner loop.

                } else {
                    // Irrecoverable failure; error has been printed.
                    break;
                }

            } else {
                // Success; evaluate it.
                MaybeValue r = state_eval(state, f);
                // Just ignore the return value.
                maybe_value_unref(r);
                // Break the inner loop.
                break;
            }
        }

        text_buf_destroy(&buf);
    }

    prompt_end();

    state_destroy(state);
}

static bool inline_mode(const char *source)
{
    bool ret = false;
    State *state = make_state();

    Func *f = load_string(state, source, strlen(source), "('-c' argument)", /*more=*/NULL);
    if (!f)
        goto done;
    MaybeValue r = state_eval(state, f);
    if (!r)
        goto done;
    value_unref(r);
    ret = true;
done:
    state_destroy(state);
    return ret;
}

static bool file_mode(const char *path)
{
    bool ret = false;
    State *state = make_state();

    Func *f;
    if (strcmp(path, "-") == 0) {
        f = load_fd(state, 0, "(stdin)");
    } else {
        f = load_file(state, path);
    }
    if (!f)
        goto done;
    MaybeValue r = state_eval(state, f);
    if (!r)
        goto done;
    value_unref(r);
    ret = true;
done:
    state_destroy(state);
    return ret;
}

static void print_usage(void)
{
    fprintf(stderr, "USAGE: calx\n");
    fprintf(stderr, "       calx FILE\n");
    fprintf(stderr, "       calx -c CODE\n");
}

int main(int argc, char **argv)
{
    init_globals();

    int ret;
    const char *c_arg = NULL;

    for (int c; (c = getopt(argc, argv, "c:")) != -1;) {
        switch (c) {
        case 'c':
            if (c_arg) {
                fprintf(stderr, "Usage error: multiple '-c' flags.\n");
                print_usage();
                ret = 2;
                goto done;
            }
            c_arg = optarg;
            break;
        default:
            print_usage();
            ret = 2;
            goto done;
        }
    }

    switch (argc - optind) {
    case 0:
        if (c_arg) {
            ret = inline_mode(c_arg) ? 0 : 1;
        } else {
            interactive_mode();
            ret = 0;
        }
        goto done;
    case 1:
        if (c_arg) {
            fprintf(stderr, "Usage error: '-c' and file argument are mutually exclusive.\n");
            print_usage();
            ret = 2;
            goto done;
        }
        ret = file_mode(argv[optind]) ? 0 : 1;
        goto done;
    default:
        fprintf(stderr, "Usage error: multiple positional arguments.\n");
        print_usage();
        ret = 2;
        goto done;
    }
done:
    free_globals();
    return ret;
}
