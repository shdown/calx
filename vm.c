// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "vm.h"
#include "xht.h"
#include "str.h"
#include "number.h"
#include "list.h"
#include "dict.h"
#include "wref.h"
#include "text_manip.h"

GcHeader value_cache[] = {
    [VALUE_CACHE_NIL]   = {.nrefs = 1, .kind = VK_NIL},
    [VALUE_CACHE_FALSE] = {.nrefs = 1, .kind = VK_FLAG},
    [VALUE_CACHE_TRUE]  = {.nrefs = 1, .kind = VK_FLAG},
};

typedef struct {
    // Owning references are 'begin' ... 'top - 1'.
    Value *begin;
    Value *top;
    Value *end;
} ValueStack;

// A callsite is a site where one (bytecode) function calls another (bytecode) function.
typedef struct {
    // Pointer to the instruction at which the call was performed (with 'OP_CALL' opcode).
    Instr *call_ip;

    // The function being called.
    Func *callee; // non-owning reference

    // Caller's constants.
    Value *prev_consts; // non-owning reference

    // Offset from the bottom of the value stack to the beginning of the caller's locals.
    size_t prev_locals_offset;
} CallSite;

typedef struct {
    CallSite *begin;
    CallSite *top;
    CallSite *end;
} CallStack;

typedef struct {
    ValueStack vs;
    CallStack cs;
    Instr *ip;
    Value *locals;
    Value *consts;
} RuntimeInfo;

typedef struct ScratchPad {
    RuntimeInfo rti;

    char *err_msg;
    jmp_buf err_handler;

    struct ScratchPad *prev;
} ScratchPad;

typedef struct {
    MaybeValue *data;
    size_t size;
    size_t capacity;
} GlobalList;

struct State {
    xHt globals_table;
    GlobalList globals;

    ScratchPad *pad;

    NumberTruncateParams ntp;
};

State *state_new(void)
{
    State *state = uu_xmalloc(sizeof(State), 1);
    *state = (State) {
        .globals_table = xht_new(0),
        .globals = {NULL, 0, 0},
        .pad = NULL,
        .ntp = ntp_from_prec(20),
    };
    return state;
}

void state_destroy(State *state)
{
    xht_destroy(&state->globals_table);
    for (size_t i = 0; i < state->globals.size; ++i)
        maybe_value_unref(state->globals.data[i]);
    free(state->globals.data);
    free(state);
}

static inline UU_ALWAYS_INLINE __attribute__((warn_unused_result))
Value *popn(Value *top, size_t n)
{
    for (; n; --n)
        value_unref(*--top);
    return top;
}

static inline UU_ALWAYS_INLINE __attribute__((warn_unused_result))
Value *pushn(Value *top, size_t n)
{
    for (; n; --n)
        *top++ = mk_nil();
    return top;
}

static __attribute__((noinline, format(printf, 2, 3)))
void state_prepare_error(State *state, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    state->pad->err_msg = uu_xstrvf(fmt, vl);
    va_end(vl);
}

static __attribute__((noreturn))
void state_throw_prepared_error(State *state)
{
    longjmp(state->pad->err_handler, 1);
}

void state_throw(State *state, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    state->pad->err_msg = uu_xstrvf(fmt, vl);
    va_end(vl);

    longjmp(state->pad->err_handler, 1);
}

Chunk *chunk_new_steal(
    Instr *code, size_t ncode,
    Value *consts, size_t nconsts,
    Quark *quarks, size_t nquarks,
    Shape *shapes, size_t nshapes,
    char *origin,
    char *source, size_t nsource)
{
    Chunk *chunk = uu_xmalloc(sizeof(Chunk), 1);
    *chunk = (Chunk) {
        .code = code,
        .ncode = ncode,
        .consts = consts,
        .nconsts = nconsts,
        .quarks = quarks,
        .nquarks = nquarks,
        .shapes = shapes,
        .nshapes = nshapes,
        .origin = origin,
        .source = source,
        .nsource = nsource,
        .nrefs = 1,
    };
    return chunk;
}

void chunk_destroy(Chunk *chunk)
{
    free(chunk->code);
    for (size_t i = 0; i < chunk->nconsts; ++i)
        value_unref(chunk->consts[i]);
    free(chunk->consts);
    free(chunk->quarks);
    free(chunk->shapes);
    free(chunk->origin);
    free(chunk->source);
    free(chunk);
}

void value_free(Value v)
{
    switch (v->kind) {
    case VK_NUM:
        number_destroy((Number *) v);
        break;
    case VK_STR:
        string_destroy((String *) v);
        break;
    case VK_LIST:
        list_destroy((List *) v);
        break;
    case VK_DICT:
        dict_destroy((Dict *) v);
        break;
    case VK_FUNC:
        chunk_unref(((Func *) v)->chunk);
        free(v);
        break;
    case VK_CFUNC:
        free(v);
        break;
    case VK_WREF:
        wref_destroy((WeakRef *) v);
        break;
    case VK_FLAG:
    case VK_NIL:
    default:
        __builtin_unreachable();
    }
}

uint32_t state_intern_global(State *state, const char *name, size_t nname)
{
    uint32_t old_size = state->globals.size;
    uint32_t idx = *xht_put_int(
        &state->globals_table,
        name, nname, hash_str(name, nname),
        old_size);

    if (idx == old_size) {
        if (state->globals.size == state->globals.capacity) {
            state->globals.data = uu_x2realloc(state->globals.data, &state->globals.capacity, sizeof(MaybeValue));
        }
        state->globals.data[state->globals.size++] = NULL;
    }

    return idx;
}

