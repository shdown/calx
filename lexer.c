// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "lexer.h"

#include "ht.h"
#include "hash.h"

enum {
    BLOCKY_BIT = 1 << 10,
};

enum {
    LEVEL_EXPR,
    LEVEL_STMT,
};

typedef struct {
    char *data;
    size_t size;
    size_t capacity;

    size_t blocky_level;
    char is_expr_context;
} LevelStack;

static LevelStack level_stack_new(void)
{
    return (LevelStack) {NULL, 0, 0, (size_t) -1, 0};
}

static void level_stack_push(LevelStack *s, char level)
{
    if (s->size == s->capacity) {
        s->data = uu_x2realloc(s->data, &s->capacity, 1);
    }
    s->data[s->size++] = level;
    s->is_expr_context = (level == LEVEL_EXPR);
}

static void level_stack_set_blocky(LevelStack *s)
{
    s->blocky_level = s->size;
}

static void level_stack_push_blocky(LevelStack *s)
{
    char level = LEVEL_EXPR;
    if (s->blocky_level == s->size) {
        s->blocky_level = -1;
        level = LEVEL_STMT;
    }
    level_stack_push(s, level);
}

static void level_stack_pop(LevelStack *s)
{
    if (!s->size)
        return;
    --s->size;
    s->is_expr_context = (s->size && s->data[s->size - 1] == LEVEL_EXPR);
}

static void level_stack_free(LevelStack *s)
{
    free(s->data);
}

static Ht make_keywords_ht(void)
{
    typedef struct {
        const char *spelling;
        LexemeKind kind;
        char is_blocky;
    } Keyword;

    static const Keyword keywords[] = {
        {"fun", LK_FUN, 1},
        {"if", LK_IF, 1},
        {"else", LK_ELSE, 1},
        {"elif", LK_ELIF, 1},
        {"for", LK_FOR, 1},
        {"while", LK_WHILE, 1},
        {"break", LK_BREAK, 0},
        {"continue", LK_CONTINUE, 0},
        {"return", LK_RETURN, 0},
        {"true", LK_TRUE, 0},
        {"false", LK_FALSE, 0},
        {"nil", LK_NIL, 0},
        {0},
    };

    Ht ht = ht_new(1);
    for (const Keyword *p = keywords; p->spelling; ++p) {
        const char *spelling = p->spelling;
        size_t nspelling = strlen(spelling);
        uint32_t hash = hash_str(spelling, nspelling);
        uint32_t value = p->kind | (p->is_blocky ? BLOCKY_BIT : 0);
        ht_insert_new_unchecked(&ht, spelling, nspelling, hash, value);
    }
    return ht;
}

struct Lexer {
    const char *cur;
    const char *end;
    size_t line_num;
    const char *line_start;

    LevelStack level_stack;
    char inserted_semicolon_flag;

    Ht keywords;

    const char *err_msg;
};

Lexer *lexer_new(const char *source, size_t nsource)
{
    Lexer *x = uu_xmalloc(1, sizeof(Lexer));
    *x = (Lexer) {
        .cur = source,
        .end = source + nsource,
        .line_num = 1,
        .line_start = source,
        .level_stack = level_stack_new(),
        .inserted_semicolon_flag = 0,
        .keywords = make_keywords_ht(),
        .err_msg = NULL,
    };
    return x;
}

static inline Lexeme make_lexeme(Lexer *x, LexemeKind kind, const char *start)
{
    return (Lexeme) {
        .kind = kind,
        .start = start,
        .size = x->cur - start,
        .pos = {
            .line = x->line_num,
            .column = start - x->line_start + 1,
        },
    };
}

static inline Lexeme make_lexeme_advance(Lexer *x, LexemeKind kind, size_t size)
{
    const char *start = x->cur;
    x->cur += size;
    return make_lexeme(x, kind, start);
}

