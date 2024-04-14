// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "parse.h"

#include "lexer.h"
#include "dasm.h"
#include "xht.h"
#include "hash.h"
#include "number.h"
#include "str.h"

#define SWAP(X_, Y_) \
    do { \
        __typeof__(X_) swap_tmp_ = (X_); \
        (X_) = (Y_); \
        (Y_) = swap_tmp_; \
    } while (0)

// Stacks
enum {
    S_IF,
    S_BREAK,
    S_CONTINUE,
    S__LAST,
};

typedef struct {
    Instr instr;
    size_t line;
} TaggedInstr;

typedef struct {
    TaggedInstr *data;
    size_t size;
    size_t capacity;
} Program;

typedef struct {
    size_t *data;
    size_t size;
    size_t capacity;
} Stack;

static inline void stack_push(Stack *x, size_t v)
{
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(size_t));
    }
    x->data[x->size++] = v;
}

static inline size_t stack_peek(Stack *x)
{
    return x->data[x->size - 1];
}

static inline size_t stack_pop(Stack *x)
{
    return x->data[--x->size];
}

typedef struct {
    size_t scope_index;
    size_t begin;
    size_t end;
} Range;

typedef struct {
    Range *data;
    size_t size;
    size_t capacity;
} RangeStack;

typedef struct {
    xHt *data;
    size_t size;
    size_t capacity;
} ScopeStack;

typedef struct {
    Value *data;
    size_t size;
    size_t capacity;
} ConstList;

typedef struct {
    Shape *data;
    size_t size;
    size_t capacity;
} ShapeList;

typedef struct {
    const char *start;
    size_t size;
} Ident;

typedef struct {
    Ident *data;
    size_t size;
    size_t capacity;
} IdentList;

typedef struct {
    Lexer *lexer;
    State *state;
    Lexeme cur;
    Program prog;
    ConstList consts;
    ShapeList shapes;
    ScopeStack scopes;
    RangeStack ranges;
    IdentList idents;

    Stack stacks[S__LAST];
    ParseError err;
    jmp_buf err_handler;
} Parser;

static Parser *parser_new(const char *source, size_t nsource, State *state)
{
    Parser *p = uu_xmalloc(sizeof(Parser), 1);
    *p = (Parser) {
        .lexer = lexer_new(source, nsource),
        .state = state,
        .prog = {NULL, 0, 0},
        .consts = {NULL, 0, 0},
        .shapes = {NULL, 0, 0},
        .scopes = {NULL, 0, 0},
        .ranges = {NULL, 0, 0},
        .idents = {NULL, 0, 0},
        .stacks = {[0 ... S__LAST - 1] = {NULL, 0, 0}},
        .err = {.msg = NULL},
    };
    return p;
}

static void parser_destroy(Parser *p)
{
    lexer_destroy(p->lexer);

    free(p->prog.data);

    for (size_t i = 0; i < p->consts.size; ++i)
        value_unref(p->consts.data[i]);
    free(p->consts.data);

    free(p->shapes.data);

    for (size_t i = 0; i < p->scopes.size; ++i)
        xht_destroy(&p->scopes.data[i]);
    free(p->scopes.data);

    free(p->ranges.data);

    free(p->idents.data);

    for (int i = 0; i < S__LAST; ++i)
        free(p->stacks[i].data);

    free(p->err.msg);

    free(p);
}

static __attribute__((noreturn))
void throw_error(Parser *p, const char *msg)
{
    p->err = (ParseError) {
        .size = -1,
        .msg = uu_xstrdup(msg),
        .need_more = false,
    };
    longjmp(p->err_handler, 1);
}

static __attribute__((noreturn))
void throw_error_at(Parser *p, const char *msg, Lexeme at)
{
    p->err = (ParseError) {
        .pos = at.pos,
        .size = at.size,
        .msg = uu_xstrdup(msg),
        .need_more = (at.kind == LK_EOF),
    };
    longjmp(p->err_handler, 1);
}

static __attribute__((noreturn))
void throw_error_precise(Parser *p, const char *msg, Position pos, size_t size)
{
    p->err = (ParseError) {
        .pos = pos,
        .size = size,
        .msg = uu_xstrdup(msg),
        .need_more = false,
    };
    longjmp(p->err_handler, 1);
}

static uint32_t add_shape(Parser *p)
{
    ShapeList *x = &p->shapes;
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(Shape));
    }
    uint32_t i = x->size++;
    if (UU_UNLIKELY(i == UINT32_MAX)) {
        throw_error(p, "too many functions");
    }
    x->data[i] = (Shape) {0};
    return i;
}

static uint32_t add_const_steal(Parser *p, Value v)
{
    ConstList *x = &p->consts;
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(Value));
    }
    uint32_t i = x->size++;
    if (UU_UNLIKELY(i == UINT32_MAX)) {
        throw_error(p, "too many constants");
    }
    x->data[i] = v;
    return i;
}

static inline void push_range(Parser *p, Range v)
{
    RangeStack *x = &p->ranges;
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(Range));
    }
    x->data[x->size++] = v;
}

static inline void emit_at_line(Parser *p, Instr instr, size_t line)
{
    Program *x = &p->prog;
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(TaggedInstr));
    }
    x->data[x->size++] = (TaggedInstr) {instr, line};
}

