#ifndef _UTIL_H_
#define _UTIL_H_

#include "scanner.h"
#include "parser.h"

// Function to create a new statement node
TreeNode *newStmtNode(StmtKind kind);

// Function to create a new expression node
TreeNode *newExpNode(ExprKind kind);

// Function to copy a string
char *copyString(char *s);

// Function to print the list of tokens
void printTokens(List *list, FILE *file);

// Function to print a syntax tree
void printTree(TreeNode *tree);

#endif