static inline Lexeme make_error_advance(Lexer *x, const char *msg, size_t size)
{
    x->err_msg = msg;
    return make_lexeme_advance(x, LK_ERROR, size);
}

static inline Lexeme choice1223(
        Lexer *x,
        LexemeKind if_a,
        char b, LexemeKind if_ab,
        char c, LexemeKind if_ac,
        char d, LexemeKind if_acd)
{
    if (x->cur + 1 != x->end) {
        if (x->cur[1] == b) {
            return make_lexeme_advance(x, if_ab, 2);
        }
        if (c != '\0' && x->cur[1] == c) {
            if (d != '\0' && x->cur + 2 != x->end && x->cur[2] == d) {
                return make_lexeme_advance(x, if_acd, 3);
            }
            return make_lexeme_advance(x, if_ac, 2);
        }
    }
    return make_lexeme_advance(x, if_a, 1);
}

static Lexeme number(Lexer *x)
{
    const char *start = x->cur;
    bool seen_dot = false;
    for (;;) {
        ++x->cur;
        if (UU_UNLIKELY(x->cur == x->end))
            goto out;
        switch (*x->cur) {
        case '0' ... '9':
        case '\'':
            break;
        case '.':
            if (seen_dot)
                goto out;
            seen_dot = true;
            break;
        default:
            goto out;
        }
    }
out:
    return make_lexeme(x, LK_NUMBER, start);
}

static Lexeme string(Lexer *x)
{
    const char *start = x->cur;
    for (;;) {
        ++x->cur;
        if (UU_UNLIKELY(x->cur == x->end))
            return make_error_advance(x, "unterminated string (EOF reached)", 0);
        switch (*x->cur) {
        case '"':
            ++x->cur;
            return make_lexeme(x, LK_STRING, start);
        case '\\':
            ++x->cur;
            if (UU_UNLIKELY(x->cur == x->end))
                return make_error_advance(x, "unterminated string (EOF reached)", 0);
            break;
        case '\n':
            return make_error_advance(x, "unterminated string (EOL reached)", 0);
        }
    }
}

static Lexeme identifier(Lexer *x)
{
    const char *start = x->cur;
    for (;;) {
        ++x->cur;
        if (UU_UNLIKELY(x->cur == x->end))
            goto out;
        switch (*x->cur) {
        case '0' ... '9':
        case 'a' ... 'z':
        case 'A' ... 'Z':
        case '_':
            break;
        default:
            goto out;
        }
    }
out:
    (void) 0;
    size_t size = x->cur - start;
    uint32_t value = ht_get(&x->keywords, start, size, hash_str(start, size), LK_IDENT);
    if (value & BLOCKY_BIT) {
        level_stack_set_blocky(&x->level_stack);
        value &= ~BLOCKY_BIT;
    }
    return make_lexeme(x, value, start);
}

static inline Lexeme fake_semicolon(Lexer *x)
{
    return make_lexeme_advance(x, LK_SEMICOLON, 0);
}

static inline bool insert_semicolon(Lexer *x)
{
    if (x->level_stack.is_expr_context) {
        return false;
    }
    if (x->inserted_semicolon_flag) {
        x->inserted_semicolon_flag = 0;
        return false;
    }
    x->inserted_semicolon_flag = 1;
    return true;
}