static inline Instr unemit(Parser *p)
{
    Program *x = &p->prog;
    TaggedInstr ti = x->data[--x->size];
    return ti.instr;
}

static inline void emit(Parser *p, Instr instr)
{
    emit_at_line(p, instr, -1);
}

static inline void emit_at(Parser *p, Instr instr, Lexeme at)
{
    emit_at_line(p, instr, at.pos.line);
}

static inline void advance(Parser *p)
{
    p->cur = lexer_next(p->lexer);
    if (UU_UNLIKELY(p->cur.kind == LK_ERROR))
        throw_error_at(p, lexer_error_msg(p->lexer), p->cur);
}

static inline void slurp(Parser *p, LexemeKind k, const char *err_msg)
{
    if (UU_UNLIKELY(p->cur.kind != k))
        throw_error_at(p, err_msg, p->cur);
    advance(p);
}

static inline size_t here(Parser *p)
{
    return p->prog.size;
}

static inline void fixup_last_range_end(Parser *p, size_t new_end)
{
    Range *range = &p->ranges.data[p->ranges.size - 1];
    range->end = new_end;
}

static inline Instr load(Parser *p, Lexeme ident)
{
    IdentList *x = &p->idents;
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(Ident));
    }
    uint32_t i = x->size++;
    if (UU_UNLIKELY(i == UINT32_MAX)) {
        throw_error(p, "too many identifiers");
    }
    x->data[i] = (Ident) {ident.start, ident.size};
    return (Instr) {OP_LOAD_SYMBOLIC, 0, 0, i};
}

static Instr load_to_store(Parser *p, Instr instr, bool local, Lexeme scapegoat)
{
    switch (instr.opcode) {
    case OP_LOAD_SYMBOLIC:
        if (local) {
            Ident ident = p->idents.data[instr.c];
            xHt *locals = &p->scopes.data[p->scopes.size - 1];
            uint32_t idx = *xht_put_int(
                locals,
                ident.start, ident.size, hash_str(ident.start, ident.size),
                xht_size(locals));
            return (Instr) {OP_STORE_LOCAL, 0, 0, idx};
        } else {
            return (Instr) {OP_STORE_SYMBOLIC, 0, 0, instr.c};
        }
    case OP_LOAD_AT:
        if (UU_UNLIKELY(local))
            goto error;
        return (Instr) {OP_STORE_AT, 0, 0, 0};
    default:
        goto error;
    }
error:
    throw_error_at(p, "invalid assignment", scapegoat);
}

static Instr load_to_modify(Parser *p, Instr instr, uint8_t aop, Lexeme scapegoat)
{
    switch (instr.opcode) {
    case OP_LOAD_SYMBOLIC:
        return (Instr) {OP_MODIFY_SYMBOLIC, aop, 0, instr.c};
    case OP_LOAD_AT:
        return (Instr) {OP_MODIFY_AT, aop, 0, 0};
    default:
        throw_error_at(p, "invalid compound assignment", scapegoat);
    }
}

static void open_scope(Parser *p)
{
    size_t pos = here(p);

    if (p->ranges.size)
        fixup_last_range_end(p, pos);

    ScopeStack *x = &p->scopes;
    if (x->size == x->capacity) {
        x->data = uu_x2realloc(x->data, &x->capacity, sizeof(xHt));
    }
    size_t i = x->size++;
    x->data[i] = xht_new(0);

    Range range = {i, pos, -1};
    push_range(p, range);
}

static inline Instr resolve_symbolic(Parser *p, xHt *locals, Instr instr, uint8_t op_local, uint8_t op_global)
{
    Ident ident = p->idents.data[instr.c];
    uint32_t local_idx = xht_get_int(
        locals,
        ident.start, ident.size, hash_str(ident.start, ident.size),
        -1);
    if (local_idx != (uint32_t) -1) {
        return (Instr) {op_local, instr.a, 0, local_idx};
    }

    uint32_t global_idx = state_intern_global(p->state, ident.start, ident.size);
    return (Instr) {op_global, instr.a, 0, global_idx};
}

static inline size_t nranges_for_scope_index(RangeStack *x, size_t scope_index)
{
    size_t n = x->size;
    while (n && x->data[n - 1].scope_index == scope_index)
        --n;
    return x->size - n;
}

