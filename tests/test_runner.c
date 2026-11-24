#include "analyzer.h"
#include "ast.h"
#include "bytecode.h"
#include "common.h"
#include "parser.h"
#include "scanner.h"
#include "symbol_table.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_passed = 0;
static int g_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  [FAIL] %s (line %d): %s\n", __func__, __LINE__, (msg)); \
        g_failed++; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    int before_fail = g_failed; \
    fn(); \
    if (g_failed == before_fail) { \
        printf("  [PASS] %s\n", #fn); \
        g_passed++; \
    } \
} while (0)

typedef struct {
    bool lex_ok;
    bool parse_ok;
    bool sem_ok;
    bool run_ok;
    char output[4096];
    DiagnosticList diags;
} PipelineResult;

static void run_pipeline(const char *src, const char *mock_stdin, size_t max_steps, PipelineResult *res) {
    memset(res, 0, sizeof(*res));
    diag_init(&res->diags);

    TokenList tokens;
    token_list_init(&tokens);

    TreeNode *ast = NULL;
    Analyzer *analyzer = NULL;
    BytecodeProgram *bc = NULL;
    FILE *out_fp = NULL;
    FILE *in_fp = NULL;

    res->lex_ok = scan_source(src, &tokens, &res->diags);
    if (!res->lex_ok) goto done;

    ast = parse_tokens(&tokens, &res->diags);
    res->parse_ok = (res->diags.count == 0);
    if (!res->parse_ok) goto done;

    analyzer = analyzer_create(ast, &res->diags);
    res->sem_ok = analyzer_run(analyzer);
    if (!res->sem_ok) goto done;

    bc = bytecode_compile(ast, analyzer_get_symbol_table(analyzer), &res->diags);
    if (!bc || res->diags.count > 0) goto done;

    out_fp = tmpfile();
    if (!out_fp) {
        fprintf(stderr, "Fatal: tmpfile() failed\n");
        goto done;
    }
    if (mock_stdin) {
        in_fp = tmpfile();
        if (in_fp) {
            fputs(mock_stdin, in_fp);
            rewind(in_fp);
        }
    }

    VMConfig cfg;
    cfg.in_stream = in_fp ? in_fp : stdin;
    cfg.out_stream = out_fp;
    cfg.trace_exec = false;
    cfg.max_steps = max_steps;

    res->run_ok = (vm_run(bc, &cfg, &res->diags) == VM_OK);

    rewind(out_fp);
    size_t nread = fread(res->output, 1, sizeof(res->output) - 1, out_fp);
    res->output[nread] = '\0';

done:
    if (out_fp) fclose(out_fp);
    if (in_fp) fclose(in_fp);
    bytecode_free(bc);
    analyzer_destroy(analyzer);
    ast_free(ast);
    token_list_free(&tokens);
}

static bool has_diag_substring(const DiagnosticList *diags, const char *needle) {
    for (size_t i = 0; i < diags->count; ++i) {
        if (strstr(diags->items[i].message, needle) != NULL) {
            return true;
        }
    }
    return false;
}

/* ==================== Lexer Tests ==================== */

static void test_lexer_tokens_and_comments(void) {
    const char *src =
        "# python comment\r\n"
        "// c line comment\r\n"
        "/* block\n comment */\n"
        "int x_1 = 42;\n"
        "float f = 3.14e-1;\n"
        "string s = \"hi\\n\";\n"
        "x_1 += 1; x_1++;\n";

    TokenList tl;
    DiagnosticList diags;
    token_list_init(&tl);
    diag_init(&diags);

    bool ok = scan_source(src, &tl, &diags);
    ASSERT_TRUE(ok && diags.count == 0, "expected clean lexing");
    ASSERT_TRUE(tl.count > 15, "expected tokens");
    ASSERT_TRUE(tl.items[0].type == TOK_INT && tl.items[0].line == 5, "first token should be INT on line 5");
    ASSERT_TRUE(tl.items[1].type == TOK_ID && strcmp(tl.items[1].lexeme, "x_1") == 0, "expected identifier x_1");

    token_list_free(&tl);
    diag_free(&diags);
}

