#ifndef PYC_BYTECODE_H
#define PYC_BYTECODE_H

#include "ast.h"
#include "symbol_table.h"

typedef enum {
    OP_CONST,
    OP_POP,
    OP_DUP,
    OP_DUP2,

    OP_LOAD_GLOBAL,
    OP_STORE_GLOBAL,
    OP_LOAD_LOCAL,
    OP_STORE_LOCAL,

    OP_NEW_ARRAY,
    OP_ARRAY_GET,
    OP_ARRAY_SET,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,

    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,
    OP_NOT,

    OP_CAST_FLOAT,
    OP_CAST_BOOL,

    OP_JMP,
    OP_JMP_IF_FALSE,
    OP_JMP_IF_FALSE_PEEK,
    OP_JMP_IF_TRUE_PEEK,

    OP_CALL,
    OP_CALL_BUILTIN,
    OP_RET,
    OP_HALT
} OpCode;

typedef enum {
    CONST_VOID,
    CONST_INT,
    CONST_FLOAT,
    CONST_BOOL,
    CONST_STRING
} ConstKind;

typedef struct {
    ConstKind kind;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        char *str_val;
    } as;
} Constant;

typedef struct {
    OpCode op;
    int32_t arg1;
    int32_t arg2;
    int line;
} Instruction;

typedef struct {
    char *name;
    int param_count;
    int local_count;
    int entry_pc;
    DataType ret_type;
} FunctionProto;

typedef struct {
    Instruction *code;
    size_t code_count;
    size_t code_cap;

    Constant *constants;
    size_t const_count;
    size_t const_cap;

    FunctionProto *functions;
    size_t func_count;
    size_t func_cap;

    int global_count;
    int top_local_count;
    int entry_pc;
    int main_func_index; /* -1 if no 0-arg main() function */
} BytecodeProgram;

const char *opcode_name(OpCode op);

BytecodeProgram *bytecode_compile(TreeNode *ast, SymbolTable *global_scope, DiagnosticList *diags);
void bytecode_free(BytecodeProgram *prog);
void bytecode_disassemble(const BytecodeProgram *prog, FILE *out);

#endif /* PYC_BYTECODE_H */