static void close_scope(Parser *p, size_t *out_maxstack, uint32_t *out_nlocals)
{
    size_t pos = here(p);
    fixup_last_range_end(p, pos);

    size_t scope_idx = p->scopes.size - 1;
    xHt *locals = &p->scopes.data[scope_idx];

    static const int8_t actions[] = {
        [OP_LOAD_CONST] = 1,
        [OP_LOAD_LOCAL] = 1,
        [OP_LOAD_AT] = -1,
        [OP_LOAD_GLOBAL] = 1,

        [OP_MODIFY_LOCAL] = -1,
        [OP_MODIFY_AT] = -3,
        [OP_MODIFY_GLOBAL] = -1,

        [OP_STORE_LOCAL] = -1,
        [OP_STORE_AT] = -3,
        [OP_STORE_GLOBAL] = -1,

        [OP_PRINT] = -1,
        [OP_RETURN] = -1,

        [OP_JUMP] = 0,
        [OP_JUMP_UNLESS] = -1,
        [OP_CALL] = 0,
        [OP_FUNCTION] = 1,

        [OP_NEG] = 0,
        [OP_NOT] = 0,

        [OP_AOP] = -1,
        [OP_CMP_2WAY] = -1,
        [OP_CMP_3WAY] = -1,

        [OP_LIST] = 1,
        [OP_DICT] = 1,
        [OP_LEN] = 0,

        [OP_LOAD_SYMBOLIC] = 1,
        [OP_MODIFY_SYMBOLIC] = -1,
        [OP_STORE_SYMBOLIC] = -1,
    };

    size_t curstack = 0;
    size_t maxstack = 0;

    TaggedInstr *code = p->prog.data;

    size_t nranges_ours = nranges_for_scope_index(&p->ranges, scope_idx);
    for (size_t i = p->ranges.size - nranges_ours; i < p->ranges.size; ++i) {
        Range range = p->ranges.data[i];
        for (size_t j = range.begin; j < range.end; ++j) {
            Instr instr = code[j].instr;
            curstack += actions[instr.opcode];

            switch (instr.opcode) {
            case OP_LOAD_SYMBOLIC:
                code[j].instr = resolve_symbolic(p, locals, instr, OP_LOAD_LOCAL, OP_LOAD_GLOBAL);
                break;

            case OP_STORE_SYMBOLIC:
                code[j].instr = resolve_symbolic(p, locals, instr, OP_STORE_LOCAL, OP_STORE_GLOBAL);
                break;

            case OP_MODIFY_SYMBOLIC:
                code[j].instr = resolve_symbolic(p, locals, instr, OP_MODIFY_LOCAL, OP_MODIFY_GLOBAL);
                break;

            case OP_CALL:
            case OP_LIST:
                curstack -= instr.c;
                break;

            case OP_DICT:
                curstack -= 2 * (size_t) instr.c;
                break;
            }

            if (maxstack < curstack) {
                maxstack = curstack;
            }
        }
    }
    p->ranges.size -= nranges_ours;

    uint32_t nlocals = xht_size(locals);

    if (UU_UNLIKELY(maxstack > SIZE_MAX / 2))
        throw_error(p, "program is too big");
    if (UU_UNLIKELY(nlocals > UINT32_MAX / 2))
        throw_error(p, "too many locals");

    xht_destroy(locals);
    --p->scopes.size;

    if (p->scopes.size) {
        Range range = {p->scopes.size - 1, pos, -1};
        push_range(p, range);
    }

    *out_maxstack = maxstack;
    *out_nlocals = nlocals;
}

static inline Shape *fun_shape_ptr(Parser *p, size_t begin_pos)
{
    Instr instr = p->prog.data[begin_pos].instr;
    assert(instr.opcode == OP_FUNCTION);
    return &p->shapes.data[instr.c];
}

static size_t fun_begin(Parser *p)
{
    size_t pos = here(p);

    uint32_t shape_idx = add_shape(p);
    Instr instr = {OP_FUNCTION, 0, 0, shape_idx};
    emit(p, instr);

    open_scope(p);

    for (int i = 0; i < S__LAST; ++i)
        stack_push(&p->stacks[i], -1);

    return pos;
}

static void fun_param(Parser *p, size_t begin_pos, Lexeme name)
{
    xHt *locals = &p->scopes.data[p->scopes.size - 1];
    uint32_t old_size = xht_size(locals);
    uint32_t idx = *xht_put_int(
        locals,
        name.start, name.size, hash_str(name.start, name.size),
        old_size);

    if (UU_UNLIKELY(idx != old_size))
        throw_error_at(p, "duplicate parameter", name);

    Shape *shape = fun_shape_ptr(p, begin_pos);
    ++shape->nargs;
}

static void fun_end(Parser *p, size_t begin_pos)
{
    Instr push_nil = {OP_LOAD_CONST, 0, 0, add_const_steal(p, mk_nil())};
    emit(p, push_nil);

    Instr ret = {OP_RETURN, 0, 0, 0};
    emit(p, ret);

    size_t pos = here(p);
    Shape *shape = fun_shape_ptr(p, begin_pos);
    close_scope(p, &shape->maxstack, &shape->nlocals);
    size_t offset = pos - begin_pos;
    if (UU_UNLIKELY(offset > UINT32_MAX / 2)) {
        throw_error(p, "function body is too long");
    }
    shape->offset = offset;

    for (int i = 0; i < S__LAST; ++i)
        stack_pop(&p->stacks[i]);
}

static inline void this_is_expr(Parser *p, bool expect_expr)
{
    if (UU_UNLIKELY(!expect_expr))
        throw_error_at(p, "unexpected expression", p->cur);
}

static inline void after_expr(Parser *p, bool expect_expr)
{
    if (UU_UNLIKELY(expect_expr))
        throw_error_at(p, "expected expression", p->cur);
}

// forward declaration
static inline void expr(Parser *p, int8_t min_priority);

static void unary_operator(Parser *p)
{
    typedef struct {
        uint8_t op;
        int8_t priority;
    } UnaryOpProps;

    static const UnaryOpProps table[LK__LAST] = {
        [LK_MINUS] = {OP_NEG, 50},
        [LK_BANG] = {OP_NOT, 50},
        [LK_AT] = {OP_LEN, 60},
    };

    Lexeme cur = p->cur;

    UnaryOpProps props = table[cur.kind];

    if (UU_UNLIKELY(!props.priority))
        throw_error_at(p, "syntax error", cur);

    advance(p);
    expr(p, props.priority);

    Instr instr = {props.op, 0, 0, 0};
    emit_at(p, instr, cur);
}

