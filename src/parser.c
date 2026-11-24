#include "parser.h"

#include <errno.h>
#include <stdlib.h>

typedef struct {
    const TokenList *tokens;
    size_t pos;
    DiagnosticList *diags;
} Parser;

static const Token *p_cur(const Parser *p) {
    if (p->pos < p->tokens->count) {
        return &p->tokens->items[p->pos];
    }
    return &p->tokens->items[p->tokens->count - 1]; /* TOK_EOF */
}

static const Token *p_peek(const Parser *p, size_t offset) {
    if (p->pos + offset < p->tokens->count) {
        return &p->tokens->items[p->pos + offset];
    }
    return &p->tokens->items[p->tokens->count - 1];
}

static const Token *p_advance(Parser *p) {
    const Token *t = p_cur(p);
    if (t->type != TOK_EOF) {
        p->pos++;
    }
    return t;
}

static bool p_check(const Parser *p, TokenType type) {
    return p_cur(p)->type == type;
}

static bool p_match(Parser *p, TokenType type) {
    if (p_check(p, type)) {
        p_advance(p);
        return true;
    }
    return false;
}

static bool p_expect(Parser *p, TokenType type, const char *ctx) {
    if (p_check(p, type)) {
        p_advance(p);
        return true;
    }
    const Token *t = p_cur(p);
    diag_add(p->diags, t->line, t->col,
             "expected %s %s, got %s ('%s')",
             token_type_name(type), ctx ? ctx : "",
             token_type_name(t->type), t->lexeme ? t->lexeme : "");
    return false;
}

static void p_skip_newlines(Parser *p) {
    while (p_check(p, TOK_NEWLINE)) {
        p_advance(p);
    }
}

static const Token *p_peek_skip_newlines(const Parser *p, size_t *out_offset) {
    size_t off = 0;
    while (p_peek(p, off)->type == TOK_NEWLINE) {
        off++;
    }
    if (out_offset) *out_offset = off;
    return p_peek(p, off);
}

static void p_synchronize(Parser *p) {
    while (!p_check(p, TOK_EOF)) {
        TokenType t = p_cur(p)->type;
        if (t == TOK_SEMI || t == TOK_NEWLINE) {
            p_advance(p);
            return;
        }
        if (t == TOK_RBRACE) {
            return;
        }
        p_advance(p);
    }
}

static bool is_type_keyword(TokenType type) {
    switch (type) {
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_STRING:
        case TOK_CHAR:
        case TOK_BOOL:
        case TOK_VOID:
            return true;
        default:
            return false;
    }
}

static DataType token_to_data_type(TokenType type) {
    switch (type) {
        case TOK_INT:    return TYPE_INT;
        case TOK_FLOAT:  return TYPE_FLOAT;
        case TOK_STRING: return TYPE_STRING;
        case TOK_CHAR:   return TYPE_STRING;
        case TOK_BOOL:   return TYPE_BOOL;
        case TOK_VOID:   return TYPE_VOID;
        default:         return TYPE_UNKNOWN;
    }
}

static bool is_assign_op(TokenType type) {
    switch (type) {
        case TOK_ASSIGN:
        case TOK_PLUS_ASSIGN:
        case TOK_MINUS_ASSIGN:
        case TOK_STAR_ASSIGN:
        case TOK_SLASH_ASSIGN:
        case TOK_PERCENT_ASSIGN:
            return true;
        default:
            return false;
    }
}

/* Forward declarations */
static TreeNode *parse_item(Parser *p);
static TreeNode *parse_block_or_stmt(Parser *p, int header_indent, bool allow_single, bool *out_was_python_suite);
static TreeNode *parse_brace_block(Parser *p);
static TreeNode *parse_expression(Parser *p);
static TreeNode *parse_assignment(Parser *p);

