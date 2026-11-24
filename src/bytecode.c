#include "bytecode.h"

static const char *const kOpNames[] = {
    "CONST", "POP", "DUP", "DUP2",
    "LOAD_GLOBAL", "STORE_GLOBAL", "LOAD_LOCAL", "STORE_LOCAL",
    "NEW_ARRAY", "ARRAY_GET", "ARRAY_SET",
    "ADD", "SUB", "MUL", "DIV", "MOD", "NEG",
    "EQ", "NEQ", "LT", "LTE", "GT", "GTE", "NOT",
    "CAST_FLOAT", "CAST_BOOL",
    "JMP", "JMP_IF_FALSE", "JMP_IF_FALSE_PEEK", "JMP_IF_TRUE_PEEK",
    "CALL", "CALL_BUILTIN", "RET", "HALT"
};

const char *opcode_name(OpCode op) {
    size_t idx = (size_t)op;
    size_t n = sizeof(kOpNames) / sizeof(kOpNames[0]);
    return idx < n ? kOpNames[idx] : "UNKNOWN";
}

#define MAX_LOOP_DEPTH 64
#define MAX_PATCHES 256

typedef struct {
    int break_patches[MAX_PATCHES];
    int break_count;
    int continue_patches[MAX_PATCHES];
    int continue_count;
} LoopContext;

typedef struct {
    BytecodeProgram *prog;
    DiagnosticList *diags;
    bool in_function;
    int current_local_count;
    DataType current_ret_type;
    LoopContext loops[MAX_LOOP_DEPTH];
    int loop_depth;
} Compiler;

static int emit(Compiler *c, OpCode op, int32_t arg1, int32_t arg2, int line) {
    BytecodeProgram *p = c->prog;
    if (p->code_count == p->code_cap) {
        size_t cap = p->code_cap == 0 ? 128 : p->code_cap * 2;
        p->code = (Instruction *)pyc_realloc(p->code, cap * sizeof(Instruction));
        p->code_cap = cap;
    }
    int idx = (int)p->code_count++;
    p->code[idx].op = op;
    p->code[idx].arg1 = arg1;
    p->code[idx].arg2 = arg2;
    p->code[idx].line = line;
    return idx;
}

static int add_const(Compiler *c, Constant k) {
    BytecodeProgram *p = c->prog;
    /* Deduplicate simple constants */
    for (size_t i = 0; i < p->const_count; ++i) {
        if (p->constants[i].kind != k.kind) continue;
        switch (k.kind) {
            case CONST_VOID:   return (int)i;
            case CONST_INT:    if (p->constants[i].as.int_val == k.as.int_val) return (int)i; break;
            case CONST_FLOAT:  if (p->constants[i].as.float_val == k.as.float_val) return (int)i; break;
            case CONST_BOOL:   if (p->constants[i].as.bool_val == k.as.bool_val) return (int)i; break;
            case CONST_STRING: if (strcmp(p->constants[i].as.str_val, k.as.str_val) == 0) {
                                   free(k.as.str_val);
                                   return (int)i;
                               }
                               break;
        }
    }
    if (p->const_count == p->const_cap) {
        size_t cap = p->const_cap == 0 ? 32 : p->const_cap * 2;
        p->constants = (Constant *)pyc_realloc(p->constants, cap * sizeof(Constant));
        p->const_cap = cap;
    }
    int idx = (int)p->const_count++;
    p->constants[idx] = k;
    return idx;
}

static int const_void(Compiler *c) {
    Constant k;
    k.kind = CONST_VOID;
    k.as.int_val = 0;
    return add_const(c, k);
}

static int const_int(Compiler *c, int64_t v) {
    Constant k;
    k.kind = CONST_INT;
    k.as.int_val = v;
    return add_const(c, k);
}

static int const_float(Compiler *c, double v) {
    Constant k;
    k.kind = CONST_FLOAT;
    k.as.float_val = v;
    return add_const(c, k);
}

static int const_bool(Compiler *c, bool v) {
    Constant k;
    k.kind = CONST_BOOL;
    k.as.bool_val = v;
    return add_const(c, k);
}