static bool binary_operator(Parser *p, int8_t min_priority)
{
    typedef struct {
        uint8_t op;
        uint8_t a;
        int8_t priority;
        int8_t is_left_assoc;
    } BinaryOpProps;

    enum {
        LEFT_TO_RIGHT = 1,
        RIGHT_TO_LEFT = 0,
    };

    static const BinaryOpProps table[LK__LAST] = {
        [LK_TILDE] = {OP_AOP, AOP_CONCAT, 10, LEFT_TO_RIGHT},

        [LK_OR_OR] = {OP_AOP, AOP_OR, 11, LEFT_TO_RIGHT},
        [LK_AND_AND] = {OP_AOP, AOP_AND, 12, LEFT_TO_RIGHT},

        [LK_OR] = {OP_AOP, AOP_BIT_OR, 13, LEFT_TO_RIGHT},
        [LK_HAT] = {OP_AOP, AOP_BIT_XOR, 14, LEFT_TO_RIGHT},
        [LK_AND] = {OP_AOP, AOP_BIT_AND, 15, LEFT_TO_RIGHT},

        [LK_BANG_EQ] = {OP_CMP_2WAY, 0, 16, LEFT_TO_RIGHT},
        [LK_EQ_EQ] = {OP_CMP_2WAY, COMPARE_EQ, 16, LEFT_TO_RIGHT},

        [LK_GREATER_EQ] = {OP_CMP_3WAY, COMPARE_GREATER | COMPARE_EQ, 17, LEFT_TO_RIGHT},
        [LK_GREATER] = {OP_CMP_3WAY, COMPARE_GREATER, 17, LEFT_TO_RIGHT},
        [LK_LESS_EQ] = {OP_CMP_3WAY, COMPARE_LESS | COMPARE_EQ, 17, LEFT_TO_RIGHT},
        [LK_LESS] = {OP_CMP_3WAY, COMPARE_LESS, 17, LEFT_TO_RIGHT},

        [LK_GREATER_GREATER] = {OP_AOP, AOP_RSHIFT, 18, LEFT_TO_RIGHT},
        [LK_LESS_LESS] = {OP_AOP, AOP_LSHIFT, 18, LEFT_TO_RIGHT},

        [LK_MINUS] = {OP_AOP, AOP_SUB, 19, LEFT_TO_RIGHT},
        [LK_PLUS] = {OP_AOP, AOP_ADD, 19, LEFT_TO_RIGHT},

        [LK_PERCENT] = {OP_AOP, AOP_MOD, 20, LEFT_TO_RIGHT},
        [LK_SLASH] = {OP_AOP, AOP_DIV, 20, LEFT_TO_RIGHT},
        [LK_SLASH_SLASH] = {OP_AOP, AOP_IDIV, 20, LEFT_TO_RIGHT},
        [LK_STAR] = {OP_AOP, AOP_MUL, 20, LEFT_TO_RIGHT},

        [LK_STAR_STAR] = {OP_AOP, AOP_POW, 21, RIGHT_TO_LEFT},
    };

    Lexeme cur = p->cur;

    BinaryOpProps props = table[cur.kind];

    if (UU_UNLIKELY(!props.priority))
        throw_error_at(p, "syntax error", cur);

    if (props.priority < min_priority)
        return false;

    advance(p);
    expr(p, props.priority + props.is_left_assoc);

    Instr instr = {props.op, props.a, 0, 0};
    emit_at(p, instr, cur);

    return true;
}

static uint32_t funcall(Parser *p)
{
    advance(p);
    if (p->cur.kind == LK_RPAREN) {
        advance(p);
        return 0;
    }
    uint32_t nargs = 1;
    for (;;) {
        expr(p, -1);
        switch (p->cur.kind) {
        case LK_RPAREN:
            advance(p);
            return nargs;
        case LK_COMMA:
            advance(p);
            if (UU_UNLIKELY(nargs == UINT32_MAX))
                throw_error_at(p, "too many arguments", p->cur);
            ++nargs;
            break;
        default:
            throw_error_at(p, "expected ',' or ')'", p->cur);
        }
    }
}

static uint32_t newlist(Parser *p)
{
    advance(p);
    if (p->cur.kind == LK_RBRACKET) {
        advance(p);
        return 0;
    }
    uint32_t nelems = 1;
    for (;;) {
        expr(p, -1);
        switch (p->cur.kind) {
        case LK_RBRACKET:
            advance(p);
            return nelems;
        case LK_COMMA:
            advance(p);
            if (UU_UNLIKELY(nelems == UINT32_MAX))
                throw_error_at(p, "too many list elements", p->cur);
            ++nelems;
            break;
        default:
            throw_error_at(p, "expected ',' or ']'", p->cur);
        }
    }
}