static TreeNode *parse_param_list(Parser *p) {
    TreeNode *head = NULL;
    TreeNode *tail = NULL;

    if (p_check(p, TOK_RPAREN)) {
        return NULL;
    }
    /* C-style func(void) */
    if (p_check(p, TOK_VOID) && p_peek(p, 1)->type == TOK_RPAREN) {
        p_advance(p);
        return NULL;
    }

    while (!p_check(p, TOK_RPAREN) && !p_check(p, TOK_EOF)) {
        const Token *start = p_cur(p);
        DataType dtype = TYPE_UNKNOWN;
        bool is_arr = false;
        char *pname = NULL;

        if (is_type_keyword(start->type)) {
            /* C-style: int x  or  int arr[] */
            dtype = token_to_data_type(p_advance(p)->type);
            if (!p_check(p, TOK_ID)) {
                diag_add(p->diags, p_cur(p)->line, p_cur(p)->col,
                         "expected parameter name after type '%s'", data_type_name(dtype));
                break;
            }
            pname = pyc_strdup(p_advance(p)->lexeme);
            if (p_match(p, TOK_LBRACKET)) {
                is_arr = true;
                p_expect(p, TOK_RBRACKET, "in array parameter");
            }
        } else if (start->type == TOK_ID) {
            /* Python-style: x: int  or  arr: int[] */
            pname = pyc_strdup(p_advance(p)->lexeme);
            if (p_match(p, TOK_COLON)) {
                if (!is_type_keyword(p_cur(p)->type)) {
                    diag_add(p->diags, p_cur(p)->line, p_cur(p)->col,
                             "expected type annotation after ':' for parameter '%s'", pname);
                    free(pname);
                    break;
                }
                dtype = token_to_data_type(p_advance(p)->type);
                if (p_match(p, TOK_LBRACKET)) {
                    is_arr = true;
                    p_expect(p, TOK_RBRACKET, "in array parameter annotation");
                }
            } else {
                /* Default untyped Python parameter to int */
                dtype = TYPE_INT;
            }
        } else {
            diag_add(p->diags, start->line, start->col,
                     "unexpected token '%s' in parameter list", start->lexeme ? start->lexeme : "");
            break;
        }

        TreeNode *param = ast_new_param(is_arr ? PARAM_ARRAY : PARAM_VAR, start->line, start->col);
        param->name = pname;
        if (is_arr) {
            param->dcl_type = TYPE_ARRAY;
            param->elem_type = dtype;
        } else {
            param->dcl_type = dtype;
        }
        ast_append_sibling(&head, &tail, param);

        if (!p_match(p, TOK_COMMA)) {
            break;
        }
    }

    return head;
}

static TreeNode *parse_func_def_python(Parser *p) {
    const Token *def_tok = p_advance(p); /* 'def' */
    int header_indent = def_tok->indent;

    if (!p_check(p, TOK_ID)) {
        diag_add(p->diags, p_cur(p)->line, p_cur(p)->col, "expected function name after 'def'");
        p_synchronize(p);
        return NULL;
    }
    const Token *name_tok = p_advance(p);

    TreeNode *fn = ast_new_dcl(DCL_FUNC, def_tok->line, def_tok->col);
    fn->name = pyc_strdup(name_tok->lexeme);
    fn->dcl_type = TYPE_UNKNOWN; /* inferred or void if no '-> type' */

    p_expect(p, TOK_LPAREN, "after function name");
    TreeNode *params = parse_param_list(p);
    p_expect(p, TOK_RPAREN, "after parameter list");

    if (p_match(p, TOK_ARROW)) {
        if (is_type_keyword(p_cur(p)->type)) {
            fn->dcl_type = token_to_data_type(p_advance(p)->type);
        } else {
            diag_add(p->diags, p_cur(p)->line, p_cur(p)->col, "expected return type after '->'");
        }
    }

    ast_set_child(fn, 0, params);

    bool was_py = false;
    TreeNode *body = parse_block_or_stmt(p, header_indent, false, &was_py);
    if (body && !(body->node_kind == NODE_STMT && body->kind.stmt == STMT_COMPOUND)) {
        TreeNode *blk = ast_new_stmt(STMT_COMPOUND, body->line, body->col);
        ast_set_child(blk, 0, body);
        body = blk;
    }
    ast_set_child(fn, 1, body);
    return fn;
}