static int const_string(Compiler *c, const char *s) {
    Constant k;
    k.kind = CONST_STRING;
    k.as.str_val = pyc_strdup(s ? s : "");
    return add_const(c, k);
}

static void emit_default_value(Compiler *c, DataType t, int line) {
    switch (t) {
        case TYPE_INT:    emit(c, OP_CONST, const_int(c, 0), 0, line); break;
        case TYPE_FLOAT:  emit(c, OP_CONST, const_float(c, 0.0), 0, line); break;
        case TYPE_BOOL:   emit(c, OP_CONST, const_bool(c, false), 0, line); break;
        case TYPE_STRING: emit(c, OP_CONST, const_string(c, ""), 0, line); break;
        default:          emit(c, OP_CONST, const_void(c), 0, line); break;
    }
}

static void emit_cast_if_needed(Compiler *c, DataType target, DataType actual, int line) {
    if (target == TYPE_FLOAT && actual == TYPE_INT) {
        emit(c, OP_CAST_FLOAT, 0, 0, line);
    } else if (target == TYPE_BOOL && actual != TYPE_BOOL) {
        emit(c, OP_CAST_BOOL, 0, 0, line);
    }
}

static void patch_jump(Compiler *c, int instr_idx, int target_pc) {
    c->prog->code[instr_idx].arg1 = target_pc;
}

static void push_loop(Compiler *c) {
    if (c->loop_depth >= MAX_LOOP_DEPTH) {
        diag_add(c->diags, 0, 0, "loop nesting depth exceeded limit (%d)", MAX_LOOP_DEPTH);
        return;
    }
    c->loops[c->loop_depth].break_count = 0;
    c->loops[c->loop_depth].continue_count = 0;
    c->loop_depth++;
}

static void pop_loop(Compiler *c, int continue_target, int break_target) {
    if (c->loop_depth <= 0) return;
    LoopContext *ctx = &c->loops[--c->loop_depth];
    for (int i = 0; i < ctx->continue_count; ++i) {
        patch_jump(c, ctx->continue_patches[i], continue_target);
    }
    for (int i = 0; i < ctx->break_count; ++i) {
        patch_jump(c, ctx->break_patches[i], break_target);
    }
}

static void compile_node(Compiler *c, TreeNode *nd);
static void compile_list(Compiler *c, TreeNode *head);
static void compile_expr(Compiler *c, TreeNode *nd);

static BucketList get_ref_bucket(const TreeNode *nd) {
    if (!nd || !nd->sym_ref) return NULL;
    LineList ref = (LineList)nd->sym_ref;
    return ref->bk;
}

static BucketList get_dcl_bucket(const TreeNode *nd) {
    if (!nd || !nd->sym_ref) return NULL;
    return (BucketList)nd->sym_ref;
}

static void emit_load_var(Compiler *c, BucketList bk, int line) {
    if (bk->is_global) {
        emit(c, OP_LOAD_GLOBAL, bk->slot, 0, line);
    } else {
        emit(c, OP_LOAD_LOCAL, bk->slot, 0, line);
    }
}

static void emit_store_var(Compiler *c, BucketList bk, int line) {
    if (bk->is_global) {
        emit(c, OP_STORE_GLOBAL, bk->slot, 0, line);
    } else {
        emit(c, OP_STORE_LOCAL, bk->slot, 0, line);
    }
}

static void emit_compound_arith(Compiler *c, TokenType assign_op, int line) {
    switch (assign_op) {
        case TOK_PLUS_ASSIGN:    emit(c, OP_ADD, 0, 0, line); break;
        case TOK_MINUS_ASSIGN:   emit(c, OP_SUB, 0, 0, line); break;
        case TOK_STAR_ASSIGN:    emit(c, OP_MUL, 0, 0, line); break;
        case TOK_SLASH_ASSIGN:   emit(c, OP_DIV, 0, 0, line); break;
        case TOK_PERCENT_ASSIGN: emit(c, OP_MOD, 0, 0, line); break;
        default: break;
    }
}

