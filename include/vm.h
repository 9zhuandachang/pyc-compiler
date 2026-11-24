#ifndef PYC_VM_H
#define PYC_VM_H

#include "bytecode.h"

typedef enum {
    VAL_VOID,
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_ARRAY
} ValueKind;

typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjArray ObjArray;

typedef struct {
    ValueKind kind;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        ObjString *str;
        ObjArray *arr;
    } as;
} Value;

typedef enum {
    VM_OK,
    VM_RUNTIME_ERROR
} VMResult;

typedef struct {
    FILE *in_stream;
    FILE *out_stream;
    bool trace_exec;
    size_t max_steps; /* 0 = default (10,000,000) */
} VMConfig;

/* Runs a compiled BytecodeProgram. Returns VM_OK on clean termination. */
VMResult vm_run(const BytecodeProgram *prog, const VMConfig *config, DiagnosticList *diags);

#endif /* PYC_VM_H */