static ValueStack value_stack_new(size_t capacity)
{
    Value *begin = uu_xmalloc(sizeof(Value), capacity);
    return (ValueStack) {begin, begin, begin + capacity};
}

static inline void value_stack_ensure(ValueStack *vs, size_t n)
{
    if (UU_LIKELY(((size_t) (vs->end - vs->top)) >= n))
        return;

    size_t size = vs->top - vs->begin;
    size_t capacity = vs->end - vs->begin;
    do {
        vs->begin = uu_x2realloc(vs->begin, &capacity, sizeof(Value));
    } while (capacity - size < n);
    vs->top = vs->begin + size;
    vs->end = vs->begin + capacity;
}

static void value_stack_free(ValueStack vs)
{
    vs.top = popn(vs.top, vs.top - vs.begin);
    free(vs.begin);
}

static CallStack call_stack_new(size_t capacity)
{
    CallSite *begin = uu_xmalloc(sizeof(CallSite), capacity);
    return (CallStack) {begin, begin, begin + capacity};
}

static inline void call_stack_push(CallStack *cs, CallSite v)
{
    if (UU_UNLIKELY(cs->top == cs->end)) {
        size_t capacity = cs->end - cs->begin;
        size_t size = capacity;
        cs->begin = uu_x2realloc(cs->begin, &capacity, sizeof(CallSite));
        cs->top = cs->begin + size;
        cs->end = cs->begin + capacity;
    }
    *cs->top++ = v;
}

static void call_stack_free(CallStack cs)
{
    free(cs.begin);
}

static ScratchPad *scratch_pad_new(ScratchPad *prev)
{
    ScratchPad *pad = uu_xmalloc(sizeof(ScratchPad), 1);

    pad->rti.vs = value_stack_new(/*capacity=*/0);
    pad->rti.cs = call_stack_new(/*capacity=*/1);

    pad->err_msg = NULL;
    pad->prev = prev;

    return pad;
}

static ScratchPad *scratch_pad_free(ScratchPad *pad)
{
    ScratchPad *prev = pad->prev;

    value_stack_free(pad->rti.vs);
    call_stack_free(pad->rti.cs);

    free(pad->err_msg);

    free(pad);
    return prev;
}

// Borrows (takes regular references to):
//   * 'v'.
static inline UU_ALWAYS_INLINE bool value_is_truthy(Value v)
{
    return v != &value_cache[VALUE_CACHE_NIL] && v != &value_cache[VALUE_CACHE_FALSE];
}

static void num_writer_func(void *userdata, const char *buf, size_t nbuf)
{
    FILE *f = userdata;
    if (nbuf)
        fwrite(buf, 1, nbuf, f);
}

static void write_char_escaped(unsigned char c)
{
    static const char *HEX_CHARS = "0123456789ABCDEF";

    switch (c) {
    case '\0': fputs("\\0", stdout); break;
    case '\a': fputs("\\a", stdout); break;
    case '\b': fputs("\\b", stdout); break;
    case '\t': fputs("\\t", stdout); break;
    case '\n': fputs("\\n", stdout); break;
    case '\v': fputs("\\v", stdout); break;
    case '\f': fputs("\\f", stdout); break;
    case '\r': fputs("\\r", stdout); break;
    case '\033': fputs("\\e", stdout); break;
    case '\\': fputs("\\\\", stdout); break;
    case '"': fputs("\\\"", stdout); break;
    default:
        {
            char buf[4] = {'\\', 'x', HEX_CHARS[c / 16], HEX_CHARS[c % 16]};
            fwrite(buf, 1, sizeof(buf), stdout);
        }
    }
}

static void write_string_escaped(const char *s, size_t ns)
{
    const char *prev = s;
    const char *end = s + ns;

    fputc('"', stdout);

    for (; s != end; ++s) {
        unsigned char c = *s;
        if (UU_UNLIKELY(c < 32 || c == '\\' || c == '"')) {
            fwrite(prev, 1, s - prev, stdout);
            write_char_escaped(c);
            prev = s + 1;
        }
    }

    if (prev != s)
        fwrite(prev, 1, s - prev, stdout);

    fputc('"', stdout);
}

// Borrows (takes regular references to):
//   * 'v'.
static __attribute__((noinline))
void value_write(Value v, bool esc, unsigned reclimit)
{
    if (!reclimit--) {
        fputs("...", stdout);
        return;
    }

    switch (v->kind) {
    case VK_NUM:
        number_write((Number *) v, /*userdata=*/stdout, /*writer=*/num_writer_func);
        break;

    case VK_FLAG:
        fputs(v == &value_cache[VALUE_CACHE_TRUE] ? "true" : "false", stdout);
        break;

    case VK_NIL:
        fputs("nil", stdout);
        break;

    case VK_STR:
        if (esc) {
            String *s = (String *) v;
            write_string_escaped(s->data, s->size);
        } else {
            String *s = (String *) v;
            if (s->size) {
                fwrite(s->data, 1, s->size, stdout);
            }
        }
        break;

    case VK_LIST:
        {
            List *list = (List *) v;
            fputs("[", stdout);
            for (size_t i = 0; i < list->size; ++i) {
                if (i)
                    fputs(", ", stdout);
                value_write(list->data[i], true, reclimit);
            }
            fputs("]", stdout);
        }
        break;

    case VK_DICT:
        {
            Dict *dict = (Dict *) v;
            fputs("{", stdout);

            bool first = true;
            xht_foreach(&dict->xht, item, item_end) {
                if (!first) {
                    fputs(", ", stdout);
                }
                write_string_escaped(item->key, item->nkey);
                fputs(": ", stdout);
                value_write(item->value.p, true, reclimit);
                first = false;
            }

            fputs("}", stdout);
        }
        break;

    case VK_FUNC:
    case VK_CFUNC:
        fprintf(stdout, "<function at %p>", (void *) v);
        break;

    case VK_WREF:
        fprintf(stdout, "<weakref at %p>", (void *) v);
        break;

    default:
        __builtin_unreachable();
    }
}