static void compile_expr(Compiler *c, TreeNode *nd) {
    if (!nd) return;

    switch (nd->kind.expr) {
        case EXPR_INT:
            emit(c, OP_CONST, const_int(c, nd->lit.int_val), 0, nd->line);
            break;

        case EXPR_FLOAT:
            emit(c, OP_CONST, const_float(c, nd->lit.float_val), 0, nd->line);
            break;

        case EXPR_STRING:
            emit(c, OP_CONST, const_string(c, nd->lit.str_val), 0, nd->line);
            break;

        case EXPR_BOOL:
            emit(c, OP_CONST, const_bool(c, nd->lit.bool_val), 0, nd->line);
            break;

        case EXPR_ID: {
            BucketList bk = get_ref_bucket(nd);
            emit_load_var(c, bk, nd->line);
            break;
        }

        case EXPR_INDEX:
            compile_expr(c, nd->child[0]);
            compile_expr(c, nd->child[1]);
            emit(c, OP_ARRAY_GET, 0, 0, nd->line);
            break;

        case EXPR_UNARY: {
            TreeNode *opnd = nd->child[0];
            if (nd->op == TOK_MINUS) {
                compile_expr(c, opnd);
                emit(c, OP_NEG, 0, 0, nd->line);
            } else if (nd->op == TOK_PLUS) {
                compile_expr(c, opnd);
            } else if (nd->op == TOK_NOT) {
                compile_expr(c, opnd);
                emit(c, OP_NOT, 0, 0, nd->line);
            } else if (nd->op == TOK_PLUS_PLUS || nd->op == TOK_MINUS_MINUS) {
                OpCode aop = (nd->op == TOK_PLUS_PLUS) ? OP_ADD : OP_SUB;
                int one = (opnd->expr_type == TYPE_FLOAT) ? const_float(c, 1.0) : const_int(c, 1);
                if (opnd->kind.expr == EXPR_ID) {
                    BucketList bk = get_ref_bucket(opnd);
                    emit_load_var(c, bk, nd->line);
                    emit(c, OP_CONST, one, 0, nd->line);
                    emit(c, aop, 0, 0, nd->line);
                    emit_store_var(c, bk, nd->line);
                } else if (opnd->kind.expr == EXPR_INDEX) {
                    compile_expr(c, opnd->child[0]);
                    compile_expr(c, opnd->child[1]);
                    emit(c, OP_DUP2, 0, 0, nd->line);
                    emit(c, OP_ARRAY_GET, 0, 0, nd->line);
                    emit(c, OP_CONST, one, 0, nd->line);
                    emit(c, aop, 0, 0, nd->line);
                    emit(c, OP_ARRAY_SET, 0, 0, nd->line);
                }
            }
            break;
        }

        case EXPR_POSTFIX: {
            TreeNode *opnd = nd->child[0];
            OpCode aop = (nd->op == TOK_PLUS_PLUS) ? OP_ADD : OP_SUB;
            int one = (opnd->expr_type == TYPE_FLOAT) ? const_float(c, 1.0) : const_int(c, 1);
            if (opnd->kind.expr == EXPR_ID) {
                BucketList bk = get_ref_bucket(opnd);
                emit_load_var(c, bk, nd->line);      /* push old value (expression result) */
                emit(c, OP_DUP, 0, 0, nd->line);     /* duplicate old value */
                emit(c, OP_CONST, one, 0, nd->line);
                emit(c, aop, 0, 0, nd->line);
                emit_store_var(c, bk, nd->line);     /* store new value */
                emit(c, OP_POP, 0, 0, nd->line);     /* discard new value, keep old */
            } else if (opnd->kind.expr == EXPR_INDEX) {
                /* Evaluate old value, update in-place, leave old value on stack */
                int tmp_slot = c->current_local_count++;
                compile_expr(c, opnd->child[0]);
                compile_expr(c, opnd->child[1]);
                emit(c, OP_DUP2, 0, 0, nd->line);
                emit(c, OP_ARRAY_GET, 0, 0, nd->line);
                emit(c, OP_STORE_LOCAL, tmp_slot, 0, nd->line);
                emit(c, OP_CONST, one, 0, nd->line);
                emit(c, aop, 0, 0, nd->line);
                emit(c, OP_ARRAY_SET, 0, 0, nd->line);
                emit(c, OP_POP, 0, 0, nd->line);
                emit(c, OP_LOAD_LOCAL, tmp_slot, 0, nd->line);
            }
            break;
        }

        case EXPR_ASSIGN: {
            TreeNode *lhs = nd->child[0];
            TreeNode *rhs = nd->child[1];
            if (lhs->kind.expr == EXPR_ID) {
                BucketList bk = get_ref_bucket(lhs);
                if (nd->op == TOK_ASSIGN) {
                    compile_expr(c, rhs);
                    emit_cast_if_needed(c, lhs->expr_type, rhs->expr_type, nd->line);
                    emit_store_var(c, bk, nd->line);
                } else {
                    emit_load_var(c, bk, nd->line);
                    compile_expr(c, rhs);
                    emit_compound_arith(c, nd->op, nd->line);
                    emit_cast_if_needed(c, lhs->expr_type, nd->expr_type, nd->line);
                    emit_store_var(c, bk, nd->line);
                }
            } else if (lhs->kind.expr == EXPR_INDEX) {
                if (nd->op == TOK_ASSIGN) {
                    compile_expr(c, lhs->child[0]);
                    compile_expr(c, lhs->child[1]);
                    compile_expr(c, rhs);
                    emit_cast_if_needed(c, lhs->expr_type, rhs->expr_type, nd->line);
                    emit(c, OP_ARRAY_SET, 0, 0, nd->line);
                } else {
                    compile_expr(c, lhs->child[0]);
                    compile_expr(c, lhs->child[1]);
                    emit(c, OP_DUP2, 0, 0, nd->line);
                    emit(c, OP_ARRAY_GET, 0, 0, nd->line);
                    compile_expr(c, rhs);
                    emit_compound_arith(c, nd->op, nd->line);
                    emit_cast_if_needed(c, lhs->expr_type, nd->expr_type, nd->line);
                    emit(c, OP_ARRAY_SET, 0, 0, nd->line);
                }
            }
            break;
        }

        case EXPR_BINARY: {
            /* Short-circuit logical operators */
            if (nd->op == TOK_AND) {
                compile_expr(c, nd->child[0]);
                int skip = emit(c, OP_JMP_IF_FALSE_PEEK, 0, 0, nd->line);
                emit(c, OP_POP, 0, 0, nd->line);
                compile_expr(c, nd->child[1]);
                patch_jump(c, skip, (int)c->prog->code_count);
                emit(c, OP_CAST_BOOL, 0, 0, nd->line);
                break;
            }
            if (nd->op == TOK_OR) {
                compile_expr(c, nd->child[0]);
                int skip = emit(c, OP_JMP_IF_TRUE_PEEK, 0, 0, nd->line);
                emit(c, OP_POP, 0, 0, nd->line);
                compile_expr(c, nd->child[1]);
                patch_jump(c, skip, (int)c->prog->code_count);
                emit(c, OP_CAST_BOOL, 0, 0, nd->line);
                break;
            }

            compile_expr(c, nd->child[0]);
            compile_expr(c, nd->child[1]);
            switch (nd->op) {
                case TOK_PLUS:    emit(c, OP_ADD, 0, 0, nd->line); break;
                case TOK_MINUS:   emit(c, OP_SUB, 0, 0, nd->line); break;
                case TOK_STAR:    emit(c, OP_MUL, 0, 0, nd->line); break;
                case TOK_SLASH:   emit(c, OP_DIV, 0, 0, nd->line); break;
                case TOK_PERCENT: emit(c, OP_MOD, 0, 0, nd->line); break;
                case TOK_EQ:      emit(c, OP_EQ, 0, 0, nd->line); break;
                case TOK_NEQ:     emit(c, OP_NEQ, 0, 0, nd->line); break;
                case TOK_LT:      emit(c, OP_LT, 0, 0, nd->line); break;
                case TOK_LTE:     emit(c, OP_LTE, 0, 0, nd->line); break;
                case TOK_GT:      emit(c, OP_GT, 0, 0, nd->line); break;
                case TOK_GTE:     emit(c, OP_GTE, 0, 0, nd->line); break;
                default: break;
            }
            break;
        }

        case EXPR_CALL: {
            BucketList bk = get_ref_bucket(nd);
            int argc = 0;
            TreeNode *param = bk->is_builtin ? NULL : bk->nd->child[0];
            for (TreeNode *arg = nd->child[0]; arg; arg = arg->rSibling) {
                compile_expr(c, arg);
                if (param && param->kind.param == PARAM_VAR) {
                    emit_cast_if_needed(c, param->dcl_type, arg->expr_type, arg->line);
                }
                if (param) param = param->rSibling;
                argc++;
            }
            if (bk->is_builtin) {
                emit(c, OP_CALL_BUILTIN, (int32_t)bk->builtin_kind, argc, nd->line);
            } else {
                emit(c, OP_CALL, bk->slot, argc, nd->line);
            }
            break;
        }
    }
}