static TreeNode *parse_typed_dcl(Parser *p, bool consume_terminator) {
    const Token *type_tok = p_advance(p);
    DataType base_type = token_to_data_type(type_tok->type);
    int header_indent = type_tok->indent;

    if (!p_check(p, TOK_ID)) {
        diag_add(p->diags, p_cur(p)->line, p_cur(p)->col,
                 "expected identifier after type '%s'", data_type_name(base_type));
        p_synchronize(p);
        return NULL;
    }

    /* C-style function declaration: type ID '(' ... */
    if (p_peek(p, 1)->type == TOK_LPAREN) {
        const Token *name_tok = p_advance(p);
        TreeNode *fn = ast_new_dcl(DCL_FUNC, type_tok->line, type_tok->col);
        fn->name = pyc_strdup(name_tok->lexeme);
        fn->dcl_type = base_type;

        p_advance(p); /* '(' */
        TreeNode *params = parse_param_list(p);
        p_expect(p, TOK_RPAREN, "after function parameters");
        ast_set_child(fn, 0, params);

        bool was_py = false;
        TreeNode *body = parse_block_or_stmt(p, header_indent, false, &was_py);
        if (body && !(body->node_kind == NODE_STMT && body->kind.stmt == STMT_COMPOUND)) {
            TreeNode *blk = ast_new_stmt(STMT_COMPOUND, body->line, body->col);
            ast_set_child(blk, 0, body);
            body = blk;
        }
        ast_set_child(fn, 1, body);
        return fn;
    }

    /* Variable / array declaration(s): int a = 1, b[10], c; */
    TreeNode *head = NULL;
    TreeNode *tail = NULL;

    while (true) {
        if (!p_check(p, TOK_ID)) {
            diag_add(p->diags, p_cur(p)->line, p_cur(p)->col, "expected variable name in declaration");
            break;
        }
        const Token *name_tok = p_advance(p);
        TreeNode *dcl = NULL;

        if (p_match(p, TOK_LBRACKET)) {
            dcl = ast_new_dcl(DCL_ARRAY, name_tok->line, name_tok->col);
            dcl->name = pyc_strdup(name_tok->lexeme);
            dcl->dcl_type = TYPE_ARRAY;
            dcl->elem_type = base_type;
            TreeNode *size_expr = parse_expression(p);
            if (size_expr && size_expr->node_kind == NODE_EXPR && size_expr->kind.expr == EXPR_INT) {
                dcl->array_size = (int)size_expr->lit.int_val;
            }
            ast_set_child(dcl, 0, size_expr);
            p_expect(p, TOK_RBRACKET, "after array size");
        } else {
            dcl = ast_new_dcl(DCL_VAR, name_tok->line, name_tok->col);
            dcl->name = pyc_strdup(name_tok->lexeme);
            dcl->dcl_type = base_type;
            if (p_match(p, TOK_ASSIGN)) {
                TreeNode *init_expr = parse_expression(p);
                ast_set_child(dcl, 0, init_expr);
            }
        }

        ast_append_sibling(&head, &tail, dcl);
        if (!p_match(p, TOK_COMMA)) {
            break;
        }
    }

    if (consume_terminator) {
        p_match(p, TOK_SEMI);
    }
    return head;
}

static TreeNode *parse_brace_block(Parser *p) {
    const Token *lb = p_advance(p); /* '{' */
    TreeNode *blk = ast_new_stmt(STMT_COMPOUND, lb->line, lb->col);
    TreeNode *head = NULL;
    TreeNode *tail = NULL;

    while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        p_skip_newlines(p);
        if (p_check(p, TOK_RBRACE) || p_check(p, TOK_EOF)) break;
        if (p_match(p, TOK_SEMI)) continue;

        size_t before = p->pos;
        TreeNode *item = parse_item(p);
        ast_append_sibling(&head, &tail, item);
        if (p->pos == before && !p_check(p, TOK_EOF)) {
            p_synchronize(p);
        }
    }
    p_expect(p, TOK_RBRACE, "to close '{' block");
    ast_set_child(blk, 0, head);
    return blk;
}

static TreeNode *parse_python_suite(Parser *p, int header_indent) {
    const Token *first = p_cur(p);
    TreeNode *blk = ast_new_stmt(STMT_COMPOUND, first->line, first->col);

    if (first->type == TOK_EOF || first->indent <= header_indent) {
        diag_add(p->diags, first->line, first->col,
                 "expected an indented block after ':' (indentation must be greater than %d)",
                 header_indent);
        return blk;
    }

    int body_indent = first->indent;
    TreeNode *head = NULL;
    TreeNode *tail = NULL;

    while (!p_check(p, TOK_EOF)) {
        p_skip_newlines(p);
        if (p_check(p, TOK_EOF) || p_check(p, TOK_RBRACE)) {
            break;
        }
        const Token *tok = p_cur(p);
        if (tok->indent < body_indent) {
            break;
        }
        if (tok->indent > body_indent) {
            diag_add(p->diags, tok->line, tok->col,
                     "unexpected indentation (got %d, expected %d)",
                     tok->indent, body_indent);
            p_synchronize(p);
            continue;
        }

        size_t before = p->pos;
        TreeNode *item = parse_item(p);
        ast_append_sibling(&head, &tail, item);
        if (p->pos == before && !p_check(p, TOK_EOF)) {
            p_synchronize(p);
        }
    }

    ast_set_child(blk, 0, head);
    return blk;
}