static void test_lexer_errors(void) {
    const char *src = "int a = @;\nstring s = \"unterminated\n/* open";
    TokenList tl;
    DiagnosticList diags;
    token_list_init(&tl);
    diag_init(&diags);

    bool ok = scan_source(src, &tl, &diags);
    ASSERT_TRUE(!ok && diags.count >= 3, "expected 3 lexical errors");
    ASSERT_TRUE(has_diag_substring(&diags, "unrecognized"), "expected unrecognized char error");
    ASSERT_TRUE(has_diag_substring(&diags, "unterminated string"), "expected unterminated string error");
    ASSERT_TRUE(has_diag_substring(&diags, "unterminated block comment"), "expected unterminated comment error");

    token_list_free(&tl);
    diag_free(&diags);
}

/* ==================== Parser & Hybrid Syntax Tests ==================== */

static void test_hybrid_braces_and_indentation(void) {
    const char *src =
        "int x = 0;\n"
        "int y = 0;\n"
        "while x < 5 :\n"
        "    x = x + 1   # python indent\n"
        "    y += x\n"
        "while x > 0 :\n"
        "{\n"
        "    x--;\n"
        "        y += 2; // free indentation inside braces\n"
        "}\n"
        "println(x, y)\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.lex_ok && res.parse_ok && res.sem_ok && res.run_ok, "expected hybrid program to succeed");
    ASSERT_TRUE(strcmp(res.output, "0 25\n") == 0, "expected x=0, y=1+2+3+4+5+10=25");
    diag_free(&res.diags);
}

static void test_nested_python_if_elif_else(void) {
    const char *src =
        "def classify(n: int) -> string:\n"
        "    if n < 0:\n"
        "        return \"neg\"\n"
        "    elif n == 0:\n"
        "        return \"zero\"\n"
        "    else:\n"
        "        if n % 2 == 0:\n"
        "            return \"even\"\n"
        "        else:\n"
        "            return \"odd\"\n"
        "println(classify(-3), classify(0), classify(4), classify(7))\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected nested python if/elif/else to run");
    ASSERT_TRUE(strcmp(res.output, "neg zero even odd\n") == 0, "unexpected classification output");
    diag_free(&res.diags);
}

static void test_bad_indentation_error(void) {
    const char *src =
        "int x = 0\n"
        "if x == 0:\n"
        "    x = 1\n"
        "      x = 2\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.lex_ok && !res.parse_ok, "expected indentation syntax error");
    ASSERT_TRUE(has_diag_substring(&res.diags, "unexpected indentation"), "expected unexpected indentation message");
    diag_free(&res.diags);
}

/* ==================== Semantic Analysis Tests ==================== */

static void test_semantic_errors(void) {
    const char *src =
        "int a = \"hello\";\n"
        "int a = 2;\n"
        "b = 3;\n"
        "break;\n"
        "return 1;\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.lex_ok && res.parse_ok && !res.sem_ok, "expected semantic errors");
    ASSERT_TRUE(has_diag_substring(&res.diags, "cannot initialize"), "expected init type mismatch");
    ASSERT_TRUE(has_diag_substring(&res.diags, "redeclaration"), "expected redeclaration error");
    ASSERT_TRUE(has_diag_substring(&res.diags, "undeclared"), "expected undeclared error");
    ASSERT_TRUE(has_diag_substring(&res.diags, "outside of a loop"), "expected break outside loop error");
    ASSERT_TRUE(has_diag_substring(&res.diags, "outside of a function"), "expected return outside func error");
    diag_free(&res.diags);
}

static void test_semantic_call_and_array_checks(void) {
    const char *src =
        "int foo(int x, float y) { return x; }\n"
        "int main() {\n"
        "    int arr[3];\n"
        "    int r1 = foo(1);\n"
        "    int r2 = arr[\"bad\"];\n"
        "    int r3 = r1(2);\n"
        "    return 0;\n"
        "}\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.parse_ok && !res.sem_ok, "expected call/array semantic errors");
    ASSERT_TRUE(has_diag_substring(&res.diags, "expects 2 argument"), "expected wrong arg count");
    ASSERT_TRUE(has_diag_substring(&res.diags, "array index must be 'int'"), "expected index type error");
    ASSERT_TRUE(has_diag_substring(&res.diags, "is not a function"), "expected not-a-function error");
    diag_free(&res.diags);
}

