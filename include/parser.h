#ifndef PYC_PARSER_H
#define PYC_PARSER_H

#include "ast.h"
#include "scanner.h"

/* Parses a token stream into an AST. Returns the root (sibling list of top-level items). */
TreeNode *parse_tokens(const TokenList *tokens, DiagnosticList *diags);

#endif /* PYC_PARSER_H */