static TreeNode *parse_block_or_stmt(Parser *p, int header_indent, bool allow_single, bool *out_was_python_suite) {
    if (out_was_python_suite) *out_was_python_suite = false;

    if (p_match(p, TOK_COLON)) {
        if (p_check(p, TOK_NEWLINE)) {
            p_skip_newlines(p);
            /* PyC hybrid: `while x > y :\n { ... }` */
            if (p_check(p, TOK_LBRACE)) {
                return parse_brace_block(p);
            }
            if (out_was_python_suite) *out_was_python_suite = true;
            return parse_python_suite(p, header_indent);
        }
        if (p_check(p, TOK_LBRACE)) {
            return parse_brace_block(p);
        }
        /* Inline statement after colon: `if x > 0: break` */
        TreeNode *single = parse_item(p);
        TreeNode *blk = ast_new_stmt(STMT_COMPOUND, single ? single->line : p_cur(p)->line,
                                     single ? single->col : p_cur(p)->col);
        ast_set_child(blk, 0, single);
        return blk;
    }

    /* Check for C-style `{ ... }` possibly on the next line */
    size_t nl_off = 0;
    const Token *after_nl = p_peek_skip_newlines(p, &nl_off);
    if (after_nl->type == TOK_LBRACE) {
        p_skip_newlines(p);
        return parse_brace_block(p);
    }

    if (allow_single) {
        p_skip_newlines(p);
        return parse_item(p);
    }

    diag_add(p->diags, p_cur(p)->line, p_cur(p)->col,
             "expected ':' or '{' to start block");
    return ast_new_stmt(STMT_COMPOUND, p_cur(p)->line, p_cur(p)->col);
}

static TreeNode *parse_if_stmt(Parser *p, bool is_elif) {
    const Token *kw = p_advance(p); /* 'if' or 'elif' */
    int header_indent = kw->indent;
    (void)is_elif;

    TreeNode *node = ast_new_stmt(STMT_IF, kw->line, kw->col);
    TreeNode *cond = parse_expression(p);
    ast_set_child(node, 0, cond);

    bool was_py = false;
    TreeNode *then_branch = parse_block_or_stmt(p, header_indent, true, &was_py);
    ast_set_child(node, 1, then_branch);

    /* Check for matching elif / else */
    size_t off = 0;
    const Token *next = p_peek_skip_newlines(p, &off);
    if ((next->type == TOK_ELIF || next->type == TOK_ELSE) &&
        (!was_py || next->indent == header_indent)) {
        p_skip_newlines(p);
        if (p_check(p, TOK_ELIF)) {
            TreeNode *elif_node = parse_if_stmt(p, true);
            ast_set_child(node, 2, elif_node);
        } else if (p_check(p, TOK_ELSE)) {
            const Token *else_tok = p_advance(p);
            /* Support `else if ...` as well */
            if (p_check(p, TOK_IF)) {
                TreeNode *else_if = parse_if_stmt(p, false);
                ast_set_child(node, 2, else_if);
            } else {
                bool else_py = false;
                TreeNode *else_branch = parse_block_or_stmt(p, else_tok->indent, true, &else_py);
                ast_set_child(node, 2, else_branch);
            }
        }
    }

    return node;
}

static TreeNode *parse_while_stmt(Parser *p) {
    const Token *kw = p_advance(p); /* 'while' */
    TreeNode *node = ast_new_stmt(STMT_WHILE, kw->line, kw->col);

    TreeNode *cond = parse_expression(p);
    ast_set_child(node, 0, cond);

    bool was_py = false;
    TreeNode *body = parse_block_or_stmt(p, kw->indent, true, &was_py);
    ast_set_child(node, 1, body);
    return node;
}

