#include "common.h"

#include <stdarg.h>

void *pyc_malloc(size_t size) {
    void *p = malloc(size == 0 ? 1 : size);
    if (!p) {
        fprintf(stderr, "Fatal: out of memory (requested %zu bytes)\n", size);
        exit(EXIT_FAILURE);
    }
    return p;
}

void *pyc_calloc(size_t count, size_t size) {
    void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    if (!p) {
        fprintf(stderr, "Fatal: out of memory (calloc %zu x %zu)\n", count, size);
        exit(EXIT_FAILURE);
    }
    return p;
}

void *pyc_realloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size == 0 ? 1 : size);
    if (!p) {
        fprintf(stderr, "Fatal: out of memory (realloc %zu bytes)\n", size);
        exit(EXIT_FAILURE);
    }
    return p;
}

char *pyc_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)pyc_malloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

char *pyc_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *copy = (char *)pyc_malloc(n + 1);
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *pyc_read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = (char *)pyc_malloc((size_t)size + 1);
    size_t read_bytes = fread(buf, 1, (size_t)size, fp);
    buf[read_bytes] = '\0';
    fclose(fp);

    if (out_len) {
        *out_len = read_bytes;
    }
    return buf;
}

void diag_init(DiagnosticList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void diag_free(DiagnosticList *list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].message);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void diag_add(DiagnosticList *list, int line, int col, const char *fmt, ...) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = (Diagnostic *)pyc_realloc(list->items, new_cap * sizeof(Diagnostic));
        list->capacity = new_cap;
    }

    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    char *msg = (char *)pyc_malloc((size_t)(needed > 0 ? needed : 0) + 1);
    vsnprintf(msg, (size_t)(needed > 0 ? needed : 0) + 1, fmt, args_copy);
    va_end(args_copy);

    list->items[list->count].line = line;
    list->items[list->count].col = col;
    list->items[list->count].message = msg;
    list->count++;
}

void diag_print(const DiagnosticList *list, const char *stage, FILE *out) {
    for (size_t i = 0; i < list->count; ++i) {
        const Diagnostic *d = &list->items[i];
        if (d->line > 0 && d->col > 0) {
            fprintf(out, "[%s Error] line %d, col %d: %s\n", stage, d->line, d->col, d->message);
        } else if (d->line > 0) {
            fprintf(out, "[%s Error] line %d: %s\n", stage, d->line, d->message);
        } else {
            fprintf(out, "[%s Error] %s\n", stage, d->message);
        }
    }
}
