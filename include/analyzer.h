#ifndef PYC_ANALYZER_H
#define PYC_ANALYZER_H

#include "ast.h"
#include "symbol_table.h"

typedef struct Analyzer Analyzer;

struct Analyzer {
    SymbolTable *global_scope;
    TreeNode *parse_tree;
    TreeNode *builtin_nodes; /* Owned dummy AST nodes for built-in functions */
    DiagnosticList *diags;
    bool has_error;

    /* Current traversal state */
    TreeNode *current_func;
    int loop_depth;
};

Analyzer *analyzer_create(TreeNode *parse_tree, DiagnosticList *diags);
void analyzer_destroy(Analyzer *analyzer);

/* Runs symbol table construction + full semantic & type checking. Returns true if error-free. */
bool analyzer_run(Analyzer *analyzer);

SymbolTable *analyzer_get_symbol_table(const Analyzer *analyzer);

#endif /* PYC_ANALYZER_H */
