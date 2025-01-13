#include "scanner.h"
#include "global.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

const char* tokenTypeNames[] = {
    "PLUS", "MINUS", "MUL", "DIV", "MOD",
    "LT", "GT", "LTE", "GTE", "ASSIGN", "EQ",
    "INT", "FRAC", "STR",
    "ID",
    "INTL", "FRACL", "STRL",
    "LCUR", "RCUR", "LPAR", "RPAR", "LBRA", "RBRA",
    "NEWLINE", "COLON", "SEMI", "COMMA",
    "INDENT",
    "COMMENT","TAB",
    "DO", "WHILE", "FOR","ELSE","IF",
    "DEF",
    "EoF"
};
void initList(List *list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}
//Create a new token node
Node* createNode(TokenType type, const char *info) {
    Node *newNode = (Node*)malloc(sizeof(Node));
    newNode->t.type = type;
    newNode->t.info = strdup(info);
    newNode->prev = newNode->next = NULL;
    return newNode;
}
//Add a token to the list
void addToken(List *list, TokenType type, const char *info) {
    Node *newNode = createNode(type, info);
    if (list->tail) {
        list->tail->next = newNode;
        newNode->prev = list->tail;
        list->tail = newNode;
    } else {
        list->head = list->tail = newNode;
    }
    list->size++;
}

//Scan through the file and generate tokens
#define MAX_STACK_SIZE 100

typedef struct {
    char items[MAX_STACK_SIZE];
    int top;
} Stack;

//init stack
void initStack(Stack *s) {
    s->top = -1;
}

//check stack whether empty
bool isEmpty(Stack *s) {
    return s->top == -1;
}

//check stack whether full
bool isFull(Stack *s) {
    return s->top == MAX_STACK_SIZE - 1;
}

void push(Stack *s, char item) {
    if (isFull(s)) {
        printf("Stack overflow\n");
        exit(1);
    }
    s->items[++s->top] = item;
}


char pop(Stack *s) {
    if (isEmpty(s)) {
        printf("Stack underflow\n");
        exit(1);
    }
    return s->items[s->top--];
}

bool isMatchingPair(char open, char close) {
    return (open == '(' && close == ')') ||
           (open == '{' && close == '}') ||
           (open == '[' && close == ']');
}


bool areBracketsBalanced(Stack *s) {
    Stack tempStack;
    initStack(&tempStack);

    //traverse original stack for bracket matching
    for (int i = 0; i <= s->top; ++i) {
        char bracket = s->items[i];

        if (bracket == '(' || bracket == '{' || bracket == '[') {
            push(&tempStack, bracket);
        }
        //process close brackets
        else if (bracket == ')' || bracket == '}' || bracket == ']') {
            //If tempStack is empty, there's no matching open bracket
            if (isEmpty(&tempStack)) {
                return false;
            }

            char openBracket = pop(&tempStack);
            if (!isMatchingPair(openBracket, bracket)) {
                return false;
            }
        }
    }

    return isEmpty(&tempStack);
}
int counttab(FILE *file)
{
    char ch;
    int count;
    while (1) {
        ch = fgetc(file);
        if (ch == '\t') {
            count=count+3;
        } else if (ch == ' ') {
        
            printf("Error\n");
            exit(1);
        } else {
            ungetc(ch, file);
            return count;
        }
    }
}
int countindent(FILE *file)
{
    char ch;
    int count;
    if((ch = fgetc(file))==' ')
    {
        ungetc(ch,file);
    while (1) {
        ch = fgetc(file);
        if (ch == ' ') {
            count++;
        } else if (ch == '\t') {
            printf("Error tabs\n");
            exit(1);
        } else {
            ungetc(ch, file);
            return count;
        }
    }
    }
    ungetc(ch, file);
    if((ch = fgetc(file))=='\t')
    {
        ungetc(ch,file);
        return counttab(file);
    }
     ungetc(ch, file);
    return 0;
}