static TreeNode *parse_do_while_stmt(Parser *p) {
    const Token *kw = p_advance(p); /* 'do' */
    TreeNode *node = ast_new_stmt(STMT_DO_WHILE, kw->line, kw->col);

    bool was_py = false;
    TreeNode *body = parse_block_or_stmt(p, kw->indent, true, &was_py);
    ast_set_child(node, 0, body);

    p_skip_newlines(p);
    p_expect(p, TOK_WHILE, "after 'do' body");
    TreeNode *cond = parse_expression(p);
    ast_set_child(node, 1, cond);
    p_match(p, TOK_SEMI);
    return node;
}

static TreeNode *parse_for_stmt(Parser *p) {
    const Token *kw = p_advance(p); /* 'for' */
    TreeNode *node = ast_new_stmt(STMT_FOR, kw->line, kw->col);

    bool has_paren = p_match(p, TOK_LPAREN);

    /* Init */
    TreeNode *init_nd = NULL;
    if (!p_check(p, TOK_SEMI)) {
        if (is_type_keyword(p_cur(p)->type)) {
            init_nd = parse_typed_dcl(p, false);
        } else {
            init_nd = parse_expression(p);
        }
    }
    p_expect(p, TOK_SEMI, "after for-loop initializer");

    /* Condition */
    TreeNode *cond_nd = NULL;
    if (!p_check(p, TOK_SEMI)) {
        cond_nd = parse_expression(p);
    }
    p_expect(p, TOK_SEMI, "after for-loop condition");

    /* Update */
    TreeNode *update_nd = NULL;
    if ((has_paren && !p_check(p, TOK_RPAREN)) ||
        (!has_paren && !p_check(p, TOK_COLON) && !p_check(p, TOK_LBRACE) && !p_check(p, TOK_NEWLINE))) {
        update_nd = parse_expression(p);
    }
    if (has_paren) {
        p_expect(p, TOK_RPAREN, "after for-loop clauses");
    }

    bool was_py = false;
    TreeNode *body = parse_block_or_stmt(p, kw->indent, true, &was_py);

    ast_set_child(node, 0, init_nd);
    ast_set_child(node, 1, cond_nd);
    ast_set_child(node, 2, update_nd);
    ast_set_child(node, 3, body);
    return node;
}

static TreeNode *parse_return_stmt(Parser *p) {
    const Token *kw = p_advance(p); /* 'return' */
    TreeNode *node = ast_new_stmt(STMT_RETURN, kw->line, kw->col);

    if (!p_check(p, TOK_SEMI) && !p_check(p, TOK_NEWLINE) &&
        !p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        TreeNode *val = parse_expression(p);
        ast_set_child(node, 0, val);
    }
    p_match(p, TOK_SEMI);
    return node;
}

static TreeNode *parse_item(Parser *p) {
    const Token *t = p_cur(p);
    switch (t->type) {
        case TOK_DEF:
            return parse_func_def_python(p);
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_STRING:
        case TOK_CHAR:
        case TOK_BOOL:
        case TOK_VOID:
            return parse_typed_dcl(p, true);
        case TOK_LBRACE:
            return parse_brace_block(p);
        case TOK_IF:
            return parse_if_stmt(p, false);
        case TOK_WHILE:
            return parse_while_stmt(p);
        case TOK_DO:
            return parse_do_while_stmt(p);
        case TOK_FOR:
            return parse_for_stmt(p);
        case TOK_RETURN:
            return parse_return_stmt(p);
        case TOK_BREAK: {
            p_advance(p);
            p_match(p, TOK_SEMI);
            return ast_new_stmt(STMT_BREAK, t->line, t->col);
        }
        case TOK_CONTINUE: {
            p_advance(p);
            p_match(p, TOK_SEMI);
            return ast_new_stmt(STMT_CONTINUE, t->line, t->col);
        }
        case TOK_PASS:
        case TOK_SEMI: {
            p_advance(p);
            p_match(p, TOK_SEMI);
            return ast_new_stmt(STMT_EMPTY, t->line, t->col);
        }
        default: {
            TreeNode *expr = parse_expression(p);
            TreeNode *stmt = ast_new_stmt(STMT_EXPR, t->line, t->col);
            ast_set_child(stmt, 0, expr);
            p_match(p, TOK_SEMI);
            return stmt;
        }
    }
}

