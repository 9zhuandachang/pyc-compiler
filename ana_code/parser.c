#include "parser.h"
#include "util.h"
#include "scanner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Current token being processed
static Node *currentToken;

// Function prototypes for recursive parsing
static TreeNode* stmt_sequence(void);
static TreeNode* stmt_sequence_colon(int ind);

static TreeNode* statement(void);
static TreeNode* statement_colon(int ind);


static TreeNode* if_stmt(void);
static TreeNode* while_stmt(void);
static TreeNode* for_stmt(void);
static TreeNode* do_while_stmt(void);

static TreeNode* int_stmt(void);
static TreeNode* frac_stmt(void);
static TreeNode* str_stmt(void);


static TreeNode* id_stmt(void);
static TreeNode* assign_stmt(TreeNode *left);

static TreeNode* expr(void);
static TreeNode* term(void);
static TreeNode* factor(void);
static TreeNode * switch_stmt(void);

TreeNode* parse(List *tokenList) {
    currentToken = tokenList->head;

    return stmt_sequence();
}

static void syntaxError(const char *message) {
    // printf("1\n");
    // if(currentToken->t.type == LPAR)
        // printf("2\n");
    fprintf(stderr, "Syntax error: %s\n", message);
    if (currentToken != NULL) {
        fprintf(stderr, "currentToken : %s\n", tokenTypeNames[currentToken->t.type]);
        // if (currentToken->t.info != NULL) {
        //     fprintf(stderr, "Token info: %s\n", currentToken->t.info);
        // }
    }
    exit(1);
}

int mi = 0;

static void match(TokenType expected) {
    printf("%s %d\n",tokenTypeNames[expected],mi);
    mi++;
    fprintf(stderr, "currentToken : %s\n", tokenTypeNames[currentToken->t.type]);
    printf("expected match %s\n",tokenTypeNames[expected]);
    printf("\n");
    if (currentToken->t.type == expected) {
        currentToken = currentToken->next;
    } else {
        // printf("here\n");
        syntaxError("unexpected token");
    }
}

static TreeNode* stmt_sequence(void) {
    TreeNode *t = statement();
    TreeNode *p = t;
    

    while (currentToken != NULL && currentToken->t.type != EOF) {
        TreeNode *q;
        
        if (currentToken->t.type == SEMI) {
            currentToken = currentToken->next;
        } 
        else if (currentToken->t.type == NEWLINE) {
            currentToken = currentToken->next;
        } 
        else if(currentToken->t.type == COMMENT){
            currentToken = currentToken->next;
        }
        else if(currentToken->t.type == EoF){
            break;
        }
        else {
            break;
        }
        q = statement();
        if (q != NULL) {
            if (t == NULL) {
                t = p = q;
            } else {
                p->rSibling = q;
                p = q;
            }
            currentToken = currentToken->next;
        }
    }
    return t;
}

static TreeNode* statement_colon(int ind){
        // printf("start colon stmt");  
    TreeNode *t = NULL;
    while( currentToken->t.type == COMMENT 
    || currentToken->t.type == NEWLINE
    ||currentToken->t.type == INDENT){
        if(currentToken->t.type == INDENT){
            printf("ind %d\n",atoi(currentToken->t.info));
            if(ind != atoi(currentToken->t.info))
            return t;
        }
        currentToken = currentToken->next;
        printf("stmt jump\n");
        if(currentToken->t.type == EoF)
            return t;
    }

    switch (currentToken->t.type) {
        case IF:
            t = if_stmt();
            break;
        case WHILE:
            t = while_stmt();
            break;
        case FOR:
            t = for_stmt();
            break;
        case DO:
            t = do_while_stmt();
            break;
        case ID:
            t = id_stmt();
            break;
        case INT:
            t=int_stmt();
            break;
        case FRAC:
            t=frac_stmt();
            break;
        case STR:
            t=str_stmt();
            break;
        // case RCUR:
        //     break;


        default:
            printf("%s\n",tokenTypeNames[currentToken->t.type]);
            syntaxError("unexpected token");
            break;
    }
    return t;
}