void scanFile(FILE *file, List *tokenList) {
    scanFiles(file, tokenList);
    if(tokenList->tail->t.type!=EoF){
        if(tokenList->tail->t.type!=NEWLINE)
            addToken(tokenList,NEWLINE,NULL);
        addToken(tokenList,EoF,NULL);

    }
    else{
        if(tokenList->tail->prev->t.type!=NEWLINE)
            addToken(tokenList,NEWLINE,NULL);
    }
        
    if(tokenList->tail->prev->t.type!=NEWLINE)
        addToken(tokenList,NEWLINE,NULL);
}

void scanFiles(FILE *file, List *tokenList) {
    char line[256];
    char ch;
    State currentState=START;
    Stack stack;
    char buffer[256];
    int bufIndex = 0;//index of buffer
    while ((ch = fgetc(file)) != EOF) {
        // printf("%c",ch);
        switch(currentState){
        case START:
            if (ch == '+') {
                addToken(tokenList, PLUS, NULL);
            } else if (ch == '-') {
                addToken(tokenList, MINUS, NULL);
            }
            else if(ch=='*'){
                addToken(tokenList, MUL, NULL);
            }
            else if(ch==';'){
                addToken(tokenList, SEMI, NULL);
            }
            else if(ch==','){
                addToken(tokenList,COMMA,NULL);
            }
            else if(ch==')'){
                addToken(tokenList,RPAR,NULL);
                 push(&stack, ch);
            }
            else if(ch=='('){
                addToken(tokenList,LPAR,NULL);
                push(&stack, ch);
            }
            else if(ch=='{'){
                addToken(tokenList,LCUR,NULL);
                push(&stack, ch);
            }
            else if(ch=='}'){
                addToken(tokenList,RCUR,NULL);
                push(&stack, ch);
            }
            else if(ch=='['){
                addToken(tokenList,LBRA,NULL);
                push(&stack, ch);
            }
            else if(ch==']'){
                addToken(tokenList,RBRA,NULL);
                push(&stack, ch);
            }
            else if (ch == '/') {
                currentState = READ_OVER;
            }
            else if(ch=='%')
            {
                addToken(tokenList,MOD,NULL);
            }
            else if (ch == '#') {
                ungetc(ch, file);
                currentState = IN_COMMENT;
            }
            else if (ch == ':') {
                addToken(tokenList,COLON,NULL);
            }
            else if (ch == '<') {
                currentState = READ_LT;
            }
            else if (ch == '>') {
                currentState = READ_GT;
            } 
            else if (ch == '=') {
                currentState = READ_EQ;
            } 
            else if (isdigit(ch)) {
                 buffer[bufIndex++] = ch;
                 currentState = IN_NUM;
            }
            else if (isalpha(ch)) {
                // printf("%c",ch);
                 buffer[bufIndex++] = ch;
                 currentState = IN_ID;
            } 
            else if (ch=='\n') {
                addToken(tokenList, NEWLINE, NULL);
                if((ch = fgetc(file))==EOF){
                    addToken(tokenList, EoF, NULL);
                    return;
                }
                else ungetc(ch,file);
                int count=0;
                count=countindent(file);
                if(count!=0)
                {   
                      snprintf(buffer, sizeof(buffer), "%d", count);
                    addToken(tokenList,INDENT,buffer);
                }
                // snprintf(buffer, sizeof(buffer), "%d", count);
                // addToken(tokenList,INDENT,buffer);
            }
            else if(ch==' ')
             {
             }
            else {
                currentState = WRONG;
            }
            break;

        case IN_NUM:
                if (isdigit(ch)) {
                    buffer[bufIndex++] = ch;
                } 
                else if(ch=='.'){
                    currentState=IN_FRAC;
                    buffer[bufIndex++] = '.';
                }
                else{
                    buffer[bufIndex] = '\0';
                    addToken(tokenList, INTL, buffer);
                    bufIndex = 0;
                    currentState = START;
                    ungetc(ch, file);
                }
                break;

        case IN_FRAC:
                if (isdigit(ch)) {
                    buffer[bufIndex++] = ch;
                } 
                else{
                    buffer[bufIndex] = '\0';
                    addToken(tokenList, FRACL, buffer);
                    bufIndex = 0;
                    currentState = START;
                    ungetc(ch, file);
                }
                break;
                
        
        case IN_ID:
                if (isalnum(ch)) {
                    buffer[bufIndex++] = ch;
                } else {
                    buffer[bufIndex] = '\0';
                    if (strcmp(buffer, "int") == 0) {
                    addToken(tokenList, INT, NULL);
                    }
                    else if (strcmp(buffer, "float") == 0) {
                    addToken(tokenList, FRAC, "float");
                    }
                    else if (strcmp(buffer, "char") == 0) {
                    addToken(tokenList, STR, NULL);
                    }
                    else if (strcmp(buffer, "do") == 0) {
                    addToken(tokenList,DO, NULL);
                    }
                    else if (strcmp(buffer, "if") == 0) {
                    addToken(tokenList,IF, NULL);
                    }
                    else if (strcmp(buffer, "while") == 0) {
                    addToken(tokenList,WHILE, NULL);
                    }
                    else if (strcmp(buffer, "else") == 0) {
                    addToken(tokenList, ELSE, NULL);
                    }
                    else if(strcmp(buffer, "for") == 0){
                        addToken(tokenList, FOR, NULL);
                    }
                    else addToken(tokenList, ID, buffer);
                    bufIndex = 0;
                    currentState = START;
                    ungetc(ch, file);
                }
                break;

        case READ_OVER:
               
                if (ch == '*'||ch=='/') {
                    currentState = IN_COMMENT;
                    ungetc(ch, file);
                } else {
                    addToken(tokenList, DIV, NULL);
                    currentState = START;
                    ungetc(ch, file);
                }
                break;

        case IN_COMMENT:
                addToken(tokenList,COMMENT,NULL);
                if (ch == '*') {
                    while(1){
                    if ((ch = fgetc(file)) == '/') {
                        currentState = START;
                        ungetc(ch, file);
                        break;
                    }
                    }
                }
                if (ch == '#'||ch=='/') {
                    while(1){
                    if ((ch = fgetc(file)) == '\n') {
                        currentState = START;
                        ungetc(ch, file);
                        break;
                    }
                    }
                }
                
                break;

        case READ_LT:
                if (ch == '=') {
                    addToken(tokenList, LTE, NULL);
                } else {
                    addToken(tokenList, LT, NULL);
                    ungetc(ch, file);  
                }
                currentState = START;
                break;

        case READ_GT:
                if (ch == '=') {
                    addToken(tokenList, GTE, NULL); 
                } else {
                    addToken(tokenList, GT, NULL);
                    ungetc(ch, file);
                }
                currentState = START;
                break;

        case READ_EQ:
                if (ch == '=') {
                    addToken(tokenList, EQ, NULL);
                } else {
                    addToken(tokenList, ASSIGN, NULL);
                    ungetc(ch, file);
                }
                currentState = START;
                break;

        case WRONG:
                printf("Error: Unrecognized character '%c'\n", ch);
                exit(1);
        default:
                currentState = START;
                break;
                }

    }
    bool result = areBracketsBalanced(&stack);
    if(result==false){
    printf("Error: pair dosen't match ");
    // if((ch = fgetc(file))==EOF){
    //     addToken(tokenList, EoF, NULL);
    //     return;
    // }
    // printf("%s",tokenList->tail->t.info);
    printf("hello");
    exit(1);
    }
}




//Free the token list
void freeList(List *list) {
    Node *current = list->head;
    while (current) {
        Node *next = current->next;
        free(current->t.info);
        free(current);
        current = next;
    }
    list->head = list->tail = NULL;
    list->size = 0;
}

// int main(int argc, char *argv[]) {
//     if (argc != 2) {
//         printf("Usage: %s <filename>\n", argv[0]);
//         return 1;
//     }

//     FILE *file = fopen(argv[1], "r");
//     FILE *file1 = fopen(argv[1], "r");
//     if (!file) {
//         perror("Error opening file");
//         return 1;
//     }
  
//     List tokenList;
//     initList(&tokenList);
//     scanFile(file, &tokenList);
//     printTokens(&tokenList,file1);
//     freeList(&tokenList);
//     fclose(file);
//     return 0;
// }