/* ==================== Expression Parsing (Precedence Climbing / RD) ==================== */

static TreeNode *parse_primary(Parser *p) {
    const Token *t = p_cur(p);
    switch (t->type) {
        case TOK_INT_LIT: {
            p_advance(p);
            TreeNode *n = ast_new_expr(EXPR_INT, t->line, t->col);
            errno = 0;
            n->lit.int_val = strtoll(t->lexeme, NULL, 10);
            return n;
        }
        case TOK_FLOAT_LIT: {
            p_advance(p);
            TreeNode *n = ast_new_expr(EXPR_FLOAT, t->line, t->col);
            n->lit.float_val = strtod(t->lexeme, NULL);
            return n;
        }
        case TOK_STR_LIT: {
            p_advance(p);
            TreeNode *n = ast_new_expr(EXPR_STRING, t->line, t->col);
            n->lit.str_val = pyc_strdup(t->lexeme);
            return n;
        }
        case TOK_TRUE: {
            p_advance(p);
            TreeNode *n = ast_new_expr(EXPR_BOOL, t->line, t->col);
            n->lit.bool_val = true;
            return n;
        }
        case TOK_FALSE: {
            p_advance(p);
            TreeNode *n = ast_new_expr(EXPR_BOOL, t->line, t->col);
            n->lit.bool_val = false;
            return n;
        }
        case TOK_ID: {
            p_advance(p);
            if (p_match(p, TOK_LPAREN)) {
                TreeNode *call = ast_new_expr(EXPR_CALL, t->line, t->col);
                call->name = pyc_strdup(t->lexeme);
                TreeNode *arg_head = NULL;
                TreeNode *arg_tail = NULL;
                if (!p_check(p, TOK_RPAREN)) {
                    while (true) {
                        TreeNode *arg = parse_expression(p);
                        ast_append_sibling(&arg_head, &arg_tail, arg);
                        if (!p_match(p, TOK_COMMA)) break;
                    }
                }
                p_expect(p, TOK_RPAREN, "after function call arguments");
                ast_set_child(call, 0, arg_head);
                return call;
            }
            TreeNode *id = ast_new_expr(EXPR_ID, t->line, t->col);
            id->name = pyc_strdup(t->lexeme);
            return id;
        }
        case TOK_LPAREN: {
            p_advance(p);
            TreeNode *inner = parse_expression(p);
            p_expect(p, TOK_RPAREN, "after parenthesized expression");
            return inner;
        }
        default: {
            diag_add(p->diags, t->line, t->col,
                     "expected expression, got %s ('%s')",
                     token_type_name(t->type), t->lexeme ? t->lexeme : "");
            if (t->type != TOK_EOF && t->type != TOK_NEWLINE && t->type != TOK_SEMI && t->type != TOK_RBRACE) {
                p_advance(p);
            }
            TreeNode *err = ast_new_expr(EXPR_INT, t->line, t->col);
            err->expr_type = TYPE_ERROR;
            return err;
        }
    }
}

static TreeNode *parse_postfix(Parser *p) {
    TreeNode *node = parse_primary(p);

    while (true) {
        if (p_check(p, TOK_LBRACKET)) {
            const Token *lb = p_advance(p);
            TreeNode *idx = parse_expression(p);
            p_expect(p, TOK_RBRACKET, "after array index");
            TreeNode *arr_idx = ast_new_expr(EXPR_INDEX, lb->line, lb->col);
            ast_set_child(arr_idx, 0, node);
            ast_set_child(arr_idx, 1, idx);
            node = arr_idx;
        } else if (p_check(p, TOK_PLUS_PLUS) || p_check(p, TOK_MINUS_MINUS)) {
            const Token *op = p_advance(p);
            TreeNode *pf = ast_new_expr(EXPR_POSTFIX, op->line, op->col);
            pf->op = op->type;
            ast_set_child(pf, 0, node);
            node = pf;
        } else {
            break;
        }
    }
    return node;
}

static TreeNode *parse_unary(Parser *p) {
    const Token *t = p_cur(p);
    if (t->type == TOK_MINUS || t->type == TOK_PLUS || t->type == TOK_NOT ||
        t->type == TOK_PLUS_PLUS || t->type == TOK_MINUS_MINUS) {
        p_advance(p);
        TreeNode *operand = parse_unary(p);
        TreeNode *u = ast_new_expr(EXPR_UNARY, t->line, t->col);
        u->op = t->type;
        ast_set_child(u, 0, operand);
        return u;
    }
    return parse_postfix(p);
}

