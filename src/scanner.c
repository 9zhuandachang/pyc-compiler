#include "scanner.h"

#include <ctype.h>

static const char *const kTokenNames[] = {
    "PLUS", "MINUS", "STAR", "SLASH", "PERCENT",
    "PLUS_PLUS", "MINUS_MINUS",
    "ASSIGN", "PLUS_ASSIGN", "MINUS_ASSIGN", "STAR_ASSIGN", "SLASH_ASSIGN", "PERCENT_ASSIGN",
    "EQ", "NEQ", "LT", "LTE", "GT", "GTE",
    "AND", "OR", "NOT",
    "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET", "RBRACKET",
    "COMMA", "COLON", "SEMI", "ARROW",
    "ID", "INT_LIT", "FLOAT_LIT", "STR_LIT", "TRUE", "FALSE",
    "INT", "FLOAT", "STRING", "CHAR", "BOOL", "VOID",
    "IF", "ELIF", "ELSE", "WHILE", "DO", "FOR",
    "BREAK", "CONTINUE", "RETURN", "DEF", "PASS",
    "NEWLINE", "EOF"
};

const char *token_type_name(TokenType type) {
    size_t idx = (size_t)type;
    size_t count = sizeof(kTokenNames) / sizeof(kTokenNames[0]);
    if (idx >= count) return "UNKNOWN";
    return kTokenNames[idx];
}

void token_list_init(TokenList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void token_list_free(TokenList *list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].lexeme);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void token_list_push(TokenList *list, TokenType type, char *lexeme, int line, int col, int indent) {
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity == 0 ? 64 : list->capacity * 2;
        list->items = (Token *)pyc_realloc(list->items, new_cap * sizeof(Token));
        list->capacity = new_cap;
    }
    Token *t = &list->items[list->count++];
    t->type = type;
    t->lexeme = lexeme;
    t->line = line;
    t->col = col;
    t->indent = indent;
}

typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
    int line_indent;
    bool at_line_start;
    bool line_has_token;
    int paren_depth; /* (...) and [...] suppress NEWLINE */
    TokenList *out;
    DiagnosticList *diags;
} Scanner;

static char sc_peek(const Scanner *sc) {
    return sc->src[sc->pos];
}

static char sc_peek_next(const Scanner *sc) {
    if (sc->src[sc->pos] == '\0') return '\0';
    return sc->src[sc->pos + 1];
}

static char sc_advance(Scanner *sc) {
    char c = sc->src[sc->pos++];
    if (c == '\n') {
        sc->line++;
        sc->col = 1;
    } else {
        sc->col++;
    }
    return c;
}

static bool sc_match(Scanner *sc, char expected) {
    if (sc_peek(sc) != expected) return false;
    sc_advance(sc);
    return true;
}

static TokenType lookup_keyword(const char *ident) {
    if (strcmp(ident, "int") == 0) return TOK_INT;
    if (strcmp(ident, "float") == 0) return TOK_FLOAT;
    if (strcmp(ident, "string") == 0 || strcmp(ident, "str") == 0) return TOK_STRING;
    if (strcmp(ident, "char") == 0) return TOK_CHAR;
    if (strcmp(ident, "bool") == 0) return TOK_BOOL;
    if (strcmp(ident, "void") == 0) return TOK_VOID;
    if (strcmp(ident, "true") == 0 || strcmp(ident, "True") == 0) return TOK_TRUE;
    if (strcmp(ident, "false") == 0 || strcmp(ident, "False") == 0) return TOK_FALSE;
    if (strcmp(ident, "if") == 0) return TOK_IF;
    if (strcmp(ident, "elif") == 0) return TOK_ELIF;
    if (strcmp(ident, "else") == 0) return TOK_ELSE;
    if (strcmp(ident, "while") == 0) return TOK_WHILE;
    if (strcmp(ident, "do") == 0) return TOK_DO;
    if (strcmp(ident, "for") == 0) return TOK_FOR;
    if (strcmp(ident, "break") == 0) return TOK_BREAK;
    if (strcmp(ident, "continue") == 0) return TOK_CONTINUE;
    if (strcmp(ident, "return") == 0) return TOK_RETURN;
    if (strcmp(ident, "def") == 0) return TOK_DEF;
    if (strcmp(ident, "pass") == 0) return TOK_PASS;
    if (strcmp(ident, "and") == 0) return TOK_AND;
    if (strcmp(ident, "or") == 0) return TOK_OR;
    if (strcmp(ident, "not") == 0) return TOK_NOT;
    return TOK_ID;
}

