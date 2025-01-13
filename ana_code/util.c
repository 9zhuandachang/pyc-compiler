#include "global.h"
#include "util.h"

// Function to create a new statement node
TreeNode *newStmtNode(StmtKind kind) {
    TreeNode *t = (TreeNode *)malloc(sizeof(TreeNode));
    if (t == NULL) {
        fprintf(stderr, "Out of memory error\n");
        exit(1);
    }
    for (int i = 0; i < MAX_CHILDREN; i++) t->child[i] = NULL;
    t->lSibling = t->rSibling = NULL;
    t->nodeKind = STMT_ND;
    t->kind.stmt = kind;
    return t;
}

// Function to create a new expression node
TreeNode *newExpNode(ExprKind kind) {
    TreeNode *t = (TreeNode *)malloc(sizeof(TreeNode));
    if (t == NULL) {
        fprintf(stderr, "Out of memory error\n");
        exit(1);
    }
    for (int i = 0; i < MAX_CHILDREN; i++) t->child[i] = NULL;
    t->lSibling = t->rSibling = NULL;
    t->nodeKind = EXPR_ND;
    t->kind.expr = kind;
    return t;
}

// Function to copy a string
char *copyString(char *s) {
    if (s == NULL) return NULL;
    char *t = (char *)malloc(strlen(s) + 1);
    if (t == NULL) {
        fprintf(stderr, "Out of memory error\n");
        exit(1);
    }
    strcpy(t, s);
    return t;
}

// Print the list of tokens
void printTokens(List *list,FILE *file){
    Node *current = list->head;
     char ch;
     int n=2;
     printf("<----- 1 ");
     while(1){
     if((ch=fgetc(file))!='\n'){
     printf("%c",ch);
     }
     else{
        break;
     }
     }
     printf("----->\n"); 
    while (current) {
        printf("Token: %s  ", tokenTypeNames[current->t.type]);
        if(current->t.info!=NULL){
        printf("Info: %s",current->t.info);
        printf("\n");
        }
        else printf("\n");
        if(current->t.type==24){
            printf("<----- %d ",n);
            n++;
            while (1) {
                    ch = fgetc(file);
                if (ch == -1) {
                    printf("\n");
                    break;
                }
                if (ch == '\n') {
                    printf("-----> \n");
                    break;
                } else {
                    printf("%c", (char)ch);
                 
                }
            }
        }
        current = current->next;
    }
}

// Function to print a syntax tree
void printTree(TreeNode *tree) {
    static int indentno = 0;
    indentno += 2;
    while (tree != NULL) {
        for (int i = 0; i < indentno; i++) printf(" ");
        if (tree->nodeKind == STMT_ND) {
            switch (tree->kind.stmt) {
			case SLCT_STMT:
				printf("If ");
				if (tree->child[2] != NULL)  // has else part
					printf( " with ELSE \n");
				else
					printf( " without ELSE \n");
				break;
				//  case ITER_STMTMT:
			case WHILE_STMT:
				printf("while stmt: \n");
				break;
			case FOR_STMT:
				printf("for stmt: \n");
				break;
			case DO_WHILE_STMT:
				printf("do while stmt: \n");
				break;
			case EXPR_STMT:
				printf("Expression stmt: \n");
				break;
			case CMPD_STMT:
				printf("Compound Stmt:\n");
				break;
			case RTN_STMT:
				printf("Return \n");

				break;
			case NULL_STMT:
				printf( "Null statement:  ;\n");
				break;
			default:
				printf("Unknown StmtNode kind\n");
				break;
			}
            
        } else if (tree->nodeKind == EXPR_ND) {
            switch (tree->kind.expr) {
                case OP_EXPR:
                    printf("Op: %s\n", tokenTypeNames[tree->attr.exprAttr.op]);
                    break;
			    case CONST_EXPR:
				    printf("Const: %d\n",tree->attr.exprAttr.val);
				    break;
                case ID_EXPR:
                    printf("Id: %s\n", tree->attr.exprAttr.name);
                    break;
                // case KW_EXPR:
                //     printf("KEYWORD: %s\n", tree->attr.exprAttr.name);
                //     break;
                default:
                    printf("%s\n",tree->attr.exprAttr.name);
                    break;
            }
        }
        // Recursively print all children
        for (int i = 0; i < MAX_CHILDREN; i++) {
            printTree(tree->child[i]);
        }
        // Move to the next sibling
        tree = tree->rSibling;
    }
    indentno -= 2;
}