/* ==================== End-to-End Execution Tests ==================== */

static void test_arithmetic_precedence_and_floats(void) {
    const char *src =
        "int a = 1 + 2 * 3 - 4 / 2 + 10 % 3;\n"
        "float b = a + 0.5;\n"
        "float c = -b * 2;\n"
        "println(a, b, c)\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected arithmetic test to run");
    ASSERT_TRUE(strcmp(res.output, "6 6.5 -13\n") == 0, "expected 6 6.5 -13");
    diag_free(&res.diags);
}

static void test_inc_dec_and_short_circuit(void) {
    const char *src =
        "int x = 5;\n"
        "int a = x++;\n"
        "int b = ++x;\n"
        "int arr[2];\n"
        "arr[0] = 10; arr[1] = 20;\n"
        "int c = arr[0]++;\n"
        "int d = ++arr[1];\n"
        "int i = 2;\n"
        "bool safe = (i < len(arr)) && (arr[i] == 99);\n"
        "bool safe2 = (i == 2) || (arr[i] == 99);\n"
        "println(a, b, x, c, arr[0], d, arr[1], safe, safe2)\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected inc/dec and short-circuit test to run");
    ASSERT_TRUE(strcmp(res.output, "5 7 7 10 11 21 21 false true\n") == 0, "unexpected output");
    diag_free(&res.diags);
}

static void test_loops_break_continue_do_while(void) {
    const char *src =
        "int sum = 0;\n"
        "for (int i = 0; i < 10; i++) {\n"
        "    if (i % 2 == 0) continue;\n"
        "    if (i > 7) break;\n"
        "    sum += i;\n"
        "}\n"
        "int k = 0;\n"
        "do {\n"
        "    k++;\n"
        "} while (k < 3);\n"
        "println(sum, k)\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected loop test to run");
    ASSERT_TRUE(strcmp(res.output, "16 3\n") == 0, "expected 1+3+5+7=16 and k=3");
    diag_free(&res.diags);
}

static void test_mutual_recursion_and_inferred_def(void) {
    const char *src =
        "def is_even(n: int) -> bool:\n"
        "    if n == 0: return true\n"
        "    return is_odd(n - 1)\n"
        "def is_odd(n: int) -> bool:\n"
        "    if n == 0: return false\n"
        "    return is_even(n - 1)\n"
        "def square(x: int):\n"
        "    return x * x\n"
        "println(is_even(6), is_odd(6), square(9))\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected mutual recursion test to run");
    ASSERT_TRUE(strcmp(res.output, "true false 81\n") == 0, "expected true false 81");
    diag_free(&res.diags);
}