static uint32_t newdict(Parser *p)
{
    advance(p);
    if (p->cur.kind == LK_RBRACE) {
        advance(p);
        return 0;
    }
    uint32_t nelems = 1;
    for (;;) {
        expr(p, -1);
        slurp(p, LK_COLON, "expected ':'");
        expr(p, -1);
        switch (p->cur.kind) {
        case LK_RBRACE:
            advance(p);
            return nelems;
        case LK_COMMA:
            advance(p);
            if (UU_UNLIKELY(nelems == UINT32_MAX))
                throw_error_at(p, "too many dict entries", p->cur);
            ++nelems;
            break;
        default:
            throw_error_at(p, "expected ',' or '}'", p->cur);
        }
    }
}

static uint32_t add_number_const(Parser *p, Lexeme token)
{
    const char *begin = token.start;
    const char *end = begin + token.size;
    return add_const_steal(p, (Value) number_parse(begin, end));
}

static inline int decode_hex(char c)
{
    switch (c) {
    case '0' ... '9':
        return c - '0';
    case 'a' ... 'f':
        return c - 'a' + 10;
    case 'A' ... 'F':
        return c - 'A' + 10;
    default:
        return -1;
    }
}

static int unescape(const char **s)
{
    char c = **s;
    ++*s;
    switch (c) {
    case '\\': return '\\';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'e': return '\033';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'v': return '\v';
    case '"': return '"';
    case '0': return '\0';
    case 'x':
        {
            int hi = decode_hex(**s);
            ++*s;
            if (UU_UNLIKELY(hi < 0))
                goto error;

            int lo = decode_hex(**s);
            ++*s;
            if (UU_UNLIKELY(lo < 0))
                goto error;

            return (hi << 4) | lo;
        }
    default:
        goto error;
    }
error:
    return -1;
}

static uint32_t add_string_const(Parser *p, Lexeme token)
{
    const char *ptr = token.start + 1;
    const char *end = token.start + token.size - 1;

    String *s = (String *) string_new_with_capacity(NULL, 0, /*capacity=*/end - ptr);

    while (ptr != end) {
        const char *esc = memchr(ptr, '\\', end - ptr);
        if (!esc) {
            s = string_append(s, ptr, end - ptr);
            break;
        }
        s = string_append(s, ptr, esc - ptr);

        ++esc;
        int v = unescape(&esc);
        if (UU_UNLIKELY(v < 0)) {
            string_destroy(s);
            --esc;
            Position pos = {
                .line = token.pos.line,
                .column = token.pos.column + (esc - token.start),
            };
            throw_error_precise(p, "invalid escape", pos, 1);
        }

        char c = v;
        s = string_append(s, &c, 1);
        ptr = esc;
    }
    return add_const_steal(p, (Value) s);
}

static inline void expr(Parser *p, int8_t min_priority)
{
    bool expect_expr = true;
    for (;;) {
        Lexeme cur = p->cur;
        switch (cur.kind) {
        case LK_NUMBER:
            this_is_expr(p, expect_expr);
            emit_at(
                p,
                (Instr) {OP_LOAD_CONST, 0, 0, add_number_const(p, cur)},
                cur);
            expect_expr = false;
            advance(p);
            break;

        case LK_TRUE:
            this_is_expr(p, expect_expr);
            emit_at(
                p,
                (Instr) {OP_LOAD_CONST, 0, 0, add_const_steal(p, mk_flag(true))},
                cur);
            expect_expr = false;
            advance(p);
            break;

        case LK_FALSE:
            this_is_expr(p, expect_expr);
            emit_at(
                p,
                (Instr) {OP_LOAD_CONST, 0, 0, add_const_steal(p, mk_flag(false))},
                cur);
            expect_expr = false;
            advance(p);
            break;

        case LK_NIL:
            this_is_expr(p, expect_expr);
            emit_at(
                p,
                (Instr) {OP_LOAD_CONST, 0, 0, add_const_steal(p, mk_nil())},
                cur);
            expect_expr = false;
            advance(p);
            break;

        case LK_STRING:
            this_is_expr(p, expect_expr);
            emit_at(
                p,
                (Instr) {OP_LOAD_CONST, 0, 0, add_string_const(p, cur)},
                cur);
            expect_expr = false;
            advance(p);
            break;

        case LK_IDENT:
            this_is_expr(p, expect_expr);
            emit_at(
                p,
                load(p, cur),
                cur);
            expect_expr = false;
            advance(p);
            break;

        case LK_LBRACKET:
            if (expect_expr) {
                uint32_t nelems = newlist(p);
                emit_at(
                    p,
                    (Instr) {OP_LIST, 0, 0, nelems},
                    cur);
                expect_expr = false;
            } else {
                advance(p);
                expr(p, -1);
                slurp(p, LK_RBRACKET, "expected ']'");
                emit_at(
                    p,
                    (Instr) {OP_LOAD_AT, 0, 0, 0},
                    cur);
            }
            break;

        case LK_LBRACE:
            {
                this_is_expr(p, expect_expr);
                uint32_t nentries = newdict(p);
                emit_at(
                    p,
                    (Instr) {OP_DICT, 0, 0, nentries},
                    cur);
                expect_expr = false;
            }
            break;

        case LK_LPAREN:
            if (expect_expr) {
                advance(p);
                expr(p, -1);
                slurp(p, LK_RPAREN, "expected ')'");
                expect_expr = false;
            } else {
                uint32_t nargs = funcall(p);
                emit_at(
                    p,
                    (Instr) {OP_CALL, 0, 0, nargs},
                    cur);
            }
            break;

        case LK_DOT:
            {
                after_expr(p, expect_expr);
                advance(p);
                Lexeme field = p->cur;
                if (UU_UNLIKELY(field.kind != LK_IDENT))
                    throw_error_at(p, "expected identifier (field name)", field);

                uint32_t const_idx = add_const_steal(p, (Value) string_new(field.start, field.size));
                emit_at(
                    p,
                    (Instr) {OP_LOAD_CONST, 0, 0, const_idx},
                    field);

                emit_at(
                    p,
                    (Instr) {OP_LOAD_AT, 0, 0, 0},
                    field);

                advance(p);
            }
            break;

        case LK_AND_AND_EQ:
        case LK_AND_EQ:
        case LK_COMMA:
        case LK_EOF:
        case LK_EQ:
        case LK_COLON:
        case LK_HAT_EQ:
        case LK_MINUS_EQ:
        case LK_OR_EQ:
        case LK_OR_OR_EQ:
        case LK_PERCENT_EQ:
        case LK_PLUS_EQ:
        case LK_SEMICOLON:
        case LK_SLASH_EQ:
        case LK_SLASH_SLASH_EQ:
        case LK_STAR_EQ:
        case LK_STAR_STAR_EQ:
        case LK_TILDE_EQ:
        case LK_COLON_EQ:
        case LK_RPAREN:
        case LK_RBRACKET:
        case LK_RBRACE:
        case LK_GREATER_GREATER_EQ:
        case LK_LESS_LESS_EQ:
            after_expr(p, expect_expr);
            return;

        default:
            if (expect_expr) {
                unary_operator(p);
                expect_expr = false;
            } else {
                if (!binary_operator(p, min_priority))
                    return;
            }
            break;
        }
    }
}