static void emit(Scanner *sc, TokenType type, char *lexeme, int line, int col) {
    token_list_push(sc->out, type, lexeme, line, col, sc->line_indent);
    if (type != TOK_NEWLINE && type != TOK_EOF) {
        sc->line_has_token = true;
    }
}

static void skip_line_comment(Scanner *sc) {
    while (sc_peek(sc) != '\0' && sc_peek(sc) != '\n') {
        sc_advance(sc);
    }
}

static void skip_block_comment(Scanner *sc, int start_line, int start_col) {
    while (sc_peek(sc) != '\0') {
        if (sc_peek(sc) == '*' && sc_peek_next(sc) == '/') {
            sc_advance(sc);
            sc_advance(sc);
            return;
        }
        sc_advance(sc);
    }
    diag_add(sc->diags, start_line, start_col, "unterminated block comment '/*'");
}

static void scan_string(Scanner *sc, char quote, int start_line, int start_col) {
    size_t cap = 32;
    size_t len = 0;
    char *buf = (char *)pyc_malloc(cap);

    while (sc_peek(sc) != '\0' && sc_peek(sc) != quote) {
        char c = sc_advance(sc);
        if (c == '\n') {
            diag_add(sc->diags, start_line, start_col, "unterminated string literal");
            free(buf);
            return;
        }
        if (c == '\\') {
            char esc = sc_peek(sc);
            if (esc == '\0') {
                diag_add(sc->diags, start_line, start_col, "unterminated escape sequence in string");
                free(buf);
                return;
            }
            sc_advance(sc);
            switch (esc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '\'': c = '\''; break;
                case '0': c = '\0'; break;
                default:
                    diag_add(sc->diags, sc->line, sc->col - 1, "unknown escape sequence '\\%c'", esc);
                    c = esc;
                    break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)pyc_realloc(buf, cap);
        }
        buf[len++] = c;
    }

    if (sc_peek(sc) != quote) {
        diag_add(sc->diags, start_line, start_col, "unterminated string literal");
        free(buf);
        return;
    }
    sc_advance(sc); /* closing quote */
    buf[len] = '\0';
    emit(sc, TOK_STR_LIT, buf, start_line, start_col);
}

static void scan_number(Scanner *sc, int start_line, int start_col) {
    size_t start = sc->pos - 1;
    bool is_float = false;

    while (isdigit((unsigned char)sc_peek(sc))) {
        sc_advance(sc);
    }
    if (sc_peek(sc) == '.' && isdigit((unsigned char)sc_peek_next(sc))) {
        is_float = true;
        sc_advance(sc); /* '.' */
        while (isdigit((unsigned char)sc_peek(sc))) {
            sc_advance(sc);
        }
    }
    /* Optional scientific notation e.g. 1e5, 2.5e-3 */
    if (sc_peek(sc) == 'e' || sc_peek(sc) == 'E') {
        size_t save_pos = sc->pos;
        int save_col = sc->col;
        sc_advance(sc);
        if (sc_peek(sc) == '+' || sc_peek(sc) == '-') {
            sc_advance(sc);
        }
        if (isdigit((unsigned char)sc_peek(sc))) {
            is_float = true;
            while (isdigit((unsigned char)sc_peek(sc))) {
                sc_advance(sc);
            }
        } else {
            sc->pos = save_pos;
            sc->col = save_col;
        }
    }

    char *lex = pyc_strndup(sc->src + start, sc->pos - start);
    emit(sc, is_float ? TOK_FLOAT_LIT : TOK_INT_LIT, lex, start_line, start_col);
}

static void scan_identifier(Scanner *sc, int start_line, int start_col) {
    size_t start = sc->pos - 1;
    while (isalnum((unsigned char)sc_peek(sc)) || sc_peek(sc) == '_') {
        sc_advance(sc);
    }
    char *lex = pyc_strndup(sc->src + start, sc->pos - start);
    TokenType kw = lookup_keyword(lex);
    emit(sc, kw, lex, start_line, start_col);
}

