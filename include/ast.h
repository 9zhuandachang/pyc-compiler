#ifndef PYC_AST_H
#define PYC_AST_H

#include "common.h"
#include "scanner.h"

#define MAX_CHILDREN 4

typedef enum {
    TYPE_VOID,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_ARRAY,
    TYPE_UNKNOWN,
    TYPE_ERROR
} DataType;

typedef enum {
    NODE_DCL,
    NODE_PARAM,
    NODE_STMT,
    NODE_EXPR
} NodeKind;

typedef enum {
    DCL_VAR,
    DCL_ARRAY,
    DCL_FUNC
} DclKind;

typedef enum {
    PARAM_VAR,
    PARAM_ARRAY
} ParamKind;

typedef enum {
    STMT_COMPOUND,
    STMT_IF,
    STMT_WHILE,
    STMT_DO_WHILE,
    STMT_FOR,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_EXPR,
    STMT_EMPTY
} StmtKind;

typedef enum {
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_POSTFIX,
    EXPR_ASSIGN,
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_ID,
    EXPR_INDEX,
    EXPR_CALL
} ExprKind;

typedef struct TreeNode {
    struct TreeNode *child[MAX_CHILDREN];
    struct TreeNode *lSibling;
    struct TreeNode *rSibling;
    struct TreeNode *parent;

    int line;
    int col;

    NodeKind node_kind;
    union {
        DclKind dcl;
        ParamKind param;
        StmtKind stmt;
        ExprKind expr;
    } kind;

    /* Declaration / parameter attributes */
    DataType dcl_type;
    DataType elem_type;
    int array_size;

    /* Expression / identifier attributes */
    TokenType op;
    char *name;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        char *str_val;
    } lit;

    /* Populated by semantic analyzer */
    DataType expr_type;
    DataType expr_elem_type;
    void *sym_ref;   /* BucketListRec* for dcl/param, LineListRec* for refs */
    void *scope_ref; /* SymbolTable* for compound/function/for scopes */
} TreeNode;

const char *data_type_name(DataType type);

TreeNode *ast_new_dcl(DclKind kind, int line, int col);
TreeNode *ast_new_param(ParamKind kind, int line, int col);
TreeNode *ast_new_stmt(StmtKind kind, int line, int col);
TreeNode *ast_new_expr(ExprKind kind, int line, int col);

void ast_append_sibling(TreeNode **head, TreeNode **tail, TreeNode *node);
void ast_set_child(TreeNode *parent, int index, TreeNode *child);

void ast_free(TreeNode *root);
void ast_print(const TreeNode *root, FILE *out);

#endif /* PYC_AST_H */