static void assign_local_slots(Compiler *c, TreeNode *nd) {
    if (!nd) return;
    for (TreeNode *cur = nd; cur != NULL; cur = cur->rSibling) {
        if (cur->node_kind == NODE_DCL && (cur->kind.dcl == DCL_VAR || cur->kind.dcl == DCL_ARRAY)) {
            BucketList bk = get_dcl_bucket(cur);
            if (bk && !bk->is_global && bk->slot < 0) {
                bk->slot = c->current_local_count++;
            }
        }
        if (!(cur->node_kind == NODE_DCL && cur->kind.dcl == DCL_FUNC)) {
            for (int i = 0; i < MAX_CHILDREN; ++i) {
                assign_local_slots(c, cur->child[i]);
            }
        }
    }
}

static void compile_node(Compiler *c, TreeNode *nd) {
    if (!nd) return;

    if (nd->node_kind == NODE_DCL) {
        switch (nd->kind.dcl) {
            case DCL_VAR: {
                BucketList bk = get_dcl_bucket(nd);
                if (nd->child[0]) {
                    compile_expr(c, nd->child[0]);
                    emit_cast_if_needed(c, nd->dcl_type, nd->child[0]->expr_type, nd->line);
                } else {
                    emit_default_value(c, nd->dcl_type, nd->line);
                }
                emit_store_var(c, bk, nd->line);
                emit(c, OP_POP, 0, 0, nd->line);
                break;
            }
            case DCL_ARRAY: {
                BucketList bk = get_dcl_bucket(nd);
                compile_expr(c, nd->child[0]); /* array size */
                emit(c, OP_NEW_ARRAY, (int32_t)nd->elem_type, 0, nd->line);
                emit_store_var(c, bk, nd->line);
                emit(c, OP_POP, 0, 0, nd->line);
                break;
            }
            case DCL_FUNC:
                /* Compiled in dedicated function pass */
                break;
        }
        return;
    }

    if (nd->node_kind == NODE_STMT) {
        switch (nd->kind.stmt) {
            case STMT_COMPOUND:
                compile_list(c, nd->child[0]);
                break;

            case STMT_EXPR:
                compile_expr(c, nd->child[0]);
                emit(c, OP_POP, 0, 0, nd->line);
                break;

            case STMT_EMPTY:
                break;

            case STMT_IF: {
                compile_expr(c, nd->child[0]);
                int jmp_false = emit(c, OP_JMP_IF_FALSE, 0, 0, nd->line);
                compile_node(c, nd->child[1]);
                if (nd->child[2]) {
                    int jmp_end = emit(c, OP_JMP, 0, 0, nd->line);
                    patch_jump(c, jmp_false, (int)c->prog->code_count);
                    compile_node(c, nd->child[2]);
                    patch_jump(c, jmp_end, (int)c->prog->code_count);
                } else {
                    patch_jump(c, jmp_false, (int)c->prog->code_count);
                }
                break;
            }

            case STMT_WHILE: {
                push_loop(c);
                int cond_pc = (int)c->prog->code_count;
                compile_expr(c, nd->child[0]);
                int exit_jmp = emit(c, OP_JMP_IF_FALSE, 0, 0, nd->line);
                compile_node(c, nd->child[1]);
                emit(c, OP_JMP, cond_pc, 0, nd->line);
                int end_pc = (int)c->prog->code_count;
                patch_jump(c, exit_jmp, end_pc);
                pop_loop(c, cond_pc, end_pc);
                break;
            }

            case STMT_DO_WHILE: {
                push_loop(c);
                int body_pc = (int)c->prog->code_count;
                compile_node(c, nd->child[0]);
                int cond_pc = (int)c->prog->code_count;
                compile_expr(c, nd->child[1]);
                int exit_jmp = emit(c, OP_JMP_IF_FALSE, 0, 0, nd->line);
                emit(c, OP_JMP, body_pc, 0, nd->line);
                int end_pc = (int)c->prog->code_count;
                patch_jump(c, exit_jmp, end_pc);
                pop_loop(c, cond_pc, end_pc);
                break;
            }

            case STMT_FOR: {
                push_loop(c);
                /* Init: could be var decl list or expression */
                if (nd->child[0]) {
                    if (nd->child[0]->node_kind == NODE_EXPR) {
                        compile_expr(c, nd->child[0]);
                        emit(c, OP_POP, 0, 0, nd->child[0]->line);
                    } else {
                        compile_list(c, nd->child[0]);
                    }
                }
                int cond_pc = (int)c->prog->code_count;
                int exit_jmp = -1;
                if (nd->child[1]) {
                    compile_expr(c, nd->child[1]);
                    exit_jmp = emit(c, OP_JMP_IF_FALSE, 0, 0, nd->line);
                }
                compile_node(c, nd->child[3]);
                int update_pc = (int)c->prog->code_count;
                if (nd->child[2]) {
                    compile_expr(c, nd->child[2]);
                    emit(c, OP_POP, 0, 0, nd->child[2]->line);
                }
                emit(c, OP_JMP, cond_pc, 0, nd->line);
                int end_pc = (int)c->prog->code_count;
                if (exit_jmp >= 0) {
                    patch_jump(c, exit_jmp, end_pc);
                }
                pop_loop(c, update_pc, end_pc);
                break;
            }

            case STMT_BREAK: {
                if (c->loop_depth > 0) {
                    LoopContext *ctx = &c->loops[c->loop_depth - 1];
                    int jmp = emit(c, OP_JMP, 0, 0, nd->line);
                    if (ctx->break_count < MAX_PATCHES) {
                        ctx->break_patches[ctx->break_count++] = jmp;
                    }
                }
                break;
            }

            case STMT_CONTINUE: {
                if (c->loop_depth > 0) {
                    LoopContext *ctx = &c->loops[c->loop_depth - 1];
                    int jmp = emit(c, OP_JMP, 0, 0, nd->line);
                    if (ctx->continue_count < MAX_PATCHES) {
                        ctx->continue_patches[ctx->continue_count++] = jmp;
                    }
                }
                break;
            }

            case STMT_RETURN: {
                if (nd->child[0]) {
                    compile_expr(c, nd->child[0]);
                    emit_cast_if_needed(c, c->current_ret_type, nd->child[0]->expr_type, nd->line);
                } else {
                    emit(c, OP_CONST, const_void(c), 0, nd->line);
                }
                emit(c, OP_RET, 0, 0, nd->line);
                break;
            }
        }
    }
}