static inline size_t postpone_jump(Parser *p)
{
    size_t pos = here(p);
    emit(p, (Instr) {OP_JUMP, 0, 0, 0 /*to be fixed up*/});
    return pos;
}

static inline size_t postpone_jump_unless(Parser *p)
{
    size_t pos = here(p);
    emit(p, (Instr) {OP_JUMP_UNLESS, 0, 0, 0 /*to be fixed up*/});
    return pos;
}

static inline void fixup_jump(Parser *p, size_t from, size_t to)
{
    uint8_t opcode = p->prog.data[from].instr.opcode;
    assert(opcode == OP_JUMP || opcode == OP_JUMP_UNLESS);
    (void) opcode;
    p->prog.data[from].instr.c = to - from;
}

static inline void emit_jump_to(Parser *p, size_t to)
{
    size_t from = postpone_jump(p);
    fixup_jump(p, from, to);
}

// forward declaration
static void stmt(Parser *p);

static void block(Parser *p)
{
    slurp(p, LK_LBRACE, "expected '{'");
    for (;;) {
        stmt(p);
        switch (p->cur.kind) {
        case LK_EOF:
            throw_error_at(p, "expected '}'", p->cur);
            break;
        case LK_RBRACE:
            advance(p);
            return;
        default:
            // do nothing
            break;
        }
    }
}

static inline size_t if_or_elif_clause(Parser *p)
{
    advance(p);
    slurp(p, LK_LPAREN, "expected '('");
    expr(p, -1);
    size_t jump_pos = postpone_jump_unless(p);
    slurp(p, LK_RPAREN, "expected ')'");
    block(p);
    return jump_pos;
}

static inline void else_clause(Parser *p)
{
    advance(p);
    block(p);
}

static inline void stack_segment_begin(Stack *stack)
{
    stack_push(stack, -2);
}

static inline bool stack_segment_emit_jump_and_push(Parser *p, Stack *stack)
{
    if (UU_UNLIKELY(stack_peek(stack) == (size_t) -1))
        return false;
    size_t pos = postpone_jump(p);
    stack_push(stack, pos);
    return true;
}

static inline void stack_segment_end_fixup(Parser *p, Stack *stack, size_t to)
{
    size_t pos;
    while ((pos = stack_pop(stack)) != (size_t) -2)
        fixup_jump(p, pos, to);
}

static void if_stmt(Parser *p)
{
    Stack *stack = &p->stacks[S_IF];
    stack_segment_begin(stack);

    size_t prev_jump_unless = if_or_elif_clause(p);
    while (p->cur.kind == LK_ELIF) {
        size_t pos = postpone_jump(p);
        stack_push(stack, pos);

        fixup_jump(p, prev_jump_unless, here(p));
        prev_jump_unless = if_or_elif_clause(p);
    }
    if (p->cur.kind == LK_ELSE) {
        size_t pos = postpone_jump(p);
        stack_push(stack, pos);

        fixup_jump(p, prev_jump_unless, here(p));
        prev_jump_unless = -1;
        else_clause(p);
    }
    if (prev_jump_unless != (size_t) -1)
        fixup_jump(p, prev_jump_unless, here(p));

    stack_segment_end_fixup(p, stack, /*to=*/here(p));
}

