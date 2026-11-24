#ifndef PYC_SCANNER_H
#define PYC_SCANNER_H

#include "common.h"

typedef enum {
    /* Operators */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_STAR,           /* * */
    TOK_SLASH,          /* / */
    TOK_PERCENT,        /* % */
    TOK_PLUS_PLUS,      /* ++ */
    TOK_MINUS_MINUS,    /* -- */

    /* Assignment */
    TOK_ASSIGN,         /* = */
    TOK_PLUS_ASSIGN,    /* += */
    TOK_MINUS_ASSIGN,   /* -= */
    TOK_STAR_ASSIGN,    /* *= */
    TOK_SLASH_ASSIGN,   /* /= */
    TOK_PERCENT_ASSIGN, /* %= */

    /* Comparison */
    TOK_EQ,             /* == */
    TOK_NEQ,            /* != */
    TOK_LT,             /* < */
    TOK_LTE,            /* <= */
    TOK_GT,             /* > */
    TOK_GTE,            /* >= */

    /* Logical */
    TOK_AND,            /* && or and */
    TOK_OR,             /* || or or */
    TOK_NOT,            /* ! or not */

    /* Delimiters */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */
    TOK_COMMA,          /* , */
    TOK_COLON,          /* : */
    TOK_SEMI,           /* ; */
    TOK_ARROW,          /* -> */

    /* Literals & Identifiers */
    TOK_ID,
    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STR_LIT,
    TOK_TRUE,
    TOK_FALSE,

    /* Type keywords */
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_CHAR,
    TOK_BOOL,
    TOK_VOID,

    /* Control & declaration keywords */
    TOK_IF,
    TOK_ELIF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_DO,
    TOK_FOR,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_RETURN,
    TOK_DEF,
    TOK_PASS,

    /* Structure */
    TOK_NEWLINE,
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;
    int line;
    int col;
    int indent; /* Leading indentation width of the logical line */
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

const char *token_type_name(TokenType type);

void token_list_init(TokenList *list);
void token_list_free(TokenList *list);

/* Scans source text into tokens. Returns true if no lexical errors occurred. */
bool scan_source(const char *source, TokenList *out_tokens, DiagnosticList *diags);

void print_tokens(const TokenList *list, FILE *out);

#endif /* PYC_SCANNER_H */