static void compile_list(Compiler *c, TreeNode *head) {
    for (TreeNode *cur = head; cur != NULL; cur = cur->rSibling) {
        compile_node(c, cur);
    }
}

static int register_function(BytecodeProgram *prog, TreeNode *fn) {
    if (prog->func_count == prog->func_cap) {
        size_t cap = prog->func_cap == 0 ? 16 : prog->func_cap * 2;
        prog->functions = (FunctionProto *)pyc_realloc(prog->functions, cap * sizeof(FunctionProto));
        prog->func_cap = cap;
    }
    int idx = (int)prog->func_count++;
    FunctionProto *fp = &prog->functions[idx];
    fp->name = pyc_strdup(fn->name);
    fp->param_count = 0;
    for (TreeNode *p = fn->child[0]; p; p = p->rSibling) {
        fp->param_count++;
    }
    fp->local_count = fp->param_count;
    fp->entry_pc = -1;
    fp->ret_type = fn->dcl_type;

    BucketList bk = get_dcl_bucket(fn);
    if (bk) bk->slot = idx;
    return idx;
}

BytecodeProgram *bytecode_compile(TreeNode *ast, SymbolTable *global_scope, DiagnosticList *diags) {
    (void)global_scope;
    BytecodeProgram *prog = (BytecodeProgram *)pyc_calloc(1, sizeof(BytecodeProgram));
    prog->main_func_index = -1;

    Compiler c;
    memset(&c, 0, sizeof(c));
    c.prog = prog;
    c.diags = diags;

    /* Pass 1: assign global variable slots and register all user functions */
    for (TreeNode *cur = ast; cur != NULL; cur = cur->rSibling) {
        if (cur->node_kind == NODE_DCL) {
            if (cur->kind.dcl == DCL_FUNC) {
                int fidx = register_function(prog, cur);
                if (strcmp(cur->name, "main") == 0 && prog->functions[fidx].param_count == 0) {
                    prog->main_func_index = fidx;
                }
            } else {
                BucketList bk = get_dcl_bucket(cur);
                if (bk) {
                    bk->is_global = true;
                    bk->slot = prog->global_count++;
                }
            }
        }
    }

    /* Pass 2: compile all user-defined functions */
    for (TreeNode *cur = ast; cur != NULL; cur = cur->rSibling) {
        if (cur->node_kind == NODE_DCL && cur->kind.dcl == DCL_FUNC) {
            BucketList fn_bk = get_dcl_bucket(cur);
            int fidx = fn_bk->slot;
            FunctionProto *fp = &prog->functions[fidx];

            c.in_function = true;
            c.current_local_count = 0;
            c.current_ret_type = cur->dcl_type;

            /* Assign parameter slots 0..param_count-1 */
            for (TreeNode *p = cur->child[0]; p; p = p->rSibling) {
                BucketList pbk = get_dcl_bucket(p);
                if (pbk) {
                    pbk->is_global = false;
                    pbk->slot = c.current_local_count++;
                }
            }
            /* Assign local slots for variables declared inside the function */
            assign_local_slots(&c, cur->child[1]);

            fp->entry_pc = (int)prog->code_count;
            compile_node(&c, cur->child[1]);

            /* Implicit return at end of function */
            emit_default_value(&c, cur->dcl_type, cur->line);
            emit(&c, OP_RET, 0, 0, cur->line);

            fp->local_count = c.current_local_count;
            c.in_function = false;
        }
    }

    /* Pass 3: compile top-level entry point (global initializers + top-level statements) */
    prog->entry_pc = (int)prog->code_count;
    /* Top-level block-scoped locals (e.g. `for (int i = 0; ...)` at top level) use global slots */
    c.in_function = false;
    c.current_local_count = 0;
    c.current_ret_type = TYPE_VOID;

    /* Assign slots for any block-scoped locals inside top-level statements */
    for (TreeNode *cur = ast; cur != NULL; cur = cur->rSibling) {
        if (!(cur->node_kind == NODE_DCL && cur->kind.dcl == DCL_FUNC)) {
            assign_local_slots(&c, cur);
        }
    }

    for (TreeNode *cur = ast; cur != NULL; cur = cur->rSibling) {
        if (cur->node_kind == NODE_DCL && cur->kind.dcl == DCL_FUNC) continue;
        compile_node(&c, cur);
    }

    prog->top_local_count = c.current_local_count;

    /* Auto-invoke main() if defined */
    if (prog->main_func_index >= 0) {
        emit(&c, OP_CALL, prog->main_func_index, 0, 0);
        emit(&c, OP_POP, 0, 0, 0);
    }
    emit(&c, OP_HALT, 0, 0, 0);

    return prog;
}