static TreeNode* statement(void) {
    // printf("start stmt");  
    TreeNode *t = NULL;
    while(currentToken->t.type == INDENT 
    || currentToken->t.type == COMMENT 
    || currentToken->t.type == NEWLINE
    || currentToken->t.type == SEMI){
        currentToken = currentToken->next;
        printf("stmt jump\n");
        if(currentToken->t.type == EoF){
            printf ("find EOF\n");
            return t;
        }
    }
    if(currentToken->t.type == EoF){
        printf ("find EOF\n");
        return t;
    }
            

    switch (currentToken->t.type) {
        case IF:
            t = if_stmt();
            break;
        case WHILE:
            t = while_stmt();
            break;
        case FOR:
            t = for_stmt();
            break;
        case DO:
            t = do_while_stmt();
            break;
        case ID:
            t = id_stmt();
            break;
        case INT:
            t=int_stmt();
            break;
        case FRAC:
            t=frac_stmt();
            break;
        case STR:
            t=str_stmt();
            break;
        case RCUR:
            break;
        
        default:
            printf("%s",tokenTypeNames[currentToken->t.type]);
            printf(" here \n");
            syntaxError("unexpected token");
            break;
    }
    
    return t;
}


static TreeNode* stmt_sequence_colon(int ind) {
    TreeNode *t = statement_colon(ind);
    TreeNode *p = t;

    while (currentToken != NULL && currentToken->t.type != EoF) {
        TreeNode *q;
        if (currentToken->t.type == SEMI || currentToken->t.type == COMMENT) {
            currentToken = currentToken->next;
            continue;
        } else if (currentToken->t.type == NEWLINE) {
            Node *newNode = createNode(NEWLINE, NULL);
            // currentToken->next->prev = newNode;
            // newNode->next = currentToken->next->prev;
            // currentToken->next = newNode;
            // newNode->prev = currentToken;
            currentToken = currentToken->next;
            if (currentToken->t.type == INDENT) {
                int currentIndent = atoi(currentToken->t.info);
                currentToken = currentToken->next;
                if (currentIndent < ind) {
                    break;
                }
                else if(currentToken->t.type == EoF){
                    break;
                }
                else if (currentIndent > ind) {
                    syntaxError("unexpected indentation");
                }
                
            }
            else if (ind > 0) {
                break;
            }
            else if(currentToken->t.type == EoF){
                break;
            }
        } else {
            break;
        }
        q = statement_colon(ind);
        if (q != NULL) {
            if (t == NULL) {
                t = p = q;
            } else {
                p->rSibling = q;
                p = q;
            }
            currentToken = currentToken->next;
        }
    }
    return t;
}

static TreeNode* if_stmt(void) {
    TreeNode *t = newStmtNode(SLCT_STMT);
    printf("%s 222\n",tokenTypeNames[currentToken->t.type]);
    match(IF);
    if (t != NULL) t->child[0] = expr();
    
    if (currentToken->t.type == COLON) {
        match(COLON);
        match(NEWLINE);
        if (t != NULL) 
            t->child[1] = stmt_sequence_colon(atoi(currentToken->t.info));
    } else if (currentToken->t.type == LCUR) {
        match(LCUR);
        if (t != NULL) t->child[1] = stmt_sequence();
        match(RCUR);
    } else {
        if (t != NULL) t->child[1] = statement();
    }

    if (currentToken->t.type == ELSE) {
        match(ELSE);
        if (currentToken->t.type == COLON) {
            match(COLON);
        match(NEWLINE);
        if (t != NULL) 
            t->child[1] = stmt_sequence_colon(atoi(currentToken->t.info));
        } else if (currentToken->t.type == LCUR) {
            match(LCUR);
            if (t != NULL) t->child[1] = stmt_sequence();
            match(RCUR);
        } else {
            if (t != NULL) t->child[1] = statement();
        }
    }
    return t;
}

