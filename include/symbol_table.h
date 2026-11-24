#ifndef PYC_SYMBOL_TABLE_H
#define PYC_SYMBOL_TABLE_H

#include "ast.h"

#define ST_SIZE 71

typedef enum {
    BUILTIN_NONE = 0,
    BUILTIN_PRINT,
    BUILTIN_PRINTLN,
    BUILTIN_WRITE,
    BUILTIN_READ,
    BUILTIN_READ_INT,
    BUILTIN_READ_FLOAT,
    BUILTIN_LEN
} BuiltinKind;

typedef struct LineListRec {
    TreeNode *nd;
    struct LineListRec *prev;
    struct LineListRec *next;
    struct BucketListRec *bk;
} *LineList;

typedef struct BucketListRec {
    struct SymbolTable *st;
    TreeNode *nd;
    LineList lines;
    struct BucketListRec *prev;
    struct BucketListRec *next;

    /* Metadata for semantic analysis & bytecode generation */
    bool is_builtin;
    BuiltinKind builtin_kind;
    bool is_global;
    int slot; /* Global variable index, local slot index, or function index */
} *BucketList;

typedef struct SymbolTable {
    int id;
    TreeNode *nd;
    struct SymbolTable *upper;
    struct SymbolTable *lower;
    struct SymbolTable *prev;
    struct SymbolTable *next;
    BucketList hashTable[ST_SIZE];
} SymbolTable;

SymbolTable *st_initialize(bool restart);
SymbolTable *st_attach(SymbolTable *st);

BucketList st_insert_dcl(TreeNode *dclNd, SymbolTable *st);
LineList st_insert_ref(TreeNode *refNd, BucketList bk);

BucketList st_lookup_local(SymbolTable *st, const char *name);
BucketList st_lookup(SymbolTable *st, const char *name);

void st_print(const SymbolTable *st, FILE *out);
void st_free(SymbolTable *st);

#endif /* PYC_SYMBOL_TABLE_H */