bool scan_source(const char *source, TokenList *out_tokens, DiagnosticList *diags) {
    size_t initial_errors = diags->count;

    Scanner sc;
    sc.src = source ? source : "";
    sc.pos = 0;
    sc.line = 1;
    sc.col = 1;
    sc.line_indent = 0;
    sc.at_line_start = true;
    sc.line_has_token = false;
    sc.paren_depth = 0;
    sc.out = out_tokens;
    sc.diags = diags;

    /* Skip UTF-8 BOM if present */
    if ((unsigned char)sc.src[0] == 0xEF &&
        (unsigned char)sc.src[1] == 0xBB &&
        (unsigned char)sc.src[2] == 0xBF) {
        sc.pos = 3;
    }

    while (sc_peek(&sc) != '\0') {
        if (sc.at_line_start && sc.paren_depth == 0) {
            int indent = 0;
            bool saw_space = false;
            bool saw_tab = false;
            int indent_line = sc.line;
            while (sc_peek(&sc) == ' ' || sc_peek(&sc) == '\t') {
                if (sc_peek(&sc) == ' ') {
                    saw_space = true;
                    indent += 1;
                } else {
                    saw_tab = true;
                    indent += 4;
                }
                sc_advance(&sc);
            }
            /* Skip blank / comment-only lines without updating active line_indent */
            if (sc_peek(&sc) == '\r') {
                sc_advance(&sc);
                if (sc_peek(&sc) == '\n') sc_advance(&sc);
                continue;
            }
            if (sc_peek(&sc) == '\n') {
                sc_advance(&sc);
                continue;
            }
            if (sc_peek(&sc) == '#') {
                skip_line_comment(&sc);
                if (sc_peek(&sc) == '\n') sc_advance(&sc);
                continue;
            }
            if (sc_peek(&sc) == '/' && sc_peek_next(&sc) == '/') {
                skip_line_comment(&sc);
                if (sc_peek(&sc) == '\n') sc_advance(&sc);
                continue;
            }
            if (saw_space && saw_tab) {
                diag_add(sc.diags, indent_line, 1, "mixed spaces and tabs in indentation");
            }
            sc.line_indent = indent;
            sc.at_line_start = false;
            sc.line_has_token = false;
        }

        char c = sc_peek(&sc);
        if (c == '\0') break;

        if (c == ' ' || c == '\t' || c == '\r') {
            sc_advance(&sc);
            continue;
        }

        if (c == '\n') {
            int nl_line = sc.line;
            int nl_col = sc.col;
            sc_advance(&sc);
            if (sc.paren_depth == 0) {
                if (sc.line_has_token) {
                    emit(&sc, TOK_NEWLINE, pyc_strdup("\\n"), nl_line, nl_col);
                    sc.line_has_token = false;
                }
                sc.at_line_start = true;
            }
            continue;
        }

        /* Comments */
        if (c == '#') {
            skip_line_comment(&sc);
            continue;
        }
        if (c == '/' && sc_peek_next(&sc) == '/') {
            skip_line_comment(&sc);
            continue;
        }
        if (c == '/' && sc_peek_next(&sc) == '*') {
            int start_line = sc.line;
            int start_col = sc.col;
            sc_advance(&sc);
            sc_advance(&sc);
            skip_block_comment(&sc, start_line, start_col);
            continue;
        }

        int start_line = sc.line;
        int start_col = sc.col;
        c = sc_advance(&sc);

        if (isdigit((unsigned char)c)) {
            scan_number(&sc, start_line, start_col);
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            scan_identifier(&sc, start_line, start_col);
            continue;
        }
        if (c == '"' || c == '\'') {
            scan_string(&sc, c, start_line, start_col);
            continue;
        }

        switch (c) {
            case '+':
                if (sc_match(&sc, '+')) emit(&sc, TOK_PLUS_PLUS, pyc_strdup("++"), start_line, start_col);
                else if (sc_match(&sc, '=')) emit(&sc, TOK_PLUS_ASSIGN, pyc_strdup("+="), start_line, start_col);
                else emit(&sc, TOK_PLUS, pyc_strdup("+"), start_line, start_col);
                break;
            case '-':
                if (sc_match(&sc, '-')) emit(&sc, TOK_MINUS_MINUS, pyc_strdup("--"), start_line, start_col);
                else if (sc_match(&sc, '=')) emit(&sc, TOK_MINUS_ASSIGN, pyc_strdup("-="), start_line, start_col);
                else if (sc_match(&sc, '>')) emit(&sc, TOK_ARROW, pyc_strdup("->"), start_line, start_col);
                else emit(&sc, TOK_MINUS, pyc_strdup("-"), start_line, start_col);
                break;
            case '*':
                if (sc_match(&sc, '=')) emit(&sc, TOK_STAR_ASSIGN, pyc_strdup("*="), start_line, start_col);
                else emit(&sc, TOK_STAR, pyc_strdup("*"), start_line, start_col);
                break;
            case '/':
                if (sc_match(&sc, '=')) emit(&sc, TOK_SLASH_ASSIGN, pyc_strdup("/="), start_line, start_col);
                else emit(&sc, TOK_SLASH, pyc_strdup("/"), start_line, start_col);
                break;
            case '%':
                if (sc_match(&sc, '=')) emit(&sc, TOK_PERCENT_ASSIGN, pyc_strdup("%="), start_line, start_col);
                else emit(&sc, TOK_PERCENT, pyc_strdup("%"), start_line, start_col);
                break;
            case '=':
                if (sc_match(&sc, '=')) emit(&sc, TOK_EQ, pyc_strdup("=="), start_line, start_col);
                else emit(&sc, TOK_ASSIGN, pyc_strdup("="), start_line, start_col);
                break;
            case '!':
                if (sc_match(&sc, '=')) emit(&sc, TOK_NEQ, pyc_strdup("!="), start_line, start_col);
                else emit(&sc, TOK_NOT, pyc_strdup("!"), start_line, start_col);
                break;
            case '<':
                if (sc_match(&sc, '=')) emit(&sc, TOK_LTE, pyc_strdup("<="), start_line, start_col);
                else emit(&sc, TOK_LT, pyc_strdup("<"), start_line, start_col);
                break;
            case '>':
                if (sc_match(&sc, '=')) emit(&sc, TOK_GTE, pyc_strdup(">="), start_line, start_col);
                else emit(&sc, TOK_GT, pyc_strdup(">"), start_line, start_col);
                break;
            case '&':
                if (sc_match(&sc, '&')) emit(&sc, TOK_AND, pyc_strdup("&&"), start_line, start_col);
                else diag_add(sc.diags, start_line, start_col, "unexpected character '&' (did you mean '&&'?)");
                break;
            case '|':
                if (sc_match(&sc, '|')) emit(&sc, TOK_OR, pyc_strdup("||"), start_line, start_col);
                else diag_add(sc.diags, start_line, start_col, "unexpected character '|' (did you mean '||'?)");
                break;
            case '(':
                sc.paren_depth++;
                emit(&sc, TOK_LPAREN, pyc_strdup("("), start_line, start_col);
                break;
            case ')':
                if (sc.paren_depth > 0) sc.paren_depth--;
                emit(&sc, TOK_RPAREN, pyc_strdup(")"), start_line, start_col);
                break;
            case '{':
                emit(&sc, TOK_LBRACE, pyc_strdup("{"), start_line, start_col);
                break;
            case '}':
                emit(&sc, TOK_RBRACE, pyc_strdup("}"), start_line, start_col);
                break;
            case '[':
                sc.paren_depth++;
                emit(&sc, TOK_LBRACKET, pyc_strdup("["), start_line, start_col);
                break;
            case ']':
                if (sc.paren_depth > 0) sc.paren_depth--;
                emit(&sc, TOK_RBRACKET, pyc_strdup("]"), start_line, start_col);
                break;
            case ',':
                emit(&sc, TOK_COMMA, pyc_strdup(","), start_line, start_col);
                break;
            case ':':
                emit(&sc, TOK_COLON, pyc_strdup(":"), start_line, start_col);
                break;
            case ';':
                emit(&sc, TOK_SEMI, pyc_strdup(";"), start_line, start_col);
                break;
            default:
                diag_add(sc.diags, start_line, start_col, "unrecognized character '%c' (0x%02X)",
                         isprint((unsigned char)c) ? c : '?', (unsigned char)c);
                break;
        }
    }

    if (sc.line_has_token) {
        emit(&sc, TOK_NEWLINE, pyc_strdup("\\n"), sc.line, sc.col);
    }
    token_list_push(sc.out, TOK_EOF, pyc_strdup("EOF"), sc.line, sc.col, 0);

    return diags->count == initial_errors;
}

void print_tokens(const TokenList *list, FILE *out) {
    fprintf(out, "%-6s %-6s %-7s %-16s %s\n", "Line", "Col", "Indent", "Type", "Lexeme");
    fprintf(out, "--------------------------------------------------\n");
    for (size_t i = 0; i < list->count; ++i) {
        const Token *t = &list->items[i];
        fprintf(out, "%-6d %-6d %-7d %-16s %s\n",
                t->line, t->col, t->indent,
                token_type_name(t->type),
                t->lexeme ? t->lexeme : "");
    }
}
