#include "dynamics.h"
#include "jitc.h"
#include "jitc_internal.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>

void print_type(jitc_type_t* type, int indent) {
    printf("%*s%s%s", indent, "",
        type->is_const ? "const " : "",
        type->is_unsigned ? "uns " : ""
    );
    switch (type->kind) {
        case Type_Void:    printf("void\n"); return;
        case Type_Int8:    printf("int8\n"); return;
        case Type_Int16:   printf("int16\n"); return;
        case Type_Int32:   printf("int32\n"); return;
        case Type_Int64:   printf("int64\n"); return;
        case Type_Float32: printf("float32\n"); return;
        case Type_Float64: printf("float64\n"); return;
        case Type_Pointer:
            printf("ptr to\n");
            print_type(type->ptr.base, indent + 2);
            return;
        case Type_Array:
            printf(type->arr.size == -1
                ? "array of unknown size containing\n"
                : "array of size %lu containing\n",
                type->arr.size
            );
            print_type(type->arr.base, indent + 2);
            return;
        case Type_Function:
            printf("function\n");
            printf("%*sthat returns\n", indent + 2, "");
            print_type(type->func.ret, indent + 4);
            printf("%*sand has params\n", indent + 2, "");
            for (size_t i = 0; i < list_size(type->func.params); i++) {
                print_type(list_get_ptr(type->func.params, i), indent + 4);
            }
            return;
        case Type_Struct:
        case Type_Union:
            printf(type->kind == Type_Struct ? "struct\n" : "union\n");
            for (size_t i = 0; i < list_size(type->str.fields); i++) {
                print_type(list_get(type->str.fields, i), indent + 2);
            }
            return;
        case Type_Varargs: printf("varargs\n"); break;
        default: printf("unk\n");
    }
}

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
                "arithplus",
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
        case AST_Type:
            printf("\n");
            print_type(ast->type.type, indent + 2);
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
