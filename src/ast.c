#include "ast.h"

const char *data_type_name(DataType type) {
    switch (type) {
        case TYPE_VOID:    return "void";
        case TYPE_INT:     return "int";
        case TYPE_FLOAT:   return "float";
        case TYPE_STRING:  return "string";
        case TYPE_BOOL:    return "bool";
        case TYPE_ARRAY:   return "array";
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_ERROR:   return "error";
        default:           return "?";
    }
}

static TreeNode *ast_alloc_node(NodeKind nk, int line, int col) {
    TreeNode *n = (TreeNode *)pyc_calloc(1, sizeof(TreeNode));
    n->node_kind = nk;
    n->line = line;
    n->col = col;
    n->dcl_type = TYPE_UNKNOWN;
    n->elem_type = TYPE_UNKNOWN;
    n->expr_type = TYPE_UNKNOWN;
    n->expr_elem_type = TYPE_UNKNOWN;
    return n;
}

TreeNode *ast_new_dcl(DclKind kind, int line, int col) {
    TreeNode *n = ast_alloc_node(NODE_DCL, line, col);
    n->kind.dcl = kind;
    return n;
}

TreeNode *ast_new_param(ParamKind kind, int line, int col) {
    TreeNode *n = ast_alloc_node(NODE_PARAM, line, col);
    n->kind.param = kind;
    return n;
}

TreeNode *ast_new_stmt(StmtKind kind, int line, int col) {
    TreeNode *n = ast_alloc_node(NODE_STMT, line, col);
    n->kind.stmt = kind;
    return n;
}

TreeNode *ast_new_expr(ExprKind kind, int line, int col) {
    TreeNode *n = ast_alloc_node(NODE_EXPR, line, col);
    n->kind.expr = kind;
    return n;
}

void ast_append_sibling(TreeNode **head, TreeNode **tail, TreeNode *node) {
    if (!node) return;
    if (!*head) {
        *head = node;
        TreeNode *cur = node;
        while (cur->rSibling) cur = cur->rSibling;
        *tail = cur;
    } else {
        (*tail)->rSibling = node;
        node->lSibling = *tail;
        TreeNode *cur = node;
        while (cur->rSibling) cur = cur->rSibling;
        *tail = cur;
    }
}

void ast_set_child(TreeNode *parent, int index, TreeNode *child) {
    if (!parent || index < 0 || index >= MAX_CHILDREN) return;
    parent->child[index] = child;
    TreeNode *cur = child;
    while (cur) {
        cur->parent = parent;
        cur = cur->rSibling;
    }
}

void ast_free(TreeNode *root) {
    while (root) {
        TreeNode *next = root->rSibling;
        for (int i = 0; i < MAX_CHILDREN; ++i) {
            ast_free(root->child[i]);
        }
        free(root->name);
        if (root->node_kind == NODE_EXPR && root->kind.expr == EXPR_STRING) {
            free(root->lit.str_val);
        }
        free(root);
        root = next;
    }
}

static void print_indent(int depth, FILE *out) {
    for (int i = 0; i < depth; ++i) {
        fprintf(out, "  ");
    }
}

