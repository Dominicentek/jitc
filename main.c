#include "dynamics.h"
#include "jitc.h"
#include "jitc_internal.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

void print_ast(jitc_ast_t* ast, int indent) {
    printf("%*s%s", indent, "", (const char*[]){
        "unary",
        "binary",
        "ternary",
        "list",
        "type",
        "decl",
        "func",
        "loop",
        "break",
        "continue",
        "return",
        "integer",
        "floating",
        "strlit",
        "variable",
        "walkstr"
    }[ast->node_type]);
    switch (ast->node_type) {
        case AST_Unary:
            printf(": %s\n", (const char*[]){
                "sufinc",
                "sufdec",
                "preinc",
                "predec",
                "arithneg",
                "logicneg",
                "binaryneg",
                "addressof",
                "deref",
                "sizeof",
                "alignof"
            }[ast->unary.operation]);
            print_ast(ast->unary.inner, indent + 2);
            break;
        case AST_Binary:
            printf(": %s\n", (const char*[]){
                "cast",
                "compexpr",
                "array",
                "funccall",
                "addition",
                "subtraction",
                "multiplication",
                "division",
                "modulo",
                "bitshleft",
                "bitshright",
                "lessthan",
                "greaterthan",
                "lessthanequals",
                "greaterthanequals",
                "equals",
                "notequals",
                "and",
                "or",
                "xor",
                "logicand",
                "logicor",
                "assign",
                "assignaddition",
                "assignsubtraction",
                "assignmultiplication",
                "assigndivision",
                "assignmodulo",
                "assignbitshleft",
                "assignbitshright",
                "assignand",
                "assignor",
                "assignxor",
                "tern1",
                "tern2",
            }[ast->unary.operation]);
            print_ast(ast->binary.left, indent + 2);
            print_ast(ast->binary.right, indent + 2);
            break;
        case AST_Ternary:
            printf("\n");
            print_ast(ast->ternary.when, indent + 2);
            print_ast(ast->ternary.then, indent + 2);
            print_ast(ast->ternary.otherwise, indent + 2);
            break;
        case AST_List:
            printf("\n");
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                print_ast(list_get_ptr(ast->list.inner, i), indent + 2);
            }
            break;
        case AST_Integer:
            printf(": %lu\n", ast->integer.value);
            break;
        case AST_Floating:
            printf(": %f\n", ast->floating.value);
            break;
        case AST_StringLit:
            printf(": %s\n", ast->string.ptr);
            break;
        case AST_Variable:
            printf(": %s\n", ast->variable.name);
            break;
        default: printf("\n");
    }
}

int main() {
    FILE* f = fopen("test/test.c", "r");
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = malloc(size + 1);
    fread(data, size, 1, f);
    data[size] = 0;
    fclose(f);

    jitc_context_t* context = jitc_create_context();
    queue_t* tokens = jitc_lex(context, data, "test.txt");
    if (!tokens) jitc_report_error(context->error, stderr);
    else {
        jitc_ast_t* ast = jitc_parse_ast(context, tokens);
        if (!ast) jitc_report_error(context->error, stderr);
        else print_ast(ast, 0);
    }

    free(data);
}
