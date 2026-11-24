#ifndef PYC_COMMON_H
#define PYC_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Checked allocation helpers */
void *pyc_malloc(size_t size);
void *pyc_calloc(size_t count, size_t size);
void *pyc_realloc(void *ptr, size_t size);
char *pyc_strdup(const char *s);
char *pyc_strndup(const char *s, size_t n);

/* Read entire file into a heap-allocated NUL-terminated buffer */
char *pyc_read_file(const char *path, size_t *out_len);

/* Diagnostic error list shared across compiler stages */
typedef struct {
    int line;
    int col;
    char *message;
} Diagnostic;

typedef struct {
    Diagnostic *items;
    size_t count;
    size_t capacity;
} DiagnosticList;

void diag_init(DiagnosticList *list);
void diag_free(DiagnosticList *list);
void diag_add(DiagnosticList *list, int line, int col, const char *fmt, ...);
void diag_print(const DiagnosticList *list, const char *stage, FILE *out);

#endif /* PYC_COMMON_H */