static void test_quicksort_arrays_and_scoping(void) {
    const char *src =
        "def swap(a: int[], i: int, j: int):\n"
        "    int t = a[i]\n"
        "    a[i] = a[j]\n"
        "    a[j] = t\n"
        "int partition(int a[], int lo, int hi) {\n"
        "    int piv = a[hi];\n"
        "    int i = lo - 1;\n"
        "    for (int j = lo; j < hi; j++) {\n"
        "        if (a[j] <= piv) {\n"
        "            i++;\n"
        "            swap(a, i, j);\n"
        "        }\n"
        "    }\n"
        "    swap(a, i + 1, hi);\n"
        "    return i + 1;\n"
        "}\n"
        "void qsort(int a[], int lo, int hi) {\n"
        "    if (lo < hi) {\n"
        "        int p = partition(a, lo, hi);\n"
        "        qsort(a, lo, p - 1);\n"
        "        qsort(a, p + 1, hi);\n"
        "    }\n"
        "}\n"
        "int main() {\n"
        "    int a[6];\n"
        "    a[0] = 5; a[1] = 2; a[2] = 9; a[3] = 1; a[4] = 5; a[5] = 6;\n"
        "    qsort(a, 0, len(a) - 1);\n"
        "    println(a);\n"
        "    return 0;\n"
        "}\n";

    PipelineResult res;
    run_pipeline(src, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected quicksort to run");
    ASSERT_TRUE(strcmp(res.output, "[1, 2, 5, 5, 6, 9]\n") == 0, "expected sorted array");
    diag_free(&res.diags);
}

static void test_builtin_io_read_write(void) {
    const char *src =
        "int a = read();\n"
        "float b = read_float();\n"
        "write(a + b);\n";

    PipelineResult res;
    run_pipeline(src, "10 2.5\n", 0, &res);
    ASSERT_TRUE(res.run_ok, "expected IO test to run");
    ASSERT_TRUE(strcmp(res.output, "12.5\n") == 0, "expected 12.5");
    diag_free(&res.diags);
}

static void test_runtime_errors(void) {
    PipelineResult r1;
    run_pipeline("int x = 10 / 0;\n", NULL, 0, &r1);
    ASSERT_TRUE(r1.sem_ok && !r1.run_ok, "expected div-by-zero runtime error");
    ASSERT_TRUE(has_diag_substring(&r1.diags, "division by zero"), "expected div by zero message");
    diag_free(&r1.diags);

    PipelineResult r2;
    run_pipeline("int a[3]; a[5] = 1;\n", NULL, 0, &r2);
    ASSERT_TRUE(r2.sem_ok && !r2.run_ok, "expected bounds runtime error");
    ASSERT_TRUE(has_diag_substring(&r2.diags, "out of bounds"), "expected out of bounds message");
    diag_free(&r2.diags);

    PipelineResult r3;
    run_pipeline("while (true) { }\n", NULL, 1000, &r3);
    ASSERT_TRUE(r3.sem_ok && !r3.run_ok, "expected step limit error");
    ASSERT_TRUE(has_diag_substring(&r3.diags, "step limit"), "expected step limit message");
    diag_free(&r3.diags);
}

static void test_legacy_samples_parse_cleanly(void) {
    /* Legacy samples reference undeclared globals like x, y, count;
       they must lex and parse cleanly without syntax errors. */
    const char *legacy_excerpt =
        "int x = 0, y = 0, count = 0;\n"
        "while (x > y)\n"
        "    x = x + 1;\n"
        "while x > y :\n"
        "    x = x + 1   # Python-style indentation\n"
        "    y = y - 1\n"
        "while x > y :\n"
        "{\n"
        "    x = x + 1;\n"
        "        y = y - 1;\n"
        "}\n"
        "while (x < 10) {\n"
        "    if (count > 5) break;\n"
        "    count++;\n"
        "    x++;\n"
        "}\n"
        "println(x, y, count)\n";

    PipelineResult res;
    run_pipeline(legacy_excerpt, NULL, 0, &res);
    ASSERT_TRUE(res.run_ok, "expected legacy hybrid syntax to compile and run");
    ASSERT_TRUE(strcmp(res.output, "6 0 6\n") == 0, "expected x=6, y=0, count=6");
    diag_free(&res.diags);
}

int main(void) {
    printf("Running PyC compiler & VM test suite...\n");
    RUN_TEST(test_lexer_tokens_and_comments);
    RUN_TEST(test_lexer_errors);
    RUN_TEST(test_hybrid_braces_and_indentation);
    RUN_TEST(test_nested_python_if_elif_else);
    RUN_TEST(test_bad_indentation_error);
    RUN_TEST(test_semantic_errors);
    RUN_TEST(test_semantic_call_and_array_checks);
    RUN_TEST(test_arithmetic_precedence_and_floats);
    RUN_TEST(test_inc_dec_and_short_circuit);
    RUN_TEST(test_loops_break_continue_do_while);
    RUN_TEST(test_mutual_recursion_and_inferred_def);
    RUN_TEST(test_quicksort_arrays_and_scoping);
    RUN_TEST(test_builtin_io_read_write);
    RUN_TEST(test_runtime_errors);
    RUN_TEST(test_legacy_samples_parse_cleanly);

    printf("\nSummary: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
