#include "global.h"
#include "scanner.h"
#include "parser.h"
#include "util.h"
#include "analyzer.h"
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    FILE *source = fopen(argv[1], "r");
    if (!source) {
        perror("Error opening file");
        return 1;
    }

    List tokenList;
    initList(&tokenList);
    scanFile(source, &tokenList);
    fclose(source);

    printf("Tokens:\n");
    printTokens(&tokenList, fopen(argv[1], "r"));

    printf("scan done\n");

    
    TreeNode *syntaxTree = parse(&tokenList);
    printf("\nSyntax Tree:\n");
    printTree(syntaxTree);

    Analyzer *analyzer = new_s_analyzer(syntaxTree);
    st_print(analyzer->get_symbol_table(analyzer));

    
    st_free(analyzer->get_symbol_table(analyzer));
    freeList(&tokenList);

    return 0;
}