void bytecode_free(BytecodeProgram *prog) {
    if (!prog) return;
    free(prog->code);
    for (size_t i = 0; i < prog->const_count; ++i) {
        if (prog->constants[i].kind == CONST_STRING) {
            free(prog->constants[i].as.str_val);
        }
    }
    free(prog->constants);
    for (size_t i = 0; i < prog->func_count; ++i) {
        free(prog->functions[i].name);
    }
    free(prog->functions);
    free(prog);
}

static void print_const(const Constant *k, FILE *out) {
    switch (k->kind) {
        case CONST_VOID:   fprintf(out, "void"); break;
        case CONST_INT:    fprintf(out, "%lld", (long long)k->as.int_val); break;
        case CONST_FLOAT:  fprintf(out, "%g", k->as.float_val); break;
        case CONST_BOOL:   fprintf(out, "%s", k->as.bool_val ? "true" : "false"); break;
        case CONST_STRING: fprintf(out, "\"%s\"", k->as.str_val ? k->as.str_val : ""); break;
    }
}

void bytecode_disassemble(const BytecodeProgram *prog, FILE *out) {
    if (!prog) return;
    fprintf(out, "=== Constants (%zu) ===\n", prog->const_count);
    for (size_t i = 0; i < prog->const_count; ++i) {
        fprintf(out, "  [%zu] ", i);
        print_const(&prog->constants[i], out);
        fprintf(out, "\n");
    }

    fprintf(out, "\n=== Functions (%zu) ===\n", prog->func_count);
    for (size_t i = 0; i < prog->func_count; ++i) {
        const FunctionProto *f = &prog->functions[i];
        fprintf(out, "  [%zu] %s(params=%d, locals=%d) -> %s @ pc=%d\n",
                i, f->name, f->param_count, f->local_count,
                data_type_name(f->ret_type), f->entry_pc);
    }

    fprintf(out, "\n=== Instructions (%zu, entry=%d, globals=%d) ===\n",
            prog->code_count, prog->entry_pc, prog->global_count);
    for (size_t pc = 0; pc < prog->code_count; ++pc) {
        for (size_t fi = 0; fi < prog->func_count; ++fi) {
            if (prog->functions[fi].entry_pc == (int)pc) {
                fprintf(out, "\n<func %s>:\n", prog->functions[fi].name);
            }
        }
        if ((int)pc == prog->entry_pc) {
            fprintf(out, "\n<entry>:\n");
        }
        const Instruction *ins = &prog->code[pc];
        fprintf(out, "  %04zu  %-18s", pc, opcode_name(ins->op));
        switch (ins->op) {
            case OP_CONST:
                fprintf(out, "#%-4d ; ", ins->arg1);
                if (ins->arg1 >= 0 && (size_t)ins->arg1 < prog->const_count) {
                    print_const(&prog->constants[ins->arg1], out);
                }
                break;
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL:
                fprintf(out, "g[%d]", ins->arg1);
                break;
            case OP_LOAD_LOCAL:
            case OP_STORE_LOCAL:
                fprintf(out, "l[%d]", ins->arg1);
                break;
            case OP_NEW_ARRAY:
                fprintf(out, "elem=%s", data_type_name((DataType)ins->arg1));
                break;
            case OP_JMP:
            case OP_JMP_IF_FALSE:
            case OP_JMP_IF_FALSE_PEEK:
            case OP_JMP_IF_TRUE_PEEK:
                fprintf(out, "-> %04d", ins->arg1);
                break;
            case OP_CALL:
                fprintf(out, "func[%d] (%s) argc=%d",
                        ins->arg1,
                        (ins->arg1 >= 0 && (size_t)ins->arg1 < prog->func_count)
                            ? prog->functions[ins->arg1].name : "?",
                        ins->arg2);
                break;
            case OP_CALL_BUILTIN:
                fprintf(out, "builtin[%d] argc=%d", ins->arg1, ins->arg2);
                break;
            default:
                break;
        }
        fprintf(out, "\n");
    }
}