// Borrows (takes regular references to):
//   * 'v'.
static inline void value_print(Value v)
{
    if (v->kind != VK_NIL) {
        value_write(v, /*esc=*/false, /*recimit=*/3);
        fputc('\n', stdout);
    }
}

const char *value_kind_name(char kind)
{
    switch (kind) {
    case VK_NUM: return "number";
    case VK_FLAG: return "flag";
    case VK_NIL: return "nil";
    case VK_STR: return "string";
    case VK_LIST: return "list";
    case VK_DICT: return "dict";
    case VK_WREF: return "weakref";
    case VK_FUNC: return "function";
    case VK_CFUNC: return "function";
    }
    return NULL;
}

const char *value_kind_name_long(char kind)
{
    switch (kind) {
    case VK_NUM: return "number";
    case VK_FLAG: return "flag";
    case VK_NIL: return "nil";
    case VK_STR: return "string";
    case VK_LIST: return "list";
    case VK_DICT: return "dict";
    case VK_WREF: return "weakref";
    case VK_FUNC: return "function (bytecode)";
    case VK_CFUNC: return "function (native code)";
    }
    return NULL;
}

// Borrows (takes regular references to):
//   * 'a';
//   * 'b'.
static inline UU_ALWAYS_INLINE bool values_equal(Value a, Value b)
{
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case VK_NUM:
        return number_compare((Number *) a, (Number *) b) == COMPARE_EQ;
    case VK_STR:
        return string_equal((String *) a, (String *) b);
    default:
        return a == b;
    }
}

// On success:
//   * returns 'true';
//   * steals 'left' and 'right';
//   * writes the result into '*out'.
// On failure:
//   * returns 'false';
//   * does NOT steal either 'left' or 'right';
//   * prepares the error message in 'state' (as if with 'state_prepare_error').
static bool perform_cmp_3way(State *state, Value left, Value right, uint8_t a, Value *out)
{
    if (left->kind == VK_NUM && right->kind == VK_NUM) {
        bool r = a & number_compare((Number *) left, (Number *) right);
        value_unref(left);
        value_unref(right);
        *out = mk_flag(r);
        return true;

    } else if (left->kind == VK_STR && right->kind == VK_STR) {
        bool r = a & string_compare((String *) left, (String *) right);
        value_unref(left);
        value_unref(right);
        *out = mk_flag(r);
        return true;

    } else {
        static const char *oprepr[] = {
            [COMPARE_LESS                ] = "<",
            [COMPARE_LESS | COMPARE_EQ   ] = "<=",
            [COMPARE_GREATER             ] = ">",
            [COMPARE_GREATER | COMPARE_EQ] = ">=",
        };
        state_prepare_error(
            state, "attempt to compute %s %s %s",
            value_kind_name(left->kind),
            oprepr[a],
            value_kind_name(right->kind));
        return false;
    }
}

// Steals (takes move references to):
//   * 's'.
// Borrows (takes regular references to):
//   * 'v'.
static __attribute__((warn_unused_result))
String *append_string_repr(String *s, Value v)
{
    switch (v->kind) {
    case VK_NUM:
        {
            Number *n = (Number *) v;
            size_t maxsz = number_tostring_size(n);
            if (UU_UNLIKELY(maxsz == SIZE_MAX))
                UU_PANIC_OOM();
            s = string_hot_append_begin(s, maxsz);
            char *buf = string_hot_append_buf(s);
            size_t nwritten = number_tostring(n, buf);
            s = string_hot_append_end(s, nwritten);
            return s;
        }

    case VK_STR:
        {
            String *t = (String *) v;
            return string_append(s, t->data, t->size);
        }

    case VK_FLAG:
        {
            const char *t = (v == &value_cache[VALUE_CACHE_TRUE]) ? "true" : "false";
            return string_append(s, t, strlen(t));
        }

    default:
        {
            const char *t = value_kind_name(v->kind);
            s = string_append(s, "<", 1);
            s = string_append(s, t, strlen(t));
            s = string_append(s, ">", 1);
            return s;
        }
    }
}

// Steals (takes move references to):
//   * 'b';
//   * 'e'.
//
// Writes the result into '*out'.
static bool do_pow(State *state, Number *b, Number *e, Value *out)
{
    if (UU_UNLIKELY(e->sign && !number_is_zero(e))) {
        state_prepare_error(state, "exponent is negative");
        return false;
    }
    if (UU_UNLIKELY(!number_is_fzero(e))) {
        state_prepare_error(state, "fraction part of exponent is non-zero");
        return false;
    }
    *out = (Value) number_pow(b, e);
    return true;
}