static void while_stmt(Parser *p)
{
    Stack *stack_break    = &p->stacks[S_BREAK];
    Stack *stack_continue = &p->stacks[S_CONTINUE];

    stack_segment_begin(stack_break);
    stack_segment_begin(stack_continue);

    advance(p);
    slurp(p, LK_LPAREN, "expected '('");
    size_t begin_pos = here(p);
    expr(p, -1);
    size_t jump_pos = postpone_jump_unless(p);
    slurp(p, LK_RPAREN, "expected ')'");

    block(p);

    emit_jump_to(p, begin_pos);
    fixup_jump(p, jump_pos, here(p));

    stack_segment_end_fixup(p, stack_break,    /*to=*/here(p));
    stack_segment_end_fixup(p, stack_continue, /*to=*/begin_pos);
}

static void fun_stmt(Parser *p)
{
    advance(p);

    Lexeme name = p->cur;
    if (UU_UNLIKELY(name.kind != LK_IDENT))
        throw_error_at(p, "expected function name", name);
    advance(p);

    slurp(p, LK_LPAREN, "expected '('");

    size_t fun_pos = fun_begin(p);
    if (p->cur.kind != LK_RPAREN) {
        for (;;) {
            Lexeme param = p->cur;
            if (UU_UNLIKELY(param.kind != LK_IDENT))
                throw_error_at(p, "expected parameter name", param);
            fun_param(p, fun_pos, param);
            advance(p);
            if (p->cur.kind == LK_RPAREN)
                break;
            slurp(p, LK_COMMA, "expected ',' or ')'");
        }
    }
    advance(p);
    block(p);

    fun_end(p, fun_pos);

    Instr load_instr = load(p, name);
    Instr store_instr = load_to_store(p, load_instr, /*local=*/false, /*scapegoat=*/name);
    emit(p, store_instr);
}

static void expr_or_assignment(Parser *p)
{
    typedef struct {
        uint8_t aop;
        uint8_t is_valid;
    } OpAssignProps;

    static const OpAssignProps props_table[LK__LAST] = {
        [LK_AND_AND_EQ] = {AOP_AND, 1},
        [LK_AND_EQ] = {AOP_BIT_AND, 1},
        [LK_HAT_EQ] = {AOP_BIT_XOR, 1},
        [LK_GREATER_GREATER_EQ] = {AOP_RSHIFT, 1},
        [LK_LESS_LESS_EQ] = {AOP_LSHIFT, 1},
        [LK_MINUS_EQ] = {AOP_SUB, 1},
        [LK_OR_OR_EQ] = {AOP_OR, 1},
        [LK_OR_EQ] = {AOP_BIT_OR, 1},
        [LK_PERCENT_EQ] = {AOP_MOD, 1},
        [LK_PLUS_EQ] = {AOP_ADD, 1},
        [LK_SLASH_EQ] = {AOP_DIV, 1},
        [LK_SLASH_SLASH_EQ] = {AOP_IDIV, 1},
        [LK_STAR_EQ] = {AOP_MUL, 1},
        [LK_STAR_STAR_EQ] = {AOP_POW, 1},
        [LK_TILDE_EQ] = {AOP_CONCAT, 1},
    };

    expr(p, -1);
    Lexeme barrier = p->cur;

    switch (barrier.kind) {
    case LK_EQ:
    case LK_COLON_EQ:
        {
            Instr load_instr = unemit(p);
            advance(p);
            expr(p, -1);
            Instr store_instr = load_to_store(
                p,
                load_instr,
                /*local=*/barrier.kind == LK_COLON_EQ,
                /*scapegoat=*/barrier);
            emit_at(p, store_instr, barrier);
        }
        break;
    case LK_SEMICOLON:
        emit_at(
            p,
            (Instr) {OP_PRINT, 0, 0, 0},
            barrier);
        break;
    default:
        {
            OpAssignProps prop = props_table[barrier.kind];
            if (UU_UNLIKELY(!prop.is_valid))
                throw_error_at(p, "expected ';' or assignment", barrier);
            Instr load_instr = unemit(p);
            advance(p);
            expr(p, -1);
            Instr modify_instr = load_to_modify(p, load_instr, prop.aop, /*scapegoat=*/barrier);
            emit_at(p, modify_instr, barrier);
        }
    }
}

static inline void ti_reverse(TaggedInstr *mem, size_t nmem)
{
    TaggedInstr *mem_end = mem + nmem;
    for (size_t n = nmem / 2; n; --n) {
        --mem_end;
        SWAP(*mem, *mem_end);
        ++mem;
    }
}

static inline void ti_rotate_left(TaggedInstr *mem, size_t nmem, size_t nrotate)
{
    size_t d = nmem - nrotate;
    ti_reverse(mem, nmem);
    ti_reverse(mem, d);
    ti_reverse(mem + d, nrotate);
}

