#include "vm.h"

#include <math.h>
#include <stdarg.h>

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY
} ObjKind;

struct Obj {
    ObjKind kind;
    struct Obj *next;
};

struct ObjString {
    Obj base;
    size_t length;
    char *chars;
};

struct ObjArray {
    Obj base;
    size_t length;
    Value *elements;
};

#define VM_STACK_MAX 65536
#define VM_FRAMES_MAX 1024
#define VM_DEFAULT_MAX_STEPS 10000000ULL

typedef struct {
    int return_pc;
    int base_slot;
    int func_index;
} CallFrame;

typedef struct {
    const BytecodeProgram *prog;
    Value *stack;
    int sp;

    Value *globals;
    int global_count;

    CallFrame frames[VM_FRAMES_MAX];
    int frame_count;

    Obj *objects;
    FILE *in_stream;
    FILE *out_stream;
    bool trace_exec;
    size_t max_steps;
    DiagnosticList *diags;
} VM;

static ObjString *vm_new_string(VM *vm, const char *chars, size_t len) {
    ObjString *s = (ObjString *)pyc_calloc(1, sizeof(ObjString));
    s->base.kind = OBJ_STRING;
    s->base.next = vm->objects;
    vm->objects = (Obj *)s;
    s->length = len;
    s->chars = (char *)pyc_malloc(len + 1);
    if (len > 0) memcpy(s->chars, chars, len);
    s->chars[len] = '\0';
    return s;
}

static ObjArray *vm_new_array(VM *vm, size_t len, DataType elem_type) {
    ObjArray *a = (ObjArray *)pyc_calloc(1, sizeof(ObjArray));
    a->base.kind = OBJ_ARRAY;
    a->base.next = vm->objects;
    vm->objects = (Obj *)a;
    a->length = len;
    a->elements = (Value *)pyc_calloc(len > 0 ? len : 1, sizeof(Value));

    for (size_t i = 0; i < len; ++i) {
        switch (elem_type) {
            case TYPE_INT:
                a->elements[i].kind = VAL_INT;
                a->elements[i].as.int_val = 0;
                break;
            case TYPE_FLOAT:
                a->elements[i].kind = VAL_FLOAT;
                a->elements[i].as.float_val = 0.0;
                break;
            case TYPE_BOOL:
                a->elements[i].kind = VAL_BOOL;
                a->elements[i].as.bool_val = false;
                break;
            case TYPE_STRING:
                a->elements[i].kind = VAL_STRING;
                a->elements[i].as.str = vm_new_string(vm, "", 0);
                break;
            default:
                a->elements[i].kind = VAL_VOID;
                break;
        }
    }
    return a;
}

static void vm_free_objects(VM *vm) {
    Obj *cur = vm->objects;
    while (cur) {
        Obj *next = cur->next;
        if (cur->kind == OBJ_STRING) {
            ObjString *s = (ObjString *)cur;
            free(s->chars);
        } else if (cur->kind == OBJ_ARRAY) {
            ObjArray *a = (ObjArray *)cur;
            free(a->elements);
        }
        free(cur);
        cur = next;
    }
    vm->objects = NULL;
}

static bool vm_error(VM *vm, int line, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    diag_add(vm->diags, line, 0, "%s", buf);
    return false;
}

static bool vm_push(VM *vm, Value v, int line) {
    if (vm->sp >= VM_STACK_MAX) {
        return vm_error(vm, line, "stack overflow");
    }
    vm->stack[vm->sp++] = v;
    return true;
}

static Value vm_pop(VM *vm) {
    return vm->stack[--vm->sp];
}

static bool value_is_truthy(Value v) {
    switch (v.kind) {
        case VAL_BOOL:   return v.as.bool_val;
        case VAL_INT:    return v.as.int_val != 0;
        case VAL_FLOAT:  return v.as.float_val != 0.0;
        case VAL_STRING: return v.as.str && v.as.str->length > 0;
        case VAL_ARRAY:  return v.as.arr && v.as.arr->length > 0;
        case VAL_VOID:   return false;
    }
    return false;
}

static double value_to_double(Value v) {
    if (v.kind == VAL_FLOAT) return v.as.float_val;
    if (v.kind == VAL_INT) return (double)v.as.int_val;
    if (v.kind == VAL_BOOL) return v.as.bool_val ? 1.0 : 0.0;
    return 0.0;
}

