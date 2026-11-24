#include "analyzer.h"

#include <stdarg.h>

static bool is_numeric(DataType t) {
    return t == TYPE_INT || t == TYPE_FLOAT;
}

static bool is_truthy_type(DataType t) {
    return t == TYPE_BOOL || t == TYPE_INT || t == TYPE_FLOAT;
}

static bool is_assignable_type(DataType target, DataType value) {
    if (target == TYPE_ERROR || value == TYPE_ERROR) return true;
    if (target == value) return true;
    if (target == TYPE_FLOAT && value == TYPE_INT) return true;
    if (target == TYPE_BOOL && value == TYPE_INT) return true;
    return false;
}

static TreeNode *add_builtin_func(Analyzer *a, const char *name, DataType ret_type,
                                  BuiltinKind bkind, int param_count, const DataType *param_types) {
    TreeNode *fn = ast_new_dcl(DCL_FUNC, 0, 0);
    fn->name = pyc_strdup(name);
    fn->dcl_type = ret_type;

    TreeNode *phead = NULL;
    TreeNode *ptail = NULL;
    for (int i = 0; i < param_count; ++i) {
        TreeNode *p = ast_new_param(PARAM_VAR, 0, 0);
        char pname[16];
        snprintf(pname, sizeof(pname), "arg%d", i);
        p->name = pyc_strdup(pname);
        p->dcl_type = param_types[i];
        ast_append_sibling(&phead, &ptail, p);
    }
    ast_set_child(fn, 0, phead);

    /* Keep linked in builtin_nodes for clean destruction */
    fn->rSibling = a->builtin_nodes;
    a->builtin_nodes = fn;

    BucketList bk = st_insert_dcl(fn, a->global_scope);
    bk->is_builtin = true;
    bk->builtin_kind = bkind;
    return fn;
}

static void register_builtins(Analyzer *a) {
    DataType any_t[1] = { TYPE_UNKNOWN };
    add_builtin_func(a, "print",      TYPE_VOID,  BUILTIN_PRINT,      0, NULL);
    add_builtin_func(a, "println",    TYPE_VOID,  BUILTIN_PRINTLN,    0, NULL);
    add_builtin_func(a, "write",      TYPE_VOID,  BUILTIN_WRITE,      1, any_t);
    add_builtin_func(a, "read",       TYPE_INT,   BUILTIN_READ,       0, NULL);
    add_builtin_func(a, "read_int",   TYPE_INT,   BUILTIN_READ_INT,   0, NULL);
    add_builtin_func(a, "read_float", TYPE_FLOAT, BUILTIN_READ_FLOAT, 0, NULL);
    add_builtin_func(a, "len",        TYPE_INT,   BUILTIN_LEN,        1, any_t);
}

Analyzer *analyzer_create(TreeNode *parse_tree, DiagnosticList *diags) {
    Analyzer *a = (Analyzer *)pyc_calloc(1, sizeof(Analyzer));
    a->parse_tree = parse_tree;
    a->diags = diags;
    a->global_scope = st_initialize(true);
    register_builtins(a);
    return a;
}

void analyzer_destroy(Analyzer *analyzer) {
    if (!analyzer) return;
    st_free(analyzer->global_scope);
    ast_free(analyzer->builtin_nodes);
    free(analyzer);
}

SymbolTable *analyzer_get_symbol_table(const Analyzer *analyzer) {
    return analyzer ? analyzer->global_scope : NULL;
}

static void sem_error(Analyzer *a, const TreeNode *nd, const char *fmt, ...) {
    a->has_error = true;
    /* Forward formatted message to diag_add via buffer */
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    diag_add(a->diags, nd ? nd->line : 0, nd ? nd->col : 0, "%s", buf);
}

static void analyze_node(Analyzer *a, TreeNode *nd, SymbolTable *st);
static void analyze_list(Analyzer *a, TreeNode *head, SymbolTable *st);