static TreeNode* while_stmt(void) {
    TreeNode *t = newStmtNode(WHILE_STMT);
    match(WHILE);
    if (t != NULL) t->child[0] = expr();
    
    if (currentToken->t.type == COLON) {
        match(COLON);
        match(NEWLINE);
        if (t != NULL) 
            t->child[1] = stmt_sequence_colon(atoi(currentToken->t.info));
    } else if (currentToken->t.type == LCUR) {
        match(LCUR);

        // while(t != NULL){
        //     // printf("%s 111\n",tokenTypeNames[currentToken->t.type]);
        //     if(currentToken->t.type == RCUR){
        //         match(RCUR);
        //         break;
        //     }
        //     else if(currentToken->t.type == NEWLINE)
        //         currentToken = currentToken->next;
        //     else if(currentToken->t.type == COMMENT)
        //         currentToken = currentToken->next;
        //     else if(currentToken->t.type == INDENT)
        //         currentToken = currentToken->next;

        //     t->child[1] = stmt_sequence();
        // }
        if (t != NULL) t->child[1] = stmt_sequence();
        match(RCUR);
    } else {
        if (t != NULL) t->child[1] = statement();
    }
    // printf("333\n");
    return t;
}

static TreeNode* for_stmt(void) {
    TreeNode *t = newStmtNode(FOR_STMT);
    match(FOR);
    if(currentToken->t.type == LPAR){
        match(LPAR);
    }
    if (t != NULL) {
        if (currentToken->t.type == INT) {
            t->child[0] = int_stmt();
        } else {
            t->child[0] = id_stmt();
        }
        match(SEMI);

        t->child[1] = expr();
        match(SEMI);

        t->child[0] = id_stmt();
    }
    if(currentToken->t.type == RPAR){
        match(RPAR);
    }

    if (currentToken->t.type == COLON) {
        match(COLON);
        if (currentToken->t.type == NEWLINE) {
            match(NEWLINE);
            if (t != NULL) 
                t->child[3] = stmt_sequence_colon(atoi(currentToken->t.info)); // 解析循环体
        }
    } else if (currentToken->t.type == LCUR) {
        match(LCUR);
        if (t != NULL) t->child[1] = stmt_sequence();
        if(currentToken->t.type == RCUR)
            match(RCUR);
    } else {
        if (t != NULL) t->child[1] = stmt_sequence();
    }
    return t;
}

static TreeNode* do_while_stmt(void) {
    TreeNode *t = newStmtNode(DO_WHILE_STMT);
    match(DO);
    if (currentToken->t.type == COLON) {
        match(COLON);
        match(NEWLINE);
        if (t != NULL) 
            t->child[1] = stmt_sequence_colon(atoi(currentToken->t.info));
    } else if (currentToken->t.type == LCUR) {
        match(LCUR);
        if (t != NULL) t->child[1] = stmt_sequence();
        if(currentToken->t.type == RCUR)
            match(RCUR);
    } else {
        if (t != NULL) t->child[1] = stmt_sequence();
    }
    match(WHILE);
    if (t != NULL) t->child[0] = expr();
    return t;
}

static TreeNode* id_stmt(void) {
    TreeNode *t = newExpNode(ID_EXPR);
    if ((t != NULL) && (currentToken->t.type == ID)) {
        t->attr.exprAttr.name = copyString(currentToken->t.info);
    }
    match(ID);
    // if(currentToken->t.type == ID){
    //     t->attr.exprAttr.name = copyString(currentToken->t.info);
    //     match(ID);
    // }
        
    if (currentToken->t.type == ASSIGN) {
        t = assign_stmt(t);
    }


    return t;
}

static TreeNode* int_stmt(void) {

    TreeNode *t  = newExpNode(EXPR_STMT);
    if (t != NULL) {
        t->attr.exprAttr.name = "int";
    }
    match(INT);


    TreeNode *idNode = newExpNode(ID_EXPR);
    if ((idNode != NULL) && (currentToken->t.type == ID)) {
        idNode->attr.exprAttr.name = copyString(currentToken->t.info);
    }
    match(ID);


    // if (t != NULL) {
    //     t->child[0] = idNode;
    // }

    if (currentToken->t.type == ASSIGN) {
        t->child[1] = assign_stmt(idNode);
    }
    else{
        t->child[0] = idNode;
    }
    return t;
}