static void value_print(Value v, FILE *out) {
    switch (v.kind) {
        case VAL_VOID:   fprintf(out, "void"); break;
        case VAL_INT:    fprintf(out, "%lld", (long long)v.as.int_val); break;
        case VAL_FLOAT:  fprintf(out, "%g", v.as.float_val); break;
        case VAL_BOOL:   fprintf(out, "%s", v.as.bool_val ? "true" : "false"); break;
        case VAL_STRING: fprintf(out, "%s", v.as.str ? v.as.str->chars : ""); break;
        case VAL_ARRAY: {
            fprintf(out, "[");
            if (v.as.arr) {
                for (size_t i = 0; i < v.as.arr->length; ++i) {
                    if (i > 0) fprintf(out, ", ");
                    value_print(v.as.arr->elements[i], out);
                }
            }
            fprintf(out, "]");
            break;
        }
    }
}

static Value make_void(void) {
    Value v;
    v.kind = VAL_VOID;
    v.as.int_val = 0;
    return v;
}

static Value make_int(int64_t x) {
    Value v;
    v.kind = VAL_INT;
    v.as.int_val = x;
    return v;
}

static Value make_float(double x) {
    Value v;
    v.kind = VAL_FLOAT;
    v.as.float_val = x;
    return v;
}

static Value make_bool(bool b) {
    Value v;
    v.kind = VAL_BOOL;
    v.as.bool_val = b;
    return v;
}

static bool exec_builtin(VM *vm, BuiltinKind bk, int argc, int line) {
    int arg_base = vm->sp - argc;
    Value ret = make_void();

    switch (bk) {
        case BUILTIN_PRINT:
            for (int i = 0; i < argc; ++i) {
                if (i > 0) fputc(' ', vm->out_stream);
                value_print(vm->stack[arg_base + i], vm->out_stream);
            }
            fflush(vm->out_stream);
            break;

        case BUILTIN_PRINTLN:
            for (int i = 0; i < argc; ++i) {
                if (i > 0) fputc(' ', vm->out_stream);
                value_print(vm->stack[arg_base + i], vm->out_stream);
            }
            fputc('\n', vm->out_stream);
            fflush(vm->out_stream);
            break;

        case BUILTIN_WRITE:
            if (argc >= 1) {
                value_print(vm->stack[arg_base], vm->out_stream);
                fputc('\n', vm->out_stream);
                fflush(vm->out_stream);
            }
            break;

        case BUILTIN_READ:
        case BUILTIN_READ_INT: {
            long long val = 0;
            if (fscanf(vm->in_stream, "%lld", &val) != 1) {
                return vm_error(vm, line, "failed to read integer from input");
            }
            ret = make_int((int64_t)val);
            break;
        }

        case BUILTIN_READ_FLOAT: {
            double val = 0.0;
            if (fscanf(vm->in_stream, "%lf", &val) != 1) {
                return vm_error(vm, line, "failed to read float from input");
            }
            ret = make_float(val);
            break;
        }

        case BUILTIN_LEN: {
            Value target = vm->stack[arg_base];
            if (target.kind == VAL_ARRAY && target.as.arr) {
                ret = make_int((int64_t)target.as.arr->length);
            } else if (target.kind == VAL_STRING && target.as.str) {
                ret = make_int((int64_t)target.as.str->length);
            } else {
                return vm_error(vm, line, "len() requires array or string");
            }
            break;
        }

        default:
            return vm_error(vm, line, "unknown built-in function");
    }

    vm->sp = arg_base;
    return vm_push(vm, ret, line);
}