static void ast_print_rec(const TreeNode *node, int depth, FILE *out) {
    for (const TreeNode *cur = node; cur != NULL; cur = cur->rSibling) {
        print_indent(depth, out);
        switch (cur->node_kind) {
            case NODE_DCL:
                switch (cur->kind.dcl) {
                    case DCL_VAR:
                        fprintf(out, "VarDecl(%s %s) [line %d]\n",
                                data_type_name(cur->dcl_type),
                                cur->name ? cur->name : "?",
                                cur->line);
                        break;
                    case DCL_ARRAY:
                        fprintf(out, "ArrayDecl(%s %s[]) [line %d]\n",
                                data_type_name(cur->elem_type),
                                cur->name ? cur->name : "?",
                                cur->line);
                        break;
                    case DCL_FUNC:
                        fprintf(out, "FuncDecl(%s -> %s) [line %d]\n",
                                cur->name ? cur->name : "?",
                                data_type_name(cur->dcl_type),
                                cur->line);
                        break;
                }
                break;

            case NODE_PARAM:
                if (cur->kind.param == PARAM_ARRAY) {
                    fprintf(out, "Param(%s %s[]) [line %d]\n",
                            data_type_name(cur->elem_type),
                            cur->name ? cur->name : "?",
                            cur->line);
                } else {
                    fprintf(out, "Param(%s %s) [line %d]\n",
                            data_type_name(cur->dcl_type),
                            cur->name ? cur->name : "?",
                            cur->line);
                }
                break;

            case NODE_STMT:
                switch (cur->kind.stmt) {
                    case STMT_COMPOUND: fprintf(out, "Block [line %d]\n", cur->line); break;
                    case STMT_IF:       fprintf(out, "If%s [line %d]\n", cur->child[2] ? " (with else)" : "", cur->line); break;
                    case STMT_WHILE:    fprintf(out, "While [line %d]\n", cur->line); break;
                    case STMT_DO_WHILE: fprintf(out, "DoWhile [line %d]\n", cur->line); break;
                    case STMT_FOR:      fprintf(out, "For [line %d]\n", cur->line); break;
                    case STMT_RETURN:   fprintf(out, "Return [line %d]\n", cur->line); break;
                    case STMT_BREAK:    fprintf(out, "Break [line %d]\n", cur->line); break;
                    case STMT_CONTINUE: fprintf(out, "Continue [line %d]\n", cur->line); break;
                    case STMT_EXPR:     fprintf(out, "ExprStmt [line %d]\n", cur->line); break;
                    case STMT_EMPTY:    fprintf(out, "EmptyStmt [line %d]\n", cur->line); break;
                }
                break;

            case NODE_EXPR:
                switch (cur->kind.expr) {
                    case EXPR_BINARY:
                        fprintf(out, "BinaryOp(%s) : %s [line %d]\n",
                                token_type_name(cur->op), data_type_name(cur->expr_type), cur->line);
                        break;
                    case EXPR_UNARY:
                        fprintf(out, "UnaryOp(%s) : %s [line %d]\n",
                                token_type_name(cur->op), data_type_name(cur->expr_type), cur->line);
                        break;
                    case EXPR_POSTFIX:
                        fprintf(out, "PostfixOp(%s) : %s [line %d]\n",
                                token_type_name(cur->op), data_type_name(cur->expr_type), cur->line);
                        break;
                    case EXPR_ASSIGN:
                        fprintf(out, "Assign(%s) : %s [line %d]\n",
                                token_type_name(cur->op), data_type_name(cur->expr_type), cur->line);
                        break;
                    case EXPR_INT:
                        fprintf(out, "Int(%lld)\n", (long long)cur->lit.int_val);
                        break;
                    case EXPR_FLOAT:
                        fprintf(out, "Float(%g)\n", cur->lit.float_val);
                        break;
                    case EXPR_STRING:
                        fprintf(out, "String(\"%s\")\n", cur->lit.str_val ? cur->lit.str_val : "");
                        break;
                    case EXPR_BOOL:
                        fprintf(out, "Bool(%s)\n", cur->lit.bool_val ? "true" : "false");
                        break;
                    case EXPR_ID:
                        fprintf(out, "Id(%s) : %s\n",
                                cur->name ? cur->name : "?", data_type_name(cur->expr_type));
                        break;
                    case EXPR_INDEX:
                        fprintf(out, "Index : %s [line %d]\n",
                                data_type_name(cur->expr_type), cur->line);
                        break;
                    case EXPR_CALL:
                        fprintf(out, "Call(%s) : %s [line %d]\n",
                                cur->name ? cur->name : "?", data_type_name(cur->expr_type), cur->line);
                        break;
                }
                break;
        }

        for (int i = 0; i < MAX_CHILDREN; ++i) {
            if (cur->child[i]) {
                ast_print_rec(cur->child[i], depth + 1, out);
            }
        }
    }
}

void ast_print(const TreeNode *root, FILE *out) {
    if (!root) {
        fprintf(out, "(empty AST)\n");
        return;
    }
    ast_print_rec(root, 0, out);
}
