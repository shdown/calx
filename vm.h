// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"

#include "compare.h"
#include "ntp.h"

enum {
    VK_NUM,
    VK_FLAG,
    VK_STR,
    VK_NIL,
    VK_LIST,
    VK_DICT,
    VK_FUNC,
    VK_CFUNC,
    VK_WREF,
};

typedef struct {
    size_t nrefs;
    char kind;
} GcHeader;

enum {
    VALUE_CACHE_NIL,
    VALUE_CACHE_FALSE,
    VALUE_CACHE_TRUE,
};

extern GcHeader value_cache[];

typedef GcHeader *Value;        // non-nullable
typedef GcHeader *MaybeValue;   // nullable

const char *value_kind_name(char kind);
const char *value_kind_name_long(char kind);

typedef struct {
    uint8_t opcode;
    uint8_t a;
    uint16_t b;
    uint32_t c;
} Instr;

typedef struct {
    size_t instr;
    size_t line;
} Quark;

typedef struct {
    uint32_t nargs_encoded;
    uint32_t nlocals; // <= UINT32_MAX/2
    size_t offset;
    size_t maxstack; // <= SIZE_MAX/2
} Shape;

typedef struct {
    Instr *code;
    size_t ncode;

    Value *consts; // owning reference
    size_t nconsts;

    Quark *quarks;
    size_t nquarks;

    Shape *shapes;
    size_t nshapes;

    char *origin;

    char *source;
    size_t nsource;

    size_t nrefs;
} Chunk;

typedef struct {
    GcHeader gc_hdr;
    Chunk *chunk;
    Instr *ip;
} Func;

// Steals (takes move references to):
//   * 'code' (should be allocated as if with 'malloc()');
//   * 'consts' as a pointer (should be allocated as if with 'malloc()');
//   * 'consts[0]' ... 'consts[nconsts - 1]' as values;
//   * 'quarks' (should be allocated as if with 'malloc()');
//   * 'shapes' (should be allocated as if with 'malloc()');
//   * 'origin' (should be allocated as if with 'malloc()');
//   * 'source' (should be allocated as if with 'malloc()').
Chunk *chunk_new_steal(
    Instr *code,
    size_t ncode,
    Value *consts,
    size_t nconsts,
    Quark *quarks,
    size_t nquarks,
    Shape *shapes,
    size_t nshapes,
    char *origin,
    char *source,
    size_t nsource);

void chunk_destroy(Chunk *chunk);

UU_INHEADER void chunk_ref(Chunk *chunk)
{
    ++chunk->nrefs;
}

UU_INHEADER void chunk_unref(Chunk *chunk)
{
    if (!--chunk->nrefs)
        chunk_destroy(chunk);
}

UU_INHEADER bool value_kind_is_wrefable(char kind)
{
    switch (kind) {
    case VK_DICT:
    case VK_LIST:
        return true;
    default:
        return false;
    }
}

void value_free(Value value);

UU_INHEADER UU_ALWAYS_INLINE void value_ref(Value value)
{
    ++value->nrefs;
}

UU_INHEADER UU_ALWAYS_INLINE void value_unref(Value value)
{
    if (!--value->nrefs)
        value_free(value);
}

enum {
    OP_LOAD_CONST,
    OP_LOAD_LOCAL,
    OP_LOAD_AT,
    OP_LOAD_GLOBAL,

    OP_STORE_LOCAL,
    OP_STORE_AT,
    OP_STORE_GLOBAL,

    OP_MODIFY_LOCAL,
    OP_MODIFY_AT,
    OP_MODIFY_GLOBAL,

    OP_PRINT,
    OP_RETURN,

    OP_JUMP,
    OP_JUMP_UNLESS,
    OP_CALL,
    OP_FUNCTION,

    OP_NEG,
    OP_NOT,

    OP_AOP,

    OP_CMP_2WAY,
    OP_CMP_3WAY,

    OP_LIST,
    OP_DICT,
    OP_LEN,

    OP_LOAD_SYMBOLIC,
    OP_MODIFY_SYMBOLIC,
    OP_STORE_SYMBOLIC,
};

enum {
    AOP_AND,
    AOP_BIT_AND,
    AOP_SUB,
    AOP_OR,
    AOP_BIT_OR,
    AOP_BIT_XOR,
    AOP_LSHIFT,
    AOP_RSHIFT,
    AOP_MOD,
    AOP_ADD,
    AOP_DIV,
    AOP_IDIV,
    AOP_MUL,
    AOP_POW,
    AOP_CONCAT,
};

UU_INHEADER UU_ALWAYS_INLINE Value mk_nil(void)
{
    Value v = &value_cache[VALUE_CACHE_NIL];
    value_ref(v);
    return v;
}

UU_INHEADER UU_ALWAYS_INLINE Value mk_flag(bool value)
{
    Value v = &value_cache[value ? VALUE_CACHE_TRUE : VALUE_CACHE_FALSE];
    value_ref(v);
    return v;
}

UU_INHEADER UU_ALWAYS_INLINE void maybe_value_unref(MaybeValue v)
{
    if (v)
        value_unref(v);
}

UU_INHEADER Value mk_func(Chunk *chunk, Instr *ip)
{
    chunk_ref(chunk);
    Func *func = uu_xmalloc(1, sizeof(Func));
    *func = (Func) {
        .gc_hdr = {.nrefs = 1, .kind = VK_FUNC},
        .chunk = chunk,
        .ip = ip,
    };
    return (Value) func;
}

// Borrows (takes regular references to):
//   * 'f'.
UU_INHEADER UU_ALWAYS_INLINE Shape func_shape(Func *f)
{
    return f->chunk->shapes[f->ip->c];
}

struct State;
typedef struct State State;

// Borrows (takes regular references to):
//   * 'args[0]' ... 'args[nargs - 1]'.
typedef Value (*CFuncPtr)(State *s, Value *args, uint32_t nargs);

typedef struct {
    GcHeader gc_hdr;
    CFuncPtr func;
} NativeFunc;

UU_INHEADER Value mk_cfunc(CFuncPtr fptr)
{
    NativeFunc *obj = uu_xmalloc(1, sizeof(NativeFunc));
    *obj = (NativeFunc) {
        .gc_hdr = {.nrefs = 1, .kind = VK_CFUNC},
        .func = fptr,
    };
    return (Value) obj;
}

State *state_new(void);

NumberTruncateParams state_get_ntp(State *s);

void state_set_ntp(State *s, NumberTruncateParams ntp);

__attribute__((noreturn, format(printf, 2, 3)))
void state_throw(State *s, const char *fmt, ...);

uint32_t state_intern_global(State *s, const char *name, size_t nname);

// Steals (takes move references to):
//     * 'value'.
void state_steal_global(State *s, const char *name, size_t nname, Value value);

// Steals (takes move references to):
//     * 'f'.
MaybeValue state_eval(State *s, Func *f);

void state_print_traceback(State *s);

void state_destroy(State *s);