VMResult vm_run(const BytecodeProgram *prog, const VMConfig *config, DiagnosticList *diags) {
    if (!prog) return VM_RUNTIME_ERROR;

    VM vm;
    memset(&vm, 0, sizeof(vm));
    vm.prog = prog;
    vm.stack = (Value *)pyc_calloc(VM_STACK_MAX, sizeof(Value));
    vm.global_count = prog->global_count > 0 ? prog->global_count : 1;
    vm.globals = (Value *)pyc_calloc((size_t)vm.global_count, sizeof(Value));
    vm.in_stream = (config && config->in_stream) ? config->in_stream : stdin;
    vm.out_stream = (config && config->out_stream) ? config->out_stream : stdout;
    vm.trace_exec = config ? config->trace_exec : false;
    vm.max_steps = (config && config->max_steps > 0) ? config->max_steps : VM_DEFAULT_MAX_STEPS;
    vm.diags = diags;

    /* Reserve a top-level frame for top-level block locals (e.g., top-level `for (int i=0;...)`) */
    int top_local_reserve = prog->top_local_count;
    vm.frames[0].return_pc = -1;
    vm.frames[0].base_slot = 0;
    vm.frames[0].func_index = -1;
    vm.frame_count = 1;
    vm.sp = top_local_reserve;

    int pc = prog->entry_pc;
    size_t steps = 0;
    VMResult result = VM_OK;

    while (pc >= 0 && (size_t)pc < prog->code_count) {
        if (++steps > vm.max_steps) {
            vm_error(&vm, prog->code[pc].line, "execution step limit exceeded (possible infinite loop)");
            result = VM_RUNTIME_ERROR;
            break;
        }

        const Instruction *ins = &prog->code[pc++];
        if (vm.trace_exec) {
            fprintf(stderr, "[TRACE pc=%04d sp=%d] %s %d %d\n",
                    pc - 1, vm.sp, opcode_name(ins->op), ins->arg1, ins->arg2);
        }

        switch (ins->op) {
            case OP_CONST: {
                const Constant *k = &prog->constants[ins->arg1];
                Value v = make_void();
                switch (k->kind) {
                    case CONST_VOID:   v = make_void(); break;
                    case CONST_INT:    v = make_int(k->as.int_val); break;
                    case CONST_FLOAT:  v = make_float(k->as.float_val); break;
                    case CONST_BOOL:   v = make_bool(k->as.bool_val); break;
                    case CONST_STRING:
                        v.kind = VAL_STRING;
                        v.as.str = vm_new_string(&vm, k->as.str_val, strlen(k->as.str_val));
                        break;
                }
                if (!vm_push(&vm, v, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_POP:
                if (vm.sp > 0) vm.sp--;
                break;

            case OP_DUP: {
                Value top = vm.stack[vm.sp - 1];
                if (!vm_push(&vm, top, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_DUP2: {
                Value a = vm.stack[vm.sp - 2];
                Value b = vm.stack[vm.sp - 1];
                if (!vm_push(&vm, a, ins->line) || !vm_push(&vm, b, ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_LOAD_GLOBAL:
                if (!vm_push(&vm, vm.globals[ins->arg1], ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;

            case OP_STORE_GLOBAL:
                vm.globals[ins->arg1] = vm.stack[vm.sp - 1];
                break;

            case OP_LOAD_LOCAL: {
                int slot = vm.frames[vm.frame_count - 1].base_slot + ins->arg1;
                if (!vm_push(&vm, vm.stack[slot], ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_STORE_LOCAL: {
                int slot = vm.frames[vm.frame_count - 1].base_slot + ins->arg1;
                vm.stack[slot] = vm.stack[vm.sp - 1];
                break;
            }

            case OP_NEW_ARRAY: {
                Value len_val = vm_pop(&vm);
                if (len_val.kind != VAL_INT || len_val.as.int_val <= 0) {
                    vm_error(&vm, ins->line, "array size must be a positive integer (got %lld)",
                             (long long)len_val.as.int_val);
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                Value arr_v;
                arr_v.kind = VAL_ARRAY;
                arr_v.as.arr = vm_new_array(&vm, (size_t)len_val.as.int_val, (DataType)ins->arg1);
                if (!vm_push(&vm, arr_v, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_ARRAY_GET: {
                Value idx_v = vm_pop(&vm);
                Value arr_v = vm_pop(&vm);
                if (arr_v.kind != VAL_ARRAY || !arr_v.as.arr) {
                    vm_error(&vm, ins->line, "cannot index non-array value");
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                if (idx_v.kind != VAL_INT || idx_v.as.int_val < 0 ||
                    (size_t)idx_v.as.int_val >= arr_v.as.arr->length) {
                    vm_error(&vm, ins->line, "array index %lld out of bounds (length %zu)",
                             (long long)idx_v.as.int_val, arr_v.as.arr->length);
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                if (!vm_push(&vm, arr_v.as.arr->elements[idx_v.as.int_val], ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_ARRAY_SET: {
                Value val = vm_pop(&vm);
                Value idx_v = vm_pop(&vm);
                Value arr_v = vm_pop(&vm);
                if (arr_v.kind != VAL_ARRAY || !arr_v.as.arr) {
                    vm_error(&vm, ins->line, "cannot index non-array value");
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                if (idx_v.kind != VAL_INT || idx_v.as.int_val < 0 ||
                    (size_t)idx_v.as.int_val >= arr_v.as.arr->length) {
                    vm_error(&vm, ins->line, "array index %lld out of bounds (length %zu)",
                             (long long)idx_v.as.int_val, arr_v.as.arr->length);
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                arr_v.as.arr->elements[idx_v.as.int_val] = val;
                if (!vm_push(&vm, val, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_ADD: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                Value res;
                if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                    size_t nlen = a.as.str->length + b.as.str->length;
                    char *buf = (char *)pyc_malloc(nlen + 1);
                    memcpy(buf, a.as.str->chars, a.as.str->length);
                    memcpy(buf + a.as.str->length, b.as.str->chars, b.as.str->length);
                    buf[nlen] = '\0';
                    res.kind = VAL_STRING;
                    res.as.str = vm_new_string(&vm, buf, nlen);
                    free(buf);
                } else if (a.kind == VAL_FLOAT || b.kind == VAL_FLOAT) {
                    res = make_float(value_to_double(a) + value_to_double(b));
                } else {
                    res = make_int(a.as.int_val + b.as.int_val);
                }
                if (!vm_push(&vm, res, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_SUB: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                Value res = (a.kind == VAL_FLOAT || b.kind == VAL_FLOAT)
                            ? make_float(value_to_double(a) - value_to_double(b))
                            : make_int(a.as.int_val - b.as.int_val);
                if (!vm_push(&vm, res, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_MUL: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                Value res = (a.kind == VAL_FLOAT || b.kind == VAL_FLOAT)
                            ? make_float(value_to_double(a) * value_to_double(b))
                            : make_int(a.as.int_val * b.as.int_val);
                if (!vm_push(&vm, res, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_DIV: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                if (a.kind == VAL_FLOAT || b.kind == VAL_FLOAT) {
                    double dv = value_to_double(b);
                    if (dv == 0.0) {
                        vm_error(&vm, ins->line, "division by zero");
                        result = VM_RUNTIME_ERROR;
                        goto cleanup;
                    }
                    if (!vm_push(&vm, make_float(value_to_double(a) / dv), ins->line)) {
                        result = VM_RUNTIME_ERROR;
                        goto cleanup;
                    }
                } else {
                    if (b.as.int_val == 0) {
                        vm_error(&vm, ins->line, "integer division by zero");
                        result = VM_RUNTIME_ERROR;
                        goto cleanup;
                    }
                    if (!vm_push(&vm, make_int(a.as.int_val / b.as.int_val), ins->line)) {
                        result = VM_RUNTIME_ERROR;
                        goto cleanup;
                    }
                }
                break;
            }

            case OP_MOD: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                if (b.as.int_val == 0) {
                    vm_error(&vm, ins->line, "modulo by zero");
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                if (!vm_push(&vm, make_int(a.as.int_val % b.as.int_val), ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_NEG: {
                Value a = vm_pop(&vm);
                Value res = (a.kind == VAL_FLOAT) ? make_float(-a.as.float_val) : make_int(-a.as.int_val);
                if (!vm_push(&vm, res, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_EQ:
            case OP_NEQ: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                bool eq = false;
                if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                    eq = strcmp(a.as.str->chars, b.as.str->chars) == 0;
                } else if (a.kind == VAL_FLOAT || b.kind == VAL_FLOAT) {
                    eq = value_to_double(a) == value_to_double(b);
                } else if (a.kind == VAL_BOOL && b.kind == VAL_BOOL) {
                    eq = a.as.bool_val == b.as.bool_val;
                } else {
                    eq = a.as.int_val == b.as.int_val;
                }
                if (!vm_push(&vm, make_bool(ins->op == OP_EQ ? eq : !eq), ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_LT:
            case OP_LTE:
            case OP_GT:
            case OP_GTE: {
                Value b = vm_pop(&vm);
                Value a = vm_pop(&vm);
                bool cmp = false;
                if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                    int r = strcmp(a.as.str->chars, b.as.str->chars);
                    if (ins->op == OP_LT) cmp = r < 0;
                    else if (ins->op == OP_LTE) cmp = r <= 0;
                    else if (ins->op == OP_GT) cmp = r > 0;
                    else cmp = r >= 0;
                } else if (a.kind == VAL_FLOAT || b.kind == VAL_FLOAT) {
                    double da = value_to_double(a);
                    double db = value_to_double(b);
                    if (ins->op == OP_LT) cmp = da < db;
                    else if (ins->op == OP_LTE) cmp = da <= db;
                    else if (ins->op == OP_GT) cmp = da > db;
                    else cmp = da >= db;
                } else {
                    int64_t ia = a.as.int_val;
                    int64_t ib = b.as.int_val;
                    if (ins->op == OP_LT) cmp = ia < ib;
                    else if (ins->op == OP_LTE) cmp = ia <= ib;
                    else if (ins->op == OP_GT) cmp = ia > ib;
                    else cmp = ia >= ib;
                }
                if (!vm_push(&vm, make_bool(cmp), ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                break;
            }

            case OP_NOT: {
                Value a = vm_pop(&vm);
                if (!vm_push(&vm, make_bool(!value_is_truthy(a)), ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_CAST_FLOAT: {
                Value a = vm_pop(&vm);
                if (!vm_push(&vm, make_float(value_to_double(a)), ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_CAST_BOOL: {
                Value a = vm_pop(&vm);
                if (!vm_push(&vm, make_bool(value_is_truthy(a)), ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;
            }

            case OP_JMP:
                pc = ins->arg1;
                break;

            case OP_JMP_IF_FALSE: {
                Value cond = vm_pop(&vm);
                if (!value_is_truthy(cond)) {
                    pc = ins->arg1;
                }
                break;
            }

            case OP_JMP_IF_FALSE_PEEK: {
                Value cond = vm.stack[vm.sp - 1];
                if (!value_is_truthy(cond)) {
                    pc = ins->arg1;
                }
                break;
            }

            case OP_JMP_IF_TRUE_PEEK: {
                Value cond = vm.stack[vm.sp - 1];
                if (value_is_truthy(cond)) {
                    pc = ins->arg1;
                }
                break;
            }

            case OP_CALL: {
                if (vm.frame_count >= VM_FRAMES_MAX) {
                    vm_error(&vm, ins->line, "call stack overflow (max recursion depth %d)", VM_FRAMES_MAX);
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                const FunctionProto *fp = &prog->functions[ins->arg1];
                int base = vm.sp - ins->arg2;
                int needed_sp = base + fp->local_count;
                if (needed_sp >= VM_STACK_MAX) {
                    vm_error(&vm, ins->line, "stack overflow in call to '%s'", fp->name);
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                /* Zero-init extra local slots beyond parameters */
                for (int s = vm.sp; s < needed_sp; ++s) {
                    vm.stack[s] = make_void();
                }
                vm.sp = needed_sp;

                CallFrame *cf = &vm.frames[vm.frame_count++];
                cf->return_pc = pc;
                cf->base_slot = base;
                cf->func_index = ins->arg1;
                pc = fp->entry_pc;
                break;
            }

            case OP_CALL_BUILTIN:
                if (!exec_builtin(&vm, (BuiltinKind)ins->arg1, ins->arg2, ins->line)) {
                    result = VM_RUNTIME_ERROR;
                    goto cleanup;
                }
                break;

            case OP_RET: {
                Value ret_val = vm_pop(&vm);
                CallFrame cf = vm.frames[--vm.frame_count];
                vm.sp = cf.base_slot;
                if (!vm_push(&vm, ret_val, ins->line)) { result = VM_RUNTIME_ERROR; goto cleanup; }
                pc = cf.return_pc;
                break;
            }

            case OP_HALT:
                goto cleanup;
        }
    }

cleanup:
    vm_free_objects(&vm);
    free(vm.stack);
    free(vm.globals);
    return result;
}