static TreeNode *parse_multiplicative(Parser *p) {
    TreeNode *left = parse_unary(p);
    while (p_check(p, TOK_STAR) || p_check(p, TOK_SLASH) || p_check(p, TOK_PERCENT)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_unary(p);
        TreeNode *bin = ast_new_expr(EXPR_BINARY, op->line, op->col);
        bin->op = op->type;
        ast_set_child(bin, 0, left);
        ast_set_child(bin, 1, right);
        left = bin;
    }
    return left;
}

static TreeNode *parse_additive(Parser *p) {
    TreeNode *left = parse_multiplicative(p);
    while (p_check(p, TOK_PLUS) || p_check(p, TOK_MINUS)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_multiplicative(p);
        TreeNode *bin = ast_new_expr(EXPR_BINARY, op->line, op->col);
        bin->op = op->type;
        ast_set_child(bin, 0, left);
        ast_set_child(bin, 1, right);
        left = bin;
    }
    return left;
}

static TreeNode *parse_relational(Parser *p) {
    TreeNode *left = parse_additive(p);
    while (p_check(p, TOK_LT) || p_check(p, TOK_LTE) || p_check(p, TOK_GT) || p_check(p, TOK_GTE)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_additive(p);
        TreeNode *bin = ast_new_expr(EXPR_BINARY, op->line, op->col);
        bin->op = op->type;
        ast_set_child(bin, 0, left);
        ast_set_child(bin, 1, right);
        left = bin;
    }
    return left;
}

static TreeNode *parse_equality(Parser *p) {
    TreeNode *left = parse_relational(p);
    while (p_check(p, TOK_EQ) || p_check(p, TOK_NEQ)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_relational(p);
        TreeNode *bin = ast_new_expr(EXPR_BINARY, op->line, op->col);
        bin->op = op->type;
        ast_set_child(bin, 0, left);
        ast_set_child(bin, 1, right);
        left = bin;
    }
    return left;
}

static TreeNode *parse_logical_and(Parser *p) {
    TreeNode *left = parse_equality(p);
    while (p_check(p, TOK_AND)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_equality(p);
        TreeNode *bin = ast_new_expr(EXPR_BINARY, op->line, op->col);
        bin->op = op->type;
        ast_set_child(bin, 0, left);
        ast_set_child(bin, 1, right);
        left = bin;
    }
    return left;
}

static TreeNode *parse_logical_or(Parser *p) {
    TreeNode *left = parse_logical_and(p);
    while (p_check(p, TOK_OR)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_logical_and(p);
        TreeNode *bin = ast_new_expr(EXPR_BINARY, op->line, op->col);
        bin->op = op->type;
        ast_set_child(bin, 0, left);
        ast_set_child(bin, 1, right);
        left = bin;
    }
    return left;
}

static TreeNode *parse_assignment(Parser *p) {
    TreeNode *left = parse_logical_or(p);
    if (is_assign_op(p_cur(p)->type)) {
        const Token *op = p_advance(p);
        TreeNode *right = parse_assignment(p); /* right-associative */
        TreeNode *asgn = ast_new_expr(EXPR_ASSIGN, op->line, op->col);
        asgn->op = op->type;
        ast_set_child(asgn, 0, left);
        ast_set_child(asgn, 1, right);
        return asgn;
    }
    return left;
}

static TreeNode *parse_expression(Parser *p) {
    return parse_assignment(p);
}

TreeNode *parse_tokens(const TokenList *tokens, DiagnosticList *diags) {
    Parser p;
    p.tokens = tokens;
    p.pos = 0;
    p.diags = diags;

    TreeNode *head = NULL;
    TreeNode *tail = NULL;

    while (!p_check(&p, TOK_EOF)) {
        p_skip_newlines(&p);
        if (p_check(&p, TOK_EOF)) break;
        if (p_match(&p, TOK_SEMI)) continue;

        size_t before = p.pos;
        TreeNode *item = parse_item(&p);
        ast_append_sibling(&head, &tail, item);
        if (p.pos == before && !p_check(&p, TOK_EOF)) {
            p_synchronize(&p);
        }
    }

    return head;
}