// On success:
//   * returns 'true';
//   * steals 'left' and 'right';
//   * writes the result into '*out'.
// On failure:
//   * returns 'false';
//   * does NOT steal either 'left' or 'right';
//   * prepares the error message in 'state' (as if with 'state_prepare_error').
static bool perform_aop(State *state, uint8_t aop, Value left, Value right, Value *out)
{
    switch (aop) {

    case AOP_ADD:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_add(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_SUB:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_sub(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_MUL:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_mul(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_DIV:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        if (UU_UNLIKELY(number_is_zero((Number *) right)))
            goto div_by_zero;
        *out = (Value) number_div(
            (Number *) left,
            (Number *) right,
            state->ntp);
        return true;

    case AOP_POW:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        return do_pow(state, (Number *) left, (Number *) right, out);

    case AOP_IDIV:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        if (UU_UNLIKELY(number_is_izero((Number *) right)))
            goto div_by_zero;
        *out = (Value) number_idiv(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_MOD:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        if (UU_UNLIKELY(number_is_izero((Number *) right)))
            goto div_by_zero;
        *out = (Value) number_imod(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_CONCAT:
        if (left->kind == VK_STR) {
            String *s = (String *) left;
            s = append_string_repr(s, right);
            value_unref(right);
            *out = (Value) s;
            return true;
        } else {
            String *s = string_new(NULL, 0);
            s = append_string_repr(s, left);
            value_unref(left);
            s = append_string_repr(s, right);
            value_unref(right);
            *out = (Value) s;
            return true;
        }

    case AOP_AND:
        if (value_is_truthy(left)) {
            value_unref(left);
            *out = right;
        } else {
            value_unref(right);
            *out = left;
        }
        return true;

    case AOP_OR:
        if (value_is_truthy(left)) {
            value_unref(right);
            *out = left;
        } else {
            value_unref(left);
            *out = right;
        }
        return true;

    case AOP_BIT_AND:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_bit_and(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_BIT_OR:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_bit_or(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_BIT_XOR:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_bit_xor(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_LSHIFT:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_bit_shl(
            (Number *) left,
            (Number *) right);
        return true;

    case AOP_RSHIFT:
        if (UU_UNLIKELY(left->kind != VK_NUM || right->kind != VK_NUM))
            goto kind_err;
        *out = (Value) number_bit_lshr(
            (Number *) left,
            (Number *) right);
        return true;

    default:
        __builtin_unreachable();
    }

kind_err:
    (void) 0;
    static const char *oprepr[] = {
        [AOP_AND] = "&&",
        [AOP_BIT_AND] = "&",
        [AOP_SUB] = "-",
        [AOP_OR] = "||",
        [AOP_BIT_OR] = "|",
        [AOP_BIT_XOR] = "^",
        [AOP_LSHIFT] = "<<",
        [AOP_RSHIFT] = ">>",
        [AOP_MOD] = "%",
        [AOP_ADD] = "+",
        [AOP_DIV] = "/",
        [AOP_IDIV] = "//",
        [AOP_MUL] = "*",
        [AOP_POW] = "**",
        [AOP_CONCAT] = "~",
    };
    state_prepare_error(
        state, "attempt to compute %s %s %s",
        value_kind_name(left->kind),
        oprepr[aop],
        value_kind_name(right->kind));
    return false;

div_by_zero:
    state_prepare_error(state, "division by zero");
    return false;
}

// On success:
//   * returns 'true';
//   * steals 'v';
//   * writes the result into '*out'.
// On failure:
//   * returns 'false';
//   * does NOT steal 'v';
//   * prepares the error message in 'state' (as if with 'state_prepare_error').
static bool perform_neg(State *state, Value v, Value *out)
{
    if (UU_UNLIKELY(v->kind != VK_NUM)) {
        state_prepare_error(state, "attempt to negate %s", value_kind_name(v->kind));
        return false;
    }

    *out = (Value) number_negate((Number *) v);
    return true;
}

// On success:
//   * returns 'true';
//   * steals 'v';
//   * writes the result into '*out'.
// On failure:
//   * returns 'false';
//   * does NOT steal 'v';
//   * prepares the error message in 'state' (as if with 'state_prepare_error').
static bool perform_len(State *state, Value v, Value *out)
{
    switch (v->kind) {
    case VK_LIST:
        {
            List *list = (List *) v;
            *out = (Value) number_new_from_zu(list->size);
            value_unref(v);
            return true;
        }
        break;

    case VK_DICT:
        {
            Dict *dict = (Dict *) v;
            *out = (Value) number_new_from_zu(xht_size(&dict->xht));
            value_unref(v);
            return true;
        }
        break;

    case VK_STR:
        {
            String *s = (String *) v;
            *out = (Value) number_new_from_zu(s->size);
            value_unref(v);
            return true;
        }
        break;

    default:
        state_prepare_error(
            state, "attempt to compute length of %s",
            value_kind_name(v->kind));
        return false;
    }
}

// On success:
//   * returns 'true';
//   * steals 'c' and 'i';
//   * writes the result into '*out'.
// On failure:
//   * returns 'false';
//   * does NOT steal either 'c' or 'i';
//   * prepares the error message in 'state' (as if with 'state_prepare_error').
static bool get_elem_at(State *state, Value c, Value i, Value *out)
{
    switch (c->kind) {
    case VK_LIST:
        {
            if (UU_UNLIKELY(i->kind != VK_NUM)) {
                state_prepare_error(
                    state, "attempt to index list with %s (expected number)",
                    value_kind_name(i->kind));
                return false;
            }
            size_t idx = number_to_zu((Number *) i);

            List *list = (List *) c;
            if (idx >= list->size) {
                *out = mk_nil();
            } else {
                Value r = list->data[idx];
                value_ref(r);
                *out = r;
            }
            value_unref(c);
            value_unref(i);
            return true;
        }
        break;

    case VK_DICT:
        {
            if (UU_UNLIKELY(i->kind != VK_STR)) {
                state_prepare_error(
                    state, "attempt to index dict with %s (expected string)",
                    value_kind_name(i->kind));
                return false;
            }
            MaybeValue r = dict_get((Dict *) c, (String *) i);
            if (r)
                *out = r;
            else
                *out = mk_nil();
            value_unref(c);
            value_unref(i);
            return true;
        }
        break;

    case VK_STR:
        {
            if (UU_UNLIKELY(i->kind != VK_NUM)) {
                state_prepare_error(
                    state, "attempt to index string with %s (expected number)",
                    value_kind_name(i->kind));
                return false;
            }
            size_t idx = number_to_zu((Number *) i);

            String *s = (String *) c;
            if (idx >= s->size)
                *out = mk_nil();
            else
                *out = (Value) string_new(s->data + idx, 1);
            value_unref(c);
            value_unref(i);
            return true;
        }
        break;

    default:
        state_prepare_error(
            state, "attempt to index %s value",
            value_kind_name(c->kind));
        return false;
    }
}

// On success:
//   * returns non-null;
//   * steals 'c' and 'i'.
// On failure:
//   * returns null;
//   * does NOT steal either 'c' or 'i';
//   * prepares the error message in 'state' (as if with 'state_prepare_error').
static Value *get_elem_ptr_at(State *state, Value c, Value i)
{
    switch (c->kind) {
    case VK_LIST:
        {
            if (UU_UNLIKELY(i->kind != VK_NUM)) {
                state_prepare_error(
                    state, "attempt to index list with %s (expected number)",
                    value_kind_name(i->kind));
                return NULL;
            }
            size_t idx = number_to_zu((Number *) i);

            List *list = (List *) c;
            size_t nlist = list->size;

            if (idx == nlist) {
                list_append_steal(list, mk_nil());
            } else if (UU_UNLIKELY(idx > nlist)) {
                state_prepare_error(state, "attempt to insert past the end of the list");
                return NULL;
            }
            return &list->data[idx];
        }
        break;

    case VK_DICT:
        {
            if (UU_UNLIKELY(i->kind != VK_STR)) {
                state_prepare_error(
                    state, "attempt to index dict with %s (expected string)",
                    value_kind_name(i->kind));
                return NULL;
            }
            return dict_get_ptr((Dict *) c, (String *) i);
        }
        break;

    case VK_STR:
        state_prepare_error(state, "strings are immutable");
        return NULL;

    default:
        state_prepare_error(
            state, "attempt to index %s value",
            value_kind_name(c->kind));
        return NULL;
    }
}

static Quark *find_quark(size_t instr, Chunk *chunk)
{
    Quark *left = chunk->quarks;
    Quark *right = left + chunk->nquarks;
    while (left != right) {
        Quark *mid = left + (right - left) / 2;
        if (instr >= mid->instr)
            left = mid + 1;
        else
            right = mid;
    }
    assert(left != chunk->quarks);
    return left - 1;
}

static void print_traceback_line(Instr *ip, Chunk *chunk)
{
    size_t instr = ip - chunk->code;
    Quark *quark = find_quark(instr, chunk);
    fprintf(stderr, ">>> at %s:%zu:\n", chunk->origin, quark->line);
    text_show_line(stderr, chunk->source, chunk->nsource, /*lineno=*/quark->line - 1);
}

void state_print_traceback(State *state)
{
    fprintf(stderr, "Stack trace (most recent first):\n");

    Instr *ip = state->pad->rti.ip;
    CallStack call_stack = state->pad->rti.cs;

    CallSite *top = call_stack.top;
    while (top != call_stack.begin) {
        --top;
        print_traceback_line(ip, top->callee->chunk);
        ip = top->call_ip;
    }
}

// Modifies (steals the previous-value-at-the-pointer, alters the value-at-the-pointer):
//   * 'where'.
// Steals (takes move references to):
//   * 'value'.
static inline UU_ALWAYS_INLINE void store(Value *where, Value value)
{
    value_unref(*where);
    *where = value;
}

// Modifies (steals the previous-value-at-the-pointer, alters the value-at-the-pointer):
//   * 'where'.
// Steals (takes move references to):
//   * 'value'.
static inline UU_ALWAYS_INLINE void checked_store(MaybeValue *where, Value value)
{
    maybe_value_unref(*where);
    *where = value;
}

// On success:
//   * returns 'true';
//   * steals 'func'.
// On failure:
//   * returns 'false';
//   * does NOT steal 'func';
//   * prepares the error message in 'pad->err_msg'.
static __attribute__((noinline))
bool setup_for_eval(ScratchPad *pad, Func *func)
{
    Shape shape = func_shape(func);

    if (UU_UNLIKELY(shape.nargs_encoded != 0)) {
        pad->err_msg = uu_xstrdup("You are not supposed to call functions with args this way! >_~");
        return false;
    }

    value_stack_ensure(&pad->rti.vs, shape.maxstack + shape.nlocals + 1);
    // Push the function itself onto the value stack.
    // No need to do 'value_ref((Value) func)' -- it is "stolen" by us.
    *(pad->rti.vs.top)++ = (Value) func;
    // Push the function locals onto the value stack.
    pad->rti.vs.top = pushn(pad->rti.vs.top, shape.nlocals);

    // Push the initial entry onto the call stack.

    call_stack_push(&pad->rti.cs, (CallSite) {
        .call_ip = NULL,
        .callee = func,
        .prev_consts = NULL,
        .prev_locals_offset = -1,
    });

    return true;
}

static __attribute__((noinline))
void missing_global(State *state, uint32_t idx)
{
    size_t nname;
    const char *name = xht_indexed_key(&state->globals_table, idx, &nname);
    if (nname > 8192)
        nname = 8192;
    state_prepare_error(state, "undefined global '%.*s'", (int) nname, name);
}

// Borrows (takes regular references to):
//   * 'kv[0]' ... 'kv_end[-1]'.
static __attribute__((noinline))
bool check_dict_keys(State *state, Value *kv, Value *kv_end)
{
    for (; kv != kv_end; kv += 2) {
        Value k = kv[0];
        if (UU_UNLIKELY(k->kind != VK_STR)) {
            state_prepare_error(
                state, "attempt to create dict with %s key (expected string)",
                value_kind_name(k->kind));
            return false;
        }
    }
    return true;
}

void state_steal_global(State *state, const char *name, size_t nname, Value value)
{
    uint32_t idx = state_intern_global(state, name, nname);
    checked_store(&state->globals.data[idx], value);
}

NumberTruncateParams state_get_ntp(State *s)
{
    return s->ntp;
}

void state_set_ntp(State *s, NumberTruncateParams ntp)
{
    s->ntp = ntp;
}

static inline void value_stack_push_copies(ValueStack *vs, Value *src, size_t nsrc)
{
    value_stack_ensure(vs, nsrc);
    Value *dst = vs->top;
    for (size_t i = 0; i < nsrc; ++i) {
        Value v = src[i];
        value_ref(v);
        dst[i] = v;
    }
    vs->top += nsrc;
}

static size_t do_call_scatter(State *state, ScratchPad *pad)
{
    Value *vs_top = pad->rti.vs.top;
    Value v = vs_top[-1];
    if (UU_UNLIKELY(v->kind != VK_LIST)) {
        state_throw(state, "cannot scatter %s value (expected list)", value_kind_name(v->kind));
    }

    pad->rti.vs.top = vs_top - 1;

    List *list = (List *) v;
    size_t n = list->size;
    value_stack_push_copies(&pad->rti.vs, list->data, n);

    value_unref(v);

    return n;
}

static void do_call_gather(ScratchPad *pad, size_t n)
{
    if (!n) {
        value_stack_ensure(&pad->rti.vs, 1);
    }
    Value *vs_top = pad->rti.vs.top;
    List *list = list_new_steal(vs_top - n, n);
    vs_top -= n;
    *vs_top++ = (Value) list;
    pad->rti.vs.top = vs_top;
}

static size_t do_call_coerce(State *state, ScratchPad *pad, uint32_t nargs_encoded, size_t nargs)
{
    if (((int32_t) nargs_encoded) >= 0) {
        // no gather arg
        if (UU_UNLIKELY(nargs != nargs_encoded)) {
            state_throw(
                state, "wrong number of arguments: expected %zu, got %zu",
                (size_t) nargs_encoded, nargs);
        }
        return nargs;

    } else {
        // with gather arg
        nargs_encoded = ~nargs_encoded;
        if (UU_UNLIKELY(nargs < nargs_encoded)) {
            state_throw(
                state, "wrong number of arguments: expected at least %zu, got %zu",
                (size_t) nargs_encoded, nargs);
        }
        do_call_gather(pad, nargs - nargs_encoded);
        return ((size_t) nargs_encoded) + 1;
    }
}

static void do_call(State *state, ScratchPad *pad, bool scatter, size_t nargs)
{
    if (scatter) {
        nargs += do_call_scatter(state, pad) - 1;
    }

    Value *vs_top = pad->rti.vs.top;

    Value v = *(vs_top - nargs - 1);

    if (v->kind == VK_FUNC) {
        Func *func = (Func *) v;
        Shape shape = func_shape(func);

        nargs = do_call_coerce(state, pad, shape.nargs_encoded, nargs);

        size_t cur_locals_offset = pad->rti.locals - pad->rti.vs.begin;

        uint32_t nvars = shape.nlocals - nargs;
        value_stack_ensure(&pad->rti.vs, shape.maxstack + nvars);
        vs_top = pad->rti.vs.top;

        call_stack_push(&pad->rti.cs, (CallSite) {
            .call_ip = pad->rti.ip,
            .callee = func,
            .prev_consts = pad->rti.consts,
            .prev_locals_offset = cur_locals_offset,
        });
        vs_top = pushn(vs_top, nvars);
        pad->rti.vs.top = vs_top;
        pad->rti.consts = func->chunk->consts;
        pad->rti.locals = vs_top - shape.nlocals;
        pad->rti.ip = func->ip;

    } else if (v->kind == VK_CFUNC) {
        Value r = ((NativeFunc *) v)->func(state, vs_top - nargs, nargs);
        vs_top = popn(vs_top, nargs + 1);
        *vs_top++ = r;
        pad->rti.vs.top = vs_top;

    } else {
        state_throw(state, "attempt to call %s value", value_kind_name(v->kind));
    }
}

MaybeValue state_eval(State *state, Func *callee)
{
    state->pad = scratch_pad_new(state->pad);

    if (UU_UNLIKELY(!setup_for_eval(state->pad, callee))) {
        fprintf(stderr, "Runtime setup error: %s\n", state->pad->err_msg);
        value_unref((Value) callee);
        state->pad = scratch_pad_free(state->pad);
        return NULL;
    }

    if (setjmp(state->pad->err_handler) != 0) {
        // we have jumped here...
        fprintf(stderr, "Runtime error: %s\n", state->pad->err_msg);
        state_print_traceback(state);
        state->pad = scratch_pad_free(state->pad);
        return NULL;
    }

    static void *jumptbl[] = {
#define J(X) [X] = &&case_ ## X
        J(OP_LOAD_CONST),
        J(OP_LOAD_LOCAL),
        J(OP_LOAD_GLOBAL),
        J(OP_LOAD_AT),
        J(OP_STORE_LOCAL),
        J(OP_STORE_GLOBAL),
        J(OP_STORE_AT),
        J(OP_MODIFY_LOCAL),
        J(OP_MODIFY_GLOBAL),
        J(OP_MODIFY_AT),
        J(OP_PRINT),
        J(OP_RETURN),
        J(OP_JUMP),
        J(OP_JUMP_UNLESS),
        J(OP_CALL),
        J(OP_FUNCTION),
        J(OP_AOP),
        J(OP_CMP_2WAY),
        J(OP_CMP_3WAY),
        J(OP_NOT),
        J(OP_NEG),
        J(OP_DICT),
        J(OP_LIST),
        J(OP_LEN),
#undef J
    };

    ScratchPad *pad = state->pad;
    Instr *ip = callee->ip + 1;
    Value *locals = pad->rti.vs.begin + 1;
    Value *consts = callee->chunk->consts;
    Value *vs_top = pad->rti.vs.top;

    Instr instr;

#define FLUSH() \
    do { \
        pad->rti.ip     = ip;     \
        pad->rti.locals = locals; \
        pad->rti.consts = consts; \
        pad->rti.vs.top = vs_top; \
    } while (0)

#define UNFLUSH() \
    do { \
        ip     = pad->rti.ip;     \
        locals = pad->rti.locals; \
        consts = pad->rti.consts; \
        vs_top = pad->rti.vs.top; \
    } while (0)

#define DISPATCH() goto *jumptbl[(instr = *ip).opcode]
#define CASE(X) case_ ## X:

    DISPATCH();

    CASE(OP_LOAD_CONST) {
        Value v = consts[instr.c];
        value_ref(v);
        *vs_top++ = v;
        ++ip;
    } DISPATCH();

    CASE(OP_LOAD_LOCAL) {
        Value v = locals[instr.c];
        value_ref(v);
        *vs_top++ = v;
        ++ip;
    } DISPATCH();

    CASE(OP_LOAD_GLOBAL) {
        MaybeValue v = state->globals.data[instr.c];
        if (UU_UNLIKELY(!v)) {
            missing_global(state, instr.c);
            goto flush_and_throw;
        }
        value_ref(v);
        *vs_top++ = v;
        ++ip;
    } DISPATCH();

    CASE(OP_LOAD_AT) {
        Value i = *--vs_top;
        Value c = *--vs_top;
        if (UU_UNLIKELY(!get_elem_at(state, c, i, /*out=*/vs_top))) {
            value_unref(i);
            value_unref(c);
            goto flush_and_throw;
        }
        ++vs_top;
        ++ip;
    } DISPATCH();

    CASE(OP_AOP) {
        Value v = *--vs_top;
        Value w = *--vs_top;
        if (UU_UNLIKELY(!perform_aop(state, instr.a, w, v, /*out=*/vs_top))) {
            value_unref(v);
            value_unref(w);
            goto flush_and_throw;
        }
        ++vs_top;
        ++ip;
    } DISPATCH();

    CASE(OP_CMP_2WAY) {
        Value v = *--vs_top;
        Value w = *--vs_top;
        *vs_top++ = mk_flag(values_equal(w, v) == (instr.a != 0));
        value_unref(v);
        value_unref(w);
        ++ip;
    } DISPATCH();

    CASE(OP_CMP_3WAY) {
        Value v = *--vs_top;
        Value w = *--vs_top;
        if (UU_UNLIKELY(!perform_cmp_3way(state, w, v, /*a=*/instr.a, /*out=*/vs_top))) {
            value_unref(v);
            value_unref(w);
            goto flush_and_throw;
        }
        ++vs_top;
        ++ip;
    } DISPATCH();

    CASE(OP_JUMP) {
        ip += (int32_t) instr.c;
    } DISPATCH();

    CASE(OP_JUMP_UNLESS) {
        Value v = *--vs_top;
        if (value_is_truthy(v))
            ++ip;
        else
            ip += (int32_t) instr.c;
        value_unref(v);
    } DISPATCH();

    CASE(OP_RETURN) {
        CallSite call_site = *--pad->rti.cs.top;

        consts = call_site.prev_consts;
        locals = pad->rti.vs.begin + call_site.prev_locals_offset;

        Value v = *--vs_top;
        vs_top = popn(vs_top, func_shape(call_site.callee).nlocals + 1);
        *vs_top++ = v;

        ip = call_site.call_ip;
        if (UU_UNLIKELY(!ip))
            goto done;
        ++ip;
    } DISPATCH();

    CASE(OP_CALL) {
        FLUSH();
        do_call(state, pad, instr.a, instr.c);
        UNFLUSH();
        ++ip;
    } DISPATCH();

    CASE(OP_NOT) {
        Value v = *--vs_top;
        *vs_top++ = mk_flag(!value_is_truthy(v));
        value_unref(v);
        ++ip;
    } DISPATCH();

    CASE(OP_LEN) {
        Value v = *--vs_top;
        if (UU_UNLIKELY(!perform_len(state, v, /*out=*/vs_top))) {
            value_unref(v);
            goto flush_and_throw;
        }
        ++vs_top;
        ++ip;
    } DISPATCH();

    CASE(OP_STORE_LOCAL) {
        store(&locals[instr.c], *--vs_top);
        ++ip;
    } DISPATCH();

    CASE(OP_STORE_GLOBAL) {
        checked_store(&state->globals.data[instr.c], *--vs_top);
        ++ip;
    } DISPATCH();

    CASE(OP_STORE_AT) {
        Value v = *--vs_top;
        Value i = *--vs_top;
        Value c = *--vs_top;
        Value *where = get_elem_ptr_at(state, c, i);
        if (UU_UNLIKELY(!where)) {
            value_unref(v);
            value_unref(i);
            value_unref(c);
            goto flush_and_throw;
        }
        store(where, v);
        value_unref(i);
        value_unref(c);
        ++ip;
    } DISPATCH();

    CASE(OP_MODIFY_LOCAL) {
        Value *where = &locals[instr.c];
        Value v = *--vs_top;
        if (UU_UNLIKELY(!perform_aop(state, instr.a, *where, v, /*out=*/where))) {
            value_unref(v);
            goto flush_and_throw;
        }
        ++ip;
    } DISPATCH();

    CASE(OP_MODIFY_GLOBAL) {
        MaybeValue *where = &state->globals.data[instr.c];
        if (UU_UNLIKELY(!*where)) {
            missing_global(state, instr.c);
            goto flush_and_throw;
        }
        Value v = *--vs_top;
        if (UU_UNLIKELY(!perform_aop(state, instr.a, *where, v, /*out=*/where))) {
            value_unref(v);
            goto flush_and_throw;
        }
        ++ip;
    } DISPATCH();

    CASE(OP_MODIFY_AT) {
        Value v = *--vs_top;
        Value i = *--vs_top;
        Value c = *--vs_top;
        Value *where = get_elem_ptr_at(state, c, i);
        if (UU_UNLIKELY(!where)) {
            value_unref(v);
            value_unref(i);
            value_unref(c);
            goto flush_and_throw;
        }
        if (UU_UNLIKELY(!perform_aop(state, instr.a, *where, v, /*out=*/where))) {
            value_unref(v);
            value_unref(i);
            value_unref(c);
            goto flush_and_throw;
        }
        value_unref(i);
        value_unref(c);
        ++ip;
    } DISPATCH();

    CASE(OP_PRINT) {
        Value v = *--vs_top;
        value_print(v);
        value_unref(v);
        ++ip;
    } DISPATCH();

    CASE(OP_FUNCTION) {
        Chunk *cur = pad->rti.cs.top[-1].callee->chunk;
        *vs_top++ = mk_func(cur, ip);
        ip += cur->shapes[instr.c].offset;
    } DISPATCH();

    CASE(OP_NEG) {
        Value v = *--vs_top;
        if (UU_UNLIKELY(!perform_neg(state, v, /*out=*/vs_top))) {
            value_unref(v);
            goto flush_and_throw;
        }
        ++vs_top;
        ++ip;
    } DISPATCH();

    CASE(OP_DICT) {
        Value *kv_begin = vs_top - ((size_t) instr.c) * 2;
        if (UU_UNLIKELY(!check_dict_keys(state, kv_begin, vs_top)))
            goto flush_and_throw;
        Value v = (Value) dict_new_steal(kv_begin, vs_top);
        vs_top = kv_begin;
        *vs_top++ = v;
        ++ip;
    } DISPATCH();

    CASE(OP_LIST) {
        uint32_t n = instr.c;
        Value *list_begin = vs_top - n;
        Value v = (Value) list_new_steal(list_begin, n);
        vs_top = list_begin;
        *vs_top++ = v;
        ++ip;
    } DISPATCH();

done:
    FLUSH();
    assert(pad->rti.vs.top - pad->rti.vs.begin == 1);
    Value r = *--pad->rti.vs.top;
    state->pad = scratch_pad_free(state->pad);
    return r;

flush_and_throw:
    FLUSH();
    state_throw_prepared_error(state);

#undef DISPATCH
#undef CASE
#undef FLUSH
#undef UNFLUSH
}