static bool is_valid_lvalue(const TreeNode *nd) {
    if (!nd || nd->node_kind != NODE_EXPR) return false;
    if (nd->kind.expr == EXPR_INDEX) return true;
    if (nd->kind.expr == EXPR_ID) {
        LineList ref = (LineList)nd->sym_ref;
        if (!ref || !ref->bk || !ref->bk->nd) return false;
        const TreeNode *decl = ref->bk->nd;
        if (decl->node_kind == NODE_DCL && decl->kind.dcl == DCL_VAR) return true;
        if (decl->node_kind == NODE_PARAM && decl->kind.param == PARAM_VAR) return true;
    }
    return false;
}

static void check_builtin_call(Analyzer *a, TreeNode *call, BucketList bk) {
    TreeNode *arg = call->child[0];
    int argc = 0;
    for (TreeNode *cur = arg; cur; cur = cur->rSibling) {
        argc++;
    }

    switch (bk->builtin_kind) {
        case BUILTIN_PRINT:
            if (argc < 1) {
                sem_error(a, call, "built-in 'print' requires at least 1 argument");
            }
            for (TreeNode *cur = arg; cur; cur = cur->rSibling) {
                if (cur->expr_type == TYPE_VOID) {
                    sem_error(a, cur, "cannot pass void expression to 'print'");
                }
            }
            call->expr_type = TYPE_VOID;
            break;

        case BUILTIN_PRINTLN:
            for (TreeNode *cur = arg; cur; cur = cur->rSibling) {
                if (cur->expr_type == TYPE_VOID) {
                    sem_error(a, cur, "cannot pass void expression to 'println'");
                }
            }
            call->expr_type = TYPE_VOID;
            break;

        case BUILTIN_WRITE:
            if (argc != 1) {
                sem_error(a, call, "built-in 'write' expects 1 argument, got %d", argc);
            } else if (arg->expr_type == TYPE_VOID) {
                sem_error(a, arg, "cannot pass void expression to 'write'");
            }
            call->expr_type = TYPE_VOID;
            break;

        case BUILTIN_READ:
        case BUILTIN_READ_INT:
            if (argc != 0) {
                sem_error(a, call, "built-in '%s' takes no arguments, got %d", call->name, argc);
            }
            call->expr_type = TYPE_INT;
            break;

        case BUILTIN_READ_FLOAT:
            if (argc != 0) {
                sem_error(a, call, "built-in 'read_float' takes no arguments, got %d", argc);
            }
            call->expr_type = TYPE_FLOAT;
            break;

        case BUILTIN_LEN:
            if (argc != 1) {
                sem_error(a, call, "built-in 'len' expects 1 argument, got %d", argc);
            } else if (arg->expr_type != TYPE_ARRAY && arg->expr_type != TYPE_STRING && arg->expr_type != TYPE_ERROR) {
                sem_error(a, arg, "built-in 'len' expects array or string, got '%s'", data_type_name(arg->expr_type));
            }
            call->expr_type = TYPE_INT;
            break;

        default:
            call->expr_type = TYPE_ERROR;
            break;
    }
}