static Lexeme token(Lexer *x)
{
    switch (*x->cur) {

    case '0' ... '9':
        return number(x);

    case 'a' ... 'z':
    case 'A' ... 'Z':
    case '_':
        return identifier(x);

    case '"':
        return string(x);

    case '!':
        return choice1223(x, LK_BANG, '=', LK_BANG_EQ, 0, 0, 0, 0);

    case '%':
        return choice1223(x, LK_PERCENT, '=', LK_PERCENT_EQ, 0, 0, 0, 0);

    case '&':
        return choice1223(x, LK_AND, '=', LK_AND_EQ, '&', LK_AND_AND, '=', LK_AND_AND_EQ);

    case '|':
        return choice1223(x, LK_OR, '=', LK_OR_EQ, '|', LK_OR_OR, '=', LK_OR_OR_EQ);

    case '(':
        level_stack_push(&x->level_stack, LEVEL_EXPR);
        return make_lexeme_advance(x, LK_LPAREN, 1);

    case ')':
        level_stack_pop(&x->level_stack);
        return make_lexeme_advance(x, LK_RPAREN, 1);

    case '*':
        return choice1223(x, LK_STAR, '=', LK_STAR_EQ, '*', LK_STAR_STAR, '=', LK_STAR_STAR_EQ);

    case '+':
        return choice1223(x, LK_PLUS, '=', LK_PLUS_EQ, 0, 0, 0, 0);

    case '-':
        return choice1223(x, LK_MINUS, '=', LK_MINUS_EQ, '>', LK_MINUS_GREATER, 0, 0);

    case ',':
        return make_lexeme_advance(x, LK_COMMA, 1);

    case '.':
        return make_lexeme_advance(x, LK_DOT, 1);

    case '@':
        return make_lexeme_advance(x, LK_AT, 1);

    case '/':
        return choice1223(x, LK_SLASH, '=', LK_SLASH_EQ, '/', LK_SLASH_SLASH, '=', LK_SLASH_SLASH_EQ);

    case ':':
        return choice1223(x, LK_COLON, '=', LK_COLON_EQ, 0, 0, 0, 0);

    case ';':
        return make_lexeme_advance(x, LK_SEMICOLON, 1);

    case '<':
        return choice1223(x, LK_LESS, '=', LK_LESS_EQ, '<', LK_LESS_LESS, '=', LK_LESS_LESS_EQ);

    case '=':
        return choice1223(x, LK_EQ, '=', LK_EQ_EQ, 0, 0, 0, 0);

    case '>':
        return choice1223(x, LK_GREATER, '=', LK_GREATER_EQ, '>', LK_GREATER_GREATER, '=', LK_GREATER_GREATER_EQ);

    case '[':
        level_stack_push(&x->level_stack, LEVEL_EXPR);
        return make_lexeme_advance(x, LK_LBRACKET, 1);

    case ']':
        level_stack_pop(&x->level_stack);
        return make_lexeme_advance(x, LK_RBRACKET, 1);

    case '^':
        return choice1223(x, LK_HAT, '=', LK_HAT_EQ, 0, 0, 0, 0);

    case '{':
        level_stack_push_blocky(&x->level_stack);
        return make_lexeme_advance(x, LK_LBRACE, 1);

    case '~':
        return choice1223(x, LK_TILDE, '=', LK_TILDE_EQ, 0, 0, 0, 0);

    case '}':
        if (insert_semicolon(x))
            return fake_semicolon(x);
        level_stack_pop(&x->level_stack);
        return make_lexeme_advance(x, LK_RBRACE, 1);

    default:
        return make_error_advance(x, "unexpected symbol", 1);
    }
}

Lexeme lexer_next(Lexer *x)
{
    for (;;) {
        if (UU_UNLIKELY(x->cur == x->end)) {
            if (insert_semicolon(x))
                return fake_semicolon(x);
            return make_lexeme_advance(x, LK_EOF, 0);
        }
        switch (*x->cur) {
        case ' ':
        case '\t':
            ++x->cur;
            break;
        case '\n':
            if (insert_semicolon(x))
                return fake_semicolon(x);
            ++x->cur;
            ++x->line_num;
            x->line_start = x->cur;
            break;
        case '#':
            do {
                ++x->cur;
            } while (x->cur != x->end && *x->cur != '\n');
            break;
        default:
            return token(x);
        }
    }
}

const char *lexer_error_msg(Lexer *x)
{
    return x->err_msg;
}

void lexer_destroy(Lexer *x)
{
    level_stack_free(&x->level_stack);
    ht_destroy(&x->keywords);
    free(x);
}
