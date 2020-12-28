// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#pragma once

#include "common.h"
#include "position.h"

typedef enum {
    LK_AND,
    LK_AND_AND,
    LK_AND_AND_EQ,
    LK_AND_EQ,
    LK_AT,
    LK_BANG,
    LK_BANG_EQ,
    LK_BREAK,
    LK_COMMA,
    LK_CONTINUE,
    LK_DOT,
    LK_ELIF,
    LK_ELSE,
    LK_EOF,
    LK_COLON,
    LK_COLON_EQ,
    LK_EQ,
    LK_EQ_EQ,
    LK_ERROR,
    LK_FALSE,
    LK_FOR,
    LK_FUN,
    LK_GREATER,
    LK_GREATER_EQ,
    LK_GREATER_GREATER,
    LK_GREATER_GREATER_EQ,
    LK_HAT,
    LK_HAT_EQ,
    LK_IDENT,
    LK_IF,
    LK_LBRACE,
    LK_LBRACKET,
    LK_LESS,
    LK_LESS_EQ,
    LK_LESS_LESS,
    LK_LESS_LESS_EQ,
    LK_LPAREN,
    LK_MINUS,
    LK_MINUS_EQ,
    LK_MINUS_GREATER,
    LK_NIL,
    LK_NUMBER,
    LK_OR,
    LK_OR_EQ,
    LK_OR_OR,
    LK_OR_OR_EQ,
    LK_PERCENT,
    LK_PERCENT_EQ,
    LK_PLUS,
    LK_PLUS_EQ,
    LK_RBRACE,
    LK_RBRACKET,
    LK_RETURN,
    LK_RPAREN,
    LK_SEMICOLON,
    LK_SLASH,
    LK_SLASH_EQ,
    LK_SLASH_SLASH,
    LK_SLASH_SLASH_EQ,
    LK_STAR,
    LK_STAR_EQ,
    LK_STAR_STAR,
    LK_STAR_STAR_EQ,
    LK_STRING,
    LK_TILDE,
    LK_TILDE_EQ,
    LK_TRUE,
    LK_WHILE,

    LK__LAST,
} LexemeKind;

typedef struct {
    LexemeKind kind;
    const char *start;
    size_t size;
    Position pos;
} Lexeme;

struct Lexer;
typedef struct Lexer Lexer;

Lexer *lexer_new(const char *source, size_t nsource);

Lexeme lexer_next(Lexer *x);

const char *lexer_error_msg(Lexer *x);

void lexer_destroy(Lexer *x);