static void analyze_expr(Analyzer *a, TreeNode *nd, SymbolTable *st) {
    switch (nd->kind.expr) {
        case EXPR_INT:
            if (nd->expr_type != TYPE_ERROR) nd->expr_type = TYPE_INT;
            break;

        case EXPR_FLOAT:
            nd->expr_type = TYPE_FLOAT;
            break;

        case EXPR_STRING:
            nd->expr_type = TYPE_STRING;
            break;

        case EXPR_BOOL:
            nd->expr_type = TYPE_BOOL;
            break;

        case EXPR_ID: {
            BucketList bk = st_lookup(st, nd->name);
            if (!bk) {
                sem_error(a, nd, "undeclared identifier '%s'", nd->name);
                nd->expr_type = TYPE_ERROR;
                break;
            }
            st_insert_ref(nd, bk);
            const TreeNode *decl = bk->nd;
            if (decl->node_kind == NODE_DCL && decl->kind.dcl == DCL_FUNC) {
                sem_error(a, nd, "function '%s' cannot be used as a value without calling it", nd->name);
                nd->expr_type = TYPE_ERROR;
                break;
            }
            nd->expr_type = decl->dcl_type;
            nd->expr_elem_type = decl->elem_type;
            break;
        }

        case EXPR_INDEX: {
            TreeNode *arr = nd->child[0];
            TreeNode *idx = nd->child[1];
            analyze_node(a, arr, st);
            analyze_node(a, idx, st);

            if (idx->expr_type != TYPE_INT && idx->expr_type != TYPE_ERROR) {
                sem_error(a, idx, "array index must be 'int', got '%s'", data_type_name(idx->expr_type));
            }
            if (arr->expr_type == TYPE_ARRAY) {
                nd->expr_type = arr->expr_elem_type;
            } else if (arr->expr_type == TYPE_ERROR) {
                nd->expr_type = TYPE_ERROR;
            } else {
                sem_error(a, arr, "cannot index value of type '%s'", data_type_name(arr->expr_type));
                nd->expr_type = TYPE_ERROR;
            }
            break;
        }

        case EXPR_CALL: {
            analyze_list(a, nd->child[0], st);
            BucketList bk = st_lookup(st, nd->name);
            if (!bk) {
                sem_error(a, nd, "call to undeclared function '%s'", nd->name);
                nd->expr_type = TYPE_ERROR;
                break;
            }
            st_insert_ref(nd, bk);

            TreeNode *decl = bk->nd;
            if (!(decl->node_kind == NODE_DCL && decl->kind.dcl == DCL_FUNC)) {
                sem_error(a, nd, "'%s' is not a function", nd->name);
                nd->expr_type = TYPE_ERROR;
                break;
            }

            if (bk->is_builtin) {
                check_builtin_call(a, nd, bk);
                break;
            }

            /* Check parameter count and types */
            TreeNode *param = decl->child[0];
            TreeNode *arg = nd->child[0];
            int idx = 1;
            while (param && arg) {
                if (param->kind.param == PARAM_ARRAY) {
                    if (arg->expr_type != TYPE_ARRAY && arg->expr_type != TYPE_ERROR) {
                        sem_error(a, arg, "argument %d to '%s' expects '%s[]', got '%s'",
                                  idx, nd->name, data_type_name(param->elem_type), data_type_name(arg->expr_type));
                    } else if (arg->expr_type == TYPE_ARRAY && arg->expr_elem_type != param->elem_type) {
                        sem_error(a, arg, "argument %d to '%s' expects '%s[]', got '%s[]'",
                                  idx, nd->name, data_type_name(param->elem_type), data_type_name(arg->expr_elem_type));
                    }
                } else {
                    if (!is_assignable_type(param->dcl_type, arg->expr_type)) {
                        sem_error(a, arg, "argument %d to '%s' expects '%s', got '%s'",
                                  idx, nd->name, data_type_name(param->dcl_type), data_type_name(arg->expr_type));
                    }
                }
                param = param->rSibling;
                arg = arg->rSibling;
                idx++;
            }
            if (param || arg) {
                int expected = 0, got = 0;
                for (TreeNode *p = decl->child[0]; p; p = p->rSibling) expected++;
                for (TreeNode *g = nd->child[0]; g; g = g->rSibling) got++;
                sem_error(a, nd, "function '%s' expects %d argument(s), got %d", nd->name, expected, got);
            }

            nd->expr_type = (decl->dcl_type == TYPE_UNKNOWN) ? TYPE_INT : decl->dcl_type;
            break;
        }

        case EXPR_UNARY: {
            TreeNode *opnd = nd->child[0];
            analyze_node(a, opnd, st);
            DataType t = opnd->expr_type;
            if (t == TYPE_ERROR) {
                nd->expr_type = TYPE_ERROR;
                break;
            }
            if (nd->op == TOK_NOT) {
                if (!is_truthy_type(t)) {
                    sem_error(a, nd, "logical '!' requires bool or numeric operand, got '%s'", data_type_name(t));
                    nd->expr_type = TYPE_ERROR;
                } else {
                    nd->expr_type = TYPE_BOOL;
                }
            } else if (nd->op == TOK_PLUS_PLUS || nd->op == TOK_MINUS_MINUS) {
                if (!is_valid_lvalue(opnd)) {
                    sem_error(a, nd, "operand of '%s' must be an assignable variable or array element",
                              token_type_name(nd->op));
                    nd->expr_type = TYPE_ERROR;
                } else if (!is_numeric(t)) {
                    sem_error(a, nd, "operator '%s' requires numeric operand, got '%s'",
                              token_type_name(nd->op), data_type_name(t));
                    nd->expr_type = TYPE_ERROR;
                } else {
                    nd->expr_type = t;
                }
            } else { /* unary + / - */
                if (!is_numeric(t)) {
                    sem_error(a, nd, "unary '%s' requires numeric operand, got '%s'",
                              token_type_name(nd->op), data_type_name(t));
                    nd->expr_type = TYPE_ERROR;
                } else {
                    nd->expr_type = t;
                }
            }
            break;
        }

        case EXPR_POSTFIX: {
            TreeNode *opnd = nd->child[0];
            analyze_node(a, opnd, st);
            DataType t = opnd->expr_type;
            if (t == TYPE_ERROR) {
                nd->expr_type = TYPE_ERROR;
            } else if (!is_valid_lvalue(opnd)) {
                sem_error(a, nd, "operand of postfix '%s' must be an assignable variable or array element",
                          token_type_name(nd->op));
                nd->expr_type = TYPE_ERROR;
            } else if (!is_numeric(t)) {
                sem_error(a, nd, "postfix '%s' requires numeric operand, got '%s'",
                          token_type_name(nd->op), data_type_name(t));
                nd->expr_type = TYPE_ERROR;
            } else {
                nd->expr_type = t;
            }
            break;
        }

        case EXPR_ASSIGN: {
            TreeNode *lhs = nd->child[0];
            TreeNode *rhs = nd->child[1];
            analyze_node(a, lhs, st);
            analyze_node(a, rhs, st);

            if (lhs->expr_type == TYPE_ERROR || rhs->expr_type == TYPE_ERROR) {
                nd->expr_type = TYPE_ERROR;
                break;
            }
            if (!is_valid_lvalue(lhs)) {
                sem_error(a, lhs, "left-hand side of assignment is not assignable");
                nd->expr_type = TYPE_ERROR;
                break;
            }
            if (nd->op == TOK_ASSIGN) {
                if (!is_assignable_type(lhs->expr_type, rhs->expr_type)) {
                    sem_error(a, nd, "cannot assign '%s' to '%s'",
                              data_type_name(rhs->expr_type), data_type_name(lhs->expr_type));
                    nd->expr_type = TYPE_ERROR;
                } else {
                    nd->expr_type = lhs->expr_type;
                }
            } else if (nd->op == TOK_PERCENT_ASSIGN) {
                if (lhs->expr_type != TYPE_INT || rhs->expr_type != TYPE_INT) {
                    sem_error(a, nd, "operator '%%=' requires 'int' operands");
                    nd->expr_type = TYPE_ERROR;
                } else {
                    nd->expr_type = TYPE_INT;
                }
            } else if (nd->op == TOK_PLUS_ASSIGN && lhs->expr_type == TYPE_STRING && rhs->expr_type == TYPE_STRING) {
                nd->expr_type = TYPE_STRING;
            } else {
                if (!is_numeric(lhs->expr_type) || !is_numeric(rhs->expr_type)) {
                    sem_error(a, nd, "compound assignment '%s' requires numeric operands, got '%s' and '%s'",
                              token_type_name(nd->op), data_type_name(lhs->expr_type), data_type_name(rhs->expr_type));
                    nd->expr_type = TYPE_ERROR;
                } else if (lhs->expr_type == TYPE_INT && rhs->expr_type == TYPE_FLOAT) {
                    sem_error(a, nd, "cannot narrow 'float' into 'int' with '%s'", token_type_name(nd->op));
                    nd->expr_type = TYPE_ERROR;
                } else {
                    nd->expr_type = lhs->expr_type;
                }
            }
            break;
        }

        case EXPR_BINARY: {
            TreeNode *lhs = nd->child[0];
            TreeNode *rhs = nd->child[1];
            analyze_node(a, lhs, st);
            analyze_node(a, rhs, st);

            DataType lt = lhs->expr_type;
            DataType rt = rhs->expr_type;
            if (lt == TYPE_ERROR || rt == TYPE_ERROR) {
                nd->expr_type = TYPE_ERROR;
                break;
            }

            switch (nd->op) {
                case TOK_PLUS:
                    if (lt == TYPE_STRING && rt == TYPE_STRING) {
                        nd->expr_type = TYPE_STRING;
                    } else if (is_numeric(lt) && is_numeric(rt)) {
                        nd->expr_type = (lt == TYPE_FLOAT || rt == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
                    } else {
                        sem_error(a, nd, "operator '+' incompatible with '%s' and '%s'",
                                  data_type_name(lt), data_type_name(rt));
                        nd->expr_type = TYPE_ERROR;
                    }
                    break;

                case TOK_MINUS:
                case TOK_STAR:
                case TOK_SLASH:
                    if (is_numeric(lt) && is_numeric(rt)) {
                        nd->expr_type = (lt == TYPE_FLOAT || rt == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
                    } else {
                        sem_error(a, nd, "arithmetic operator '%s' requires numeric operands, got '%s' and '%s'",
                                  token_type_name(nd->op), data_type_name(lt), data_type_name(rt));
                        nd->expr_type = TYPE_ERROR;
                    }
                    break;

                case TOK_PERCENT:
                    if (lt == TYPE_INT && rt == TYPE_INT) {
                        nd->expr_type = TYPE_INT;
                    } else {
                        sem_error(a, nd, "operator '%%' requires 'int' operands, got '%s' and '%s'",
                                  data_type_name(lt), data_type_name(rt));
                        nd->expr_type = TYPE_ERROR;
                    }
                    break;

                case TOK_LT:
                case TOK_LTE:
                case TOK_GT:
                case TOK_GTE:
                    if ((is_numeric(lt) && is_numeric(rt)) || (lt == TYPE_STRING && rt == TYPE_STRING)) {
                        nd->expr_type = TYPE_BOOL;
                    } else {
                        sem_error(a, nd, "relational operator '%s' incompatible with '%s' and '%s'",
                                  token_type_name(nd->op), data_type_name(lt), data_type_name(rt));
                        nd->expr_type = TYPE_ERROR;
                    }
                    break;

                case TOK_EQ:
                case TOK_NEQ:
                    if (lt == rt || (is_numeric(lt) && is_numeric(rt))) {
                        nd->expr_type = TYPE_BOOL;
                    } else {
                        sem_error(a, nd, "equality operator '%s' incompatible with '%s' and '%s'",
                                  token_type_name(nd->op), data_type_name(lt), data_type_name(rt));
                        nd->expr_type = TYPE_ERROR;
                    }
                    break;

                case TOK_AND:
                case TOK_OR:
                    if (is_truthy_type(lt) && is_truthy_type(rt)) {
                        nd->expr_type = TYPE_BOOL;
                    } else {
                        sem_error(a, nd, "logical operator '%s' incompatible with '%s' and '%s'",
                                  token_type_name(nd->op), data_type_name(lt), data_type_name(rt));
                        nd->expr_type = TYPE_ERROR;
                    }
                    break;

                default:
                    nd->expr_type = TYPE_ERROR;
                    break;
            }
            break;
        }
    }
}

static void analyze_dcl(Analyzer *a, TreeNode *nd, SymbolTable *st) {
    switch (nd->kind.dcl) {
        case DCL_VAR: {
            if (nd->dcl_type == TYPE_VOID) {
                sem_error(a, nd, "variable '%s' cannot have type 'void'", nd->name);
            }
            if (nd->child[0]) {
                analyze_node(a, nd->child[0], st);
                if (!is_assignable_type(nd->dcl_type, nd->child[0]->expr_type)) {
                    sem_error(a, nd, "cannot initialize '%s' variable '%s' with '%s'",
                              data_type_name(nd->dcl_type), nd->name,
                              data_type_name(nd->child[0]->expr_type));
                }
            }
            if (st_lookup_local(st, nd->name)) {
                sem_error(a, nd, "redeclaration of '%s' in the same scope", nd->name);
            } else {
                st_insert_dcl(nd, st);
            }
            break;
        }

        case DCL_ARRAY: {
            if (nd->elem_type == TYPE_VOID) {
                sem_error(a, nd, "array '%s' cannot have element type 'void'", nd->name);
            }
            if (nd->child[0]) {
                analyze_node(a, nd->child[0], st);
                if (nd->child[0]->expr_type != TYPE_INT && nd->child[0]->expr_type != TYPE_ERROR) {
                    sem_error(a, nd->child[0], "array size must be 'int', got '%s'",
                              data_type_name(nd->child[0]->expr_type));
                } else if (nd->child[0]->node_kind == NODE_EXPR &&
                           nd->child[0]->kind.expr == EXPR_INT &&
                           nd->child[0]->lit.int_val <= 0) {
                    sem_error(a, nd->child[0], "array '%s' size must be positive", nd->name);
                }
            }
            if (st_lookup_local(st, nd->name)) {
                sem_error(a, nd, "redeclaration of '%s' in the same scope", nd->name);
            } else {
                st_insert_dcl(nd, st);
            }
            break;
        }

        case DCL_FUNC: {
            /* Top-level functions are pre-registered in pass 1; nested functions register here */
            if (!nd->sym_ref) {
                if (st_lookup_local(st, nd->name)) {
                    sem_error(a, nd, "redeclaration of function '%s'", nd->name);
                } else {
                    st_insert_dcl(nd, st);
                }
            }

            SymbolTable *fn_scope = st_attach(st);
            fn_scope->nd = nd;
            nd->scope_ref = fn_scope;

            for (TreeNode *p = nd->child[0]; p; p = p->rSibling) {
                if (p->dcl_type == TYPE_VOID || (p->kind.param == PARAM_ARRAY && p->elem_type == TYPE_VOID)) {
                    sem_error(a, p, "parameter '%s' cannot have type 'void'", p->name);
                }
                if (st_lookup_local(fn_scope, p->name)) {
                    sem_error(a, p, "duplicate parameter name '%s' in function '%s'", p->name, nd->name);
                } else {
                    st_insert_dcl(p, fn_scope);
                }
            }

            TreeNode *prev_func = a->current_func;
            int prev_loop_depth = a->loop_depth;
            a->current_func = nd;
            a->loop_depth = 0;

            TreeNode *body = nd->child[1];
            if (body) {
                if (body->node_kind == NODE_STMT && body->kind.stmt == STMT_COMPOUND) {
                    body->scope_ref = fn_scope;
                    analyze_list(a, body->child[0], fn_scope);
                } else {
                    analyze_node(a, body, fn_scope);
                }
            }

            /* If Python 'def' omitted return type and had no return expr, finalize as void */
            if (nd->dcl_type == TYPE_UNKNOWN) {
                nd->dcl_type = TYPE_VOID;
            }

            a->current_func = prev_func;
            a->loop_depth = prev_loop_depth;
            break;
        }
    }
}

static void analyze_stmt(Analyzer *a, TreeNode *nd, SymbolTable *st) {
    switch (nd->kind.stmt) {
        case STMT_COMPOUND: {
            SymbolTable *blk_scope = st_attach(st);
            blk_scope->nd = nd;
            nd->scope_ref = blk_scope;
            analyze_list(a, nd->child[0], blk_scope);
            break;
        }

        case STMT_IF: {
            analyze_node(a, nd->child[0], st);
            if (nd->child[0] && nd->child[0]->expr_type != TYPE_ERROR &&
                !is_truthy_type(nd->child[0]->expr_type)) {
                sem_error(a, nd->child[0], "if condition must be bool or numeric, got '%s'",
                          data_type_name(nd->child[0]->expr_type));
            }
            analyze_node(a, nd->child[1], st);
            analyze_node(a, nd->child[2], st);
            break;
        }

        case STMT_WHILE: {
            analyze_node(a, nd->child[0], st);
            if (nd->child[0] && nd->child[0]->expr_type != TYPE_ERROR &&
                !is_truthy_type(nd->child[0]->expr_type)) {
                sem_error(a, nd->child[0], "while condition must be bool or numeric, got '%s'",
                          data_type_name(nd->child[0]->expr_type));
            }
            a->loop_depth++;
            analyze_node(a, nd->child[1], st);
            a->loop_depth--;
            break;
        }

        case STMT_DO_WHILE: {
            a->loop_depth++;
            analyze_node(a, nd->child[0], st);
            a->loop_depth--;
            analyze_node(a, nd->child[1], st);
            if (nd->child[1] && nd->child[1]->expr_type != TYPE_ERROR &&
                !is_truthy_type(nd->child[1]->expr_type)) {
                sem_error(a, nd->child[1], "do-while condition must be bool or numeric, got '%s'",
                          data_type_name(nd->child[1]->expr_type));
            }
            break;
        }

        case STMT_FOR: {
            SymbolTable *for_scope = st_attach(st);
            for_scope->nd = nd;
            nd->scope_ref = for_scope;

            analyze_list(a, nd->child[0], for_scope);
            if (nd->child[1]) {
                analyze_node(a, nd->child[1], for_scope);
                if (nd->child[1]->expr_type != TYPE_ERROR && !is_truthy_type(nd->child[1]->expr_type)) {
                    sem_error(a, nd->child[1], "for condition must be bool or numeric, got '%s'",
                              data_type_name(nd->child[1]->expr_type));
                }
            }
            analyze_node(a, nd->child[2], for_scope);

            a->loop_depth++;
            TreeNode *body = nd->child[3];
            if (body) {
                if (body->node_kind == NODE_STMT && body->kind.stmt == STMT_COMPOUND) {
                    body->scope_ref = for_scope;
                    analyze_list(a, body->child[0], for_scope);
                } else {
                    analyze_node(a, body, for_scope);
                }
            }
            a->loop_depth--;
            break;
        }

        case STMT_RETURN: {
            if (!a->current_func) {
                sem_error(a, nd, "'return' statement outside of a function");
                if (nd->child[0]) analyze_node(a, nd->child[0], st);
                break;
            }
            TreeNode *val = nd->child[0];
            if (val) {
                analyze_node(a, val, st);
            }

            DataType expected = a->current_func->dcl_type;
            if (expected == TYPE_UNKNOWN) {
                a->current_func->dcl_type = val ? val->expr_type : TYPE_VOID;
            } else if (expected == TYPE_VOID) {
                if (val) {
                    sem_error(a, nd, "void function '%s' should not return a value", a->current_func->name);
                }
            } else {
                if (!val) {
                    sem_error(a, nd, "non-void function '%s' must return a '%s' value",
                              a->current_func->name, data_type_name(expected));
                } else if (!is_assignable_type(expected, val->expr_type)) {
                    sem_error(a, val, "return type mismatch in '%s': expected '%s', got '%s'",
                              a->current_func->name, data_type_name(expected), data_type_name(val->expr_type));
                }
            }
            break;
        }

        case STMT_BREAK:
            if (a->loop_depth == 0) {
                sem_error(a, nd, "'break' statement outside of a loop");
            }
            break;

        case STMT_CONTINUE:
            if (a->loop_depth == 0) {
                sem_error(a, nd, "'continue' statement outside of a loop");
            }
            break;

        case STMT_EXPR:
            analyze_node(a, nd->child[0], st);
            break;

        case STMT_EMPTY:
            break;
    }
}

static void analyze_node(Analyzer *a, TreeNode *nd, SymbolTable *st) {
    if (!nd) return;
    switch (nd->node_kind) {
        case NODE_DCL:
            analyze_dcl(a, nd, st);
            break;
        case NODE_STMT:
            analyze_stmt(a, nd, st);
            break;
        case NODE_EXPR:
            analyze_expr(a, nd, st);
            break;
        case NODE_PARAM:
            break;
    }
}

static void analyze_list(Analyzer *a, TreeNode *head, SymbolTable *st) {
    for (TreeNode *cur = head; cur != NULL; cur = cur->rSibling) {
        analyze_node(a, cur, st);
    }
}

bool analyzer_run(Analyzer *a) {
    if (!a) return false;

    /* Pass 1: pre-register top-level functions so forward/mutual recursion works */
    for (TreeNode *cur = a->parse_tree; cur != NULL; cur = cur->rSibling) {
        if (cur->node_kind == NODE_DCL && cur->kind.dcl == DCL_FUNC) {
            if (st_lookup_local(a->global_scope, cur->name)) {
                sem_error(a, cur, "redeclaration of function '%s'", cur->name);
            } else {
                st_insert_dcl(cur, a->global_scope);
            }
        }
    }

    /* Pass 2: analyze all top-level items (declarations, functions, statements) */
    analyze_list(a, a->parse_tree, a->global_scope);

    return !a->has_error;
}