static void for_stmt(Parser *p)
{
    Stack *stack_break    = &p->stacks[S_BREAK];
    Stack *stack_continue = &p->stacks[S_CONTINUE];

    stack_segment_begin(stack_break);
    stack_segment_begin(stack_continue);

    // "for" "("
    advance(p);
    slurp(p, LK_LPAREN, "expected '('");

    // <initialization> ";"
    if (p->cur.kind != LK_SEMICOLON)
        expr_or_assignment(p);
    slurp(p, LK_SEMICOLON, "expected ';'");

    // <condition> ";"
    size_t begin_pos = here(p);
    size_t jump_pos;
    if (p->cur.kind != LK_SEMICOLON) {
        expr(p, -1);
        jump_pos = postpone_jump_unless(p);
    } else {
        jump_pos = -1;
    }
    slurp(p, LK_SEMICOLON, "expected ';'");

    // <postbody> ")"
    size_t i1 = here(p);
    if (p->cur.kind != LK_RPAREN)
        expr_or_assignment(p);
    slurp(p, LK_RPAREN, "expected ')'");

    // "{" <body> "}"
    size_t i2 = here(p);
    block(p);

    size_t i3 = here(p);
    size_t nrotate = i2 - i1;
    size_t ctnue_pos = i3 - nrotate;

    emit_jump_to(p, begin_pos);

    size_t end_pos = here(p);
    if (jump_pos != (size_t) -1)
        fixup_jump(p, jump_pos, end_pos);
    stack_segment_end_fixup(p, stack_break,    /*to=*/end_pos + nrotate);
    stack_segment_end_fixup(p, stack_continue, /*to=*/ctnue_pos + nrotate);

    ti_rotate_left(p->prog.data + i1, i3 - i1, nrotate);
}

static void stmt(Parser *p)
{
    switch (p->cur.kind) {
    case LK_IF:
        if_stmt(p);
        break;
    case LK_WHILE:
        while_stmt(p);
        break;
    case LK_FOR:
        for_stmt(p);
        break;
    case LK_FUN:
        fun_stmt(p);
        break;
    case LK_SEMICOLON:
        advance(p);
        break;
    case LK_RETURN:
        advance(p);
        if (p->cur.kind == LK_SEMICOLON) {
            advance(p);
            Instr push_nil = {OP_LOAD_CONST, 0, 0, add_const_steal(p, mk_nil())};
            emit(p, push_nil);
        } else {
            expr(p, -1);
            slurp(p, LK_SEMICOLON, "expected ';'");
        }
        emit(p, (Instr) {OP_RETURN, 0, 0, 0});
        break;
    case LK_BREAK:
        if (UU_UNLIKELY(!stack_segment_emit_jump_and_push(p, &p->stacks[S_BREAK])))
            throw_error_at(p, "'break' outside of a loop", p->cur);
        advance(p);
        slurp(p, LK_SEMICOLON, "expected ';'");
        break;
    case LK_CONTINUE:
        if (UU_UNLIKELY(!stack_segment_emit_jump_and_push(p, &p->stacks[S_CONTINUE])))
            throw_error_at(p, "'continue' outside of a loop", p->cur);
        advance(p);
        slurp(p, LK_SEMICOLON, "expected ';'");
        break;
    case LK_EOF:
    case LK_RBRACE:
        break;
    default:
        expr_or_assignment(p);
        slurp(p, LK_SEMICOLON, "expected ';'");
        break;
    }
}

Chunk *to_chunk(Parser *p, const char *source, size_t nsource, const char *origin)
{
    // shrink prog
    if (p->prog.capacity != p->prog.size) {
        p->prog.data = uu_xrealloc(p->prog.data, p->prog.size, sizeof(TaggedInstr));
        p->prog.capacity = p->prog.size;
    }

    TaggedInstr *prog = p->prog.data;
    size_t nprog = p->prog.size;

    Instr *code = uu_xmalloc(sizeof(Instr), nprog);
    Quark *quarks = uu_xmalloc(sizeof(Quark), nprog);
    size_t nquarks = 0;
    size_t cur_line = -1;
    for (size_t i = 0; i < nprog; ++i) {
        TaggedInstr ti = prog[i];
        code[i] = ti.instr;
        if (ti.line != ((size_t) -1) && ti.line != cur_line) {
            quarks[nquarks++] = (Quark) {i, ti.line};
            cur_line = ti.line;
        }
    }

    // shrink quarks
    if (nquarks != nprog) {
        quarks = uu_xrealloc(quarks, sizeof(Quark), nquarks);
    }

    // free and reset prog
    free(p->prog.data);
    p->prog = (Program) {NULL, 0, 0};

    Chunk *chunk = chunk_new_steal(
        code, nprog,
        p->consts.data, p->consts.size,
        quarks, nquarks,
        p->shapes.data, p->shapes.size,
        uu_xstrdup(origin),
        uu_xmemdup(source, nsource), nsource);

    // reset everything stolen by 'chunk_new_steal'
    p->consts = (ConstList) {NULL, 0, 0};
    p->shapes = (ShapeList) {NULL, 0, 0};

    return chunk;
}

ParseResult parse(State *state, const char *source, size_t nsource, const char *origin)
{
    Parser *p = parser_new(source, nsource, state);

    if (setjmp(p->err_handler) != 0) {
        // we have jumped here...

        ParseError err = p->err;
        p->err.msg = NULL;
        parser_destroy(p);

        return (ParseResult) {
           .as = {.error = err},
           .ok = false,
        };
    }

    size_t fun_pos = fun_begin(p);
    advance(p);
    for (;;) {
        stmt(p);
        switch (p->cur.kind) {
        case LK_RBRACE:
            throw_error_at(p, "extra '}'", p->cur);
            break;
        case LK_EOF:
            goto out;
        default:
            // do nothing
            break;
        }
    }
out:
    fun_end(p, fun_pos);

    Chunk *chunk = to_chunk(p, source, nsource, origin);
    Func *func = (Func *) mk_func(chunk, chunk->code);
    chunk_unref(chunk);

    parser_destroy(p);

    return (ParseResult) {
        .as = {.func = func},
        .ok = true,
    };
}