static TreeNode* frac_stmt(void) {

    TreeNode *t  = newExpNode(EXPR_STMT);
    if (t != NULL) {
        t->attr.exprAttr.name = "float";
    }
    match(FRAC);


    TreeNode *idNode = newExpNode(ID_EXPR);
    if ((idNode != NULL) && (currentToken->t.type == ID)) {
        idNode->attr.exprAttr.name = copyString(currentToken->t.info);
    }
    match(ID);
    if (currentToken->t.type == ASSIGN) {
        t->child[1] = assign_stmt(idNode);
    }
    else{
        t->child[0] = idNode;
    }
    return t;
}

static TreeNode* str_stmt(void) {

    TreeNode *t  = newExpNode(EXPR_STMT);
    if (t != NULL) {
        t->attr.exprAttr.name = "char";
    }
    match(STR);


    TreeNode *idNode = newExpNode(ID_EXPR);
    if ((idNode != NULL) && (currentToken->t.type == ID)) {
        idNode->attr.exprAttr.name = copyString(currentToken->t.info);
    }
    match(ID);

    if (currentToken->t.type == ASSIGN) {
        t->child[1] = assign_stmt(idNode);
    }
    else{
        t->child[0] = idNode;
    }
    return t;
}

static TreeNode* assign_stmt(TreeNode *left) {
    TreeNode *t = newStmtNode(EXPR_STMT);
    if (t != NULL) {
        t->child[0] = left;
        match(ASSIGN);
        t->child[1] = expr();
    }
    return t;
}

static TreeNode * switch_stmt(void){

}



static TreeNode* expr(void) {
    TreeNode *t = term();
    while ((currentToken->t.type == PLUS) || (currentToken->t.type == MINUS) ||
           (currentToken->t.type == LT) || (currentToken->t.type == GT) ||
           (currentToken->t.type == LTE) || (currentToken->t.type == GTE) ||
           (currentToken->t.type == EQ)) {//all the judgement
        TreeNode *p = newExpNode(OP_EXPR);
        if (p != NULL) {
            p->child[0] = t;
            p->attr.exprAttr.op = currentToken->t.type;
            t = p;
            match(currentToken->t.type);
            t->child[1] = term();
        }
    }
    return t;
}

static TreeNode* term(void) {
    TreeNode *t = factor();
    while ((currentToken->t.type == MUL) || (currentToken->t.type == DIV)) {
        TreeNode *p = newExpNode(OP_EXPR);
        if (p != NULL) {
            p->child[0] = t;
            p->attr.exprAttr.op = currentToken->t.type;
            t = p;
            match(currentToken->t.type);
            p->child[1] = factor();
        }
    }
    return t;
}

static TreeNode* factor(void) {
    TreeNode *t = NULL;
    switch (currentToken->t.type) {
        case INTL:
            t = newExpNode(CONST_EXPR);
            if ((t != NULL) && (currentToken->t.type == INTL)) {
                t->attr.exprAttr.val = atoi(currentToken->t.info);
            }
            match(INTL);
            break;
        case FRACL:
            t = newExpNode(ID_EXPR);
            if ((t != NULL) && (currentToken->t.type == FRACL)) {
                t->attr.exprAttr.name = copyString(currentToken->t.info);
            }
            match(FRACL);
            break;
        case STRL:
            t = newExpNode(ID_EXPR);
            if ((t != NULL) && (currentToken->t.type == STRL)) {
                t->attr.exprAttr.name = copyString(currentToken->t.info);
            }
            match(STRL);
            break;
        case ID:
            t = newExpNode(ID_EXPR);
            if ((t != NULL) && (currentToken->t.type == ID)) {
                t->attr.exprAttr.name = copyString(currentToken->t.info);
            }
            match(ID);
            break;
        case LPAR:
            match(LPAR);
            t = expr();
            match(RPAR);
            break;
        default:
            syntaxError("unexpected token");
            break;
    }
    return t;
}
