#ifndef _SCANNER_H_
#define _SCANNER_H_

#include "global.h"

// TokenType enum and tokenTypeNames array should be defined here
typedef enum tokentype {
    PLUS, MINUS, MUL, DIV, MOD,
    LT, GT, LTE, GTE, ASSIGN, EQ,
    INT, FRAC, STR,
    ID,
    INTL, FRACL, STRL,
    LCUR, RCUR, LPAR, RPAR, LBRA, RBRA,
    NEWLINE, COLON, SEMI, COMMA,
    INDENT,
    COMMENT, TAB,
    DO, WHILE, FOR, ELSE, IF,
    DEF,
    EoF
} TokenType;

extern const char* tokenTypeNames[];


typedef struct token {
    TokenType type;
    char *info;
} Token;

typedef struct node {
    Token t;
    struct node *prev;
    struct node *next;
} Node;

typedef struct list {
    Node *head;
    Node *tail;
    int size;
} List;

// State enum for scanner
typedef enum state {
    START, IN_ID, IN_NUM, READ_LT, READ_GT, READ_EQ, READ_OVER, IN_COMMENT, DONE, WRONG, IN_FRAC
} State;

// Function to initialize the token list
void initList(List *list);

// Function to scan a file and generate tokens
void scanFile(FILE *file, List *tokenList);

void addToken(List *list, TokenType type, const char *info);

void scanFiles(FILE *file, List *tokenList);

// Function to free the token list
void freeList(List *list);

Node* createNode(TokenType type, const char *info);

#endif