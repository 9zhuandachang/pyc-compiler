#include "symbol_table.h"

#define HASH_SHIFT 4

static unsigned int st_hash(const char *key) {
    unsigned int temp = 0;
    for (size_t i = 0; key && key[i] != '\0'; ++i) {
        temp = ((temp << HASH_SHIFT) + (unsigned char)key[i]) % ST_SIZE;
    }
    return temp;
}

SymbolTable *st_initialize(bool restart) {
    static int next_id = 0;
    if (restart) {
        next_id = 0;
    }
    SymbolTable *tab = (SymbolTable *)pyc_calloc(1, sizeof(SymbolTable));
    tab->id = next_id++;
    return tab;
}

SymbolTable *st_attach(SymbolTable *st) {
    if (!st) return NULL;
    SymbolTable *child = st_initialize(false);
    child->upper = st;
    if (!st->lower) {
        st->lower = child;
    } else {
        SymbolTable *last = st->lower;
        while (last->next) {
            last = last->next;
        }
        last->next = child;
        child->prev = last;
    }
    return child;
}

BucketList st_insert_dcl(TreeNode *dclNd, SymbolTable *st) {
    if (!dclNd || !st || !dclNd->name) return NULL;
    unsigned int h = st_hash(dclNd->name);

    BucketList rec = (BucketList)pyc_calloc(1, sizeof(struct BucketListRec));
    rec->st = st;
    rec->nd = dclNd;
    rec->slot = -1;
    rec->is_global = (st->upper == NULL);

    rec->next = st->hashTable[h];
    if (st->hashTable[h]) {
        st->hashTable[h]->prev = rec;
    }
    st->hashTable[h] = rec;

    dclNd->sym_ref = rec;
    return rec;
}

LineList st_insert_ref(TreeNode *refNd, BucketList bk) {
    if (!refNd || !bk) return NULL;
    LineList rec = (LineList)pyc_calloc(1, sizeof(struct LineListRec));
    rec->nd = refNd;
    rec->bk = bk;

    if (!bk->lines) {
        bk->lines = rec;
    } else {
        LineList last = bk->lines;
        while (last->next) {
            last = last->next;
        }
        last->next = rec;
        rec->prev = last;
    }

    refNd->sym_ref = rec;
    return rec;
}

BucketList st_lookup_local(SymbolTable *st, const char *name) {
    if (!st || !name) return NULL;
    unsigned int h = st_hash(name);
    BucketList bl = st->hashTable[h];
    while (bl) {
        if (bl->nd && bl->nd->name && strcmp(bl->nd->name, name) == 0) {
            return bl;
        }
        bl = bl->next;
    }
    return NULL;
}

BucketList st_lookup(SymbolTable *st, const char *name) {
    SymbolTable *cur = st;
    while (cur) {
        BucketList found = st_lookup_local(cur, name);
        if (found) return found;
        cur = cur->upper;
    }
    return NULL;
}

static const char *node_kind_label(const TreeNode *nd) {
    if (!nd) return "Unknown";
    if (nd->node_kind == NODE_DCL) {
        switch (nd->kind.dcl) {
            case DCL_VAR:   return "Var";
            case DCL_ARRAY: return "Array";
            case DCL_FUNC:  return "Func";
        }
    } else if (nd->node_kind == NODE_PARAM) {
        switch (nd->kind.param) {
            case PARAM_VAR:   return "Param";
            case PARAM_ARRAY: return "ArrParam";
        }
    }
    return "Other";
}

static void st_print_rec(const SymbolTable *st, FILE *out) {
    if (!st) return;

    for (int i = 0; i < ST_SIZE; ++i) {
        BucketList bl = st->hashTable[i];
        while (bl) {
            const TreeNode *nd = bl->nd;
            char type_buf[32];
            if (nd->dcl_type == TYPE_ARRAY) {
                snprintf(type_buf, sizeof(type_buf), "%s[]", data_type_name(nd->elem_type));
            } else {
                snprintf(type_buf, sizeof(type_buf), "%s", data_type_name(nd->dcl_type));
            }

            fprintf(out, "%-6d %-6d %-16s %-10s %-10s %-6d ",
                    st->id,
                    st->upper ? st->upper->id : -1,
                    nd->name ? nd->name : "?",
                    node_kind_label(nd),
                    type_buf,
                    nd->line);

            LineList lines = bl->lines;
            if (!lines) {
                fprintf(out, "-");
            } else {
                while (lines) {
                    fprintf(out, "%d", lines->nd->line);
                    if (lines->next) fprintf(out, ", ");
                    lines = lines->next;
                }
            }
            fprintf(out, "\n");
            bl = bl->next;
        }
    }

    st_print_rec(st->lower, out);
    st_print_rec(st->next, out);
}

void st_print(const SymbolTable *st, FILE *out) {
    if (!st) {
        fprintf(out, "(empty symbol table)\n");
        return;
    }
    fprintf(out, "%-6s %-6s %-16s %-10s %-10s %-6s %s\n",
            "Scope", "Upper", "Name", "Kind", "Type", "Decl", "Ref Lines");
    fprintf(out, "----------------------------------------------------------------------\n");
    st_print_rec(st, out);
}

static void line_list_free(LineList lis) {
    while (lis) {
        LineList next = lis->next;
        if (lis->nd) lis->nd->sym_ref = NULL;
        free(lis);
        lis = next;
    }
}

static void bucket_list_free(BucketList lis) {
    while (lis) {
        BucketList next = lis->next;
        if (lis->nd) lis->nd->sym_ref = NULL;
        line_list_free(lis->lines);
        free(lis);
        lis = next;
    }
}

void st_free(SymbolTable *st) {
    if (!st) return;
    for (int i = 0; i < ST_SIZE; ++i) {
        bucket_list_free(st->hashTable[i]);
    }
    st_free(st->lower);
    st_free(st->next);
    free(st);
}
