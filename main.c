#include "dynamics.h"
#include "jitc_internal.h"

#include <stdio.h>
#include <stdlib.h>

void print_token(jitc_token_t* token) {
    printf("(%s %d:%d) ", token->filename, token->row, token->col);
    switch (token->type) {
        case TOKEN_END_OF_FILE: printf("eof\n"); break;
        case TOKEN_IDENTIFIER: printf("id (%s)\n", token->value.string); break;
        case TOKEN_INTEGER: printf("int (%ld)\n", token->value.integer); break;
        case TOKEN_FLOAT: printf("float (%f)\n", token->value.floating); break;
        case TOKEN_STRING: printf("str (%s)\n", token->value.string); break;
        default: printf("%s\n", token_table[token->type]);
    }
}

void print_type(jitc_type_t* type, int indent) {
    printf("%*s%s%s", indent, "",
        type->is_const ? "const " : "",
        type->is_unsigned ? "uns " : ""
    );
    if (type->name) printf("%s: ", type->name);
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
            for (size_t i = 0; i < type->func.num_params; i++) {
                print_type(type->func.params[i], indent + 4);
            }
            return;
        case Type_Struct:
        case Type_Union:
            printf(type->kind == Type_Struct ? "struct\n" : "union\n");
            for (size_t i = 0; i < type->str.num_fields; i++) {
                print_type(type->str.fields[i], indent + 2);
            }
            return;
        case Type_Varargs: printf("varargs\n"); break;
        case Type_StructRef: printf("structref %s\n", type->ref.name); break;
        case Type_UnionRef: printf("unionref %s\n", type->ref.name); break;
        case Type_EnumRef: printf("enumref %s\n", type->ref.name); break;
        default: printf("unk\n");
    }
}

void print_ast(jitc_ast_t* ast, int indent) {
    if (!ast) {
        printf("%*s%s", indent, "", "(null)\n");
        return;
    }
    printf("%*s%s", indent, "", (const char*[]){
        "unary",
        "binary",
        "ternary",
        "list",
        "type",
        "decl",
        "func",
        "loop",
        "scope",
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
        case AST_Scope:
            printf("\n");
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                print_ast(list_get_ptr(ast->list.inner, i), indent + 2);
            }
            break;
        case AST_Integer:
            printf(ast->integer.is_unsigned ? ": %lu\n" : ": %ld\n", ast->integer.value);
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
        case AST_Declaration:
            printf(": %s %s\n", (const char*[]){
                "variable",
                "static",
                "extern",
                "typedef"
            }[ast->decl.decltype], ast->decl.type->name);
            print_type(ast->decl.type, indent + 2);
            break;
        case AST_Function:
            printf(": %s\n", ast->func.variable->name);
            print_type(ast->func.variable, indent + 2);
            print_ast(ast->func.body, indent + 2);
            break;
        case AST_Loop:
            printf("\n");
            print_ast(ast->loop.cond, indent + 2);
            print_ast(ast->loop.body, indent + 2);
            break;
        case AST_Return:
            printf("\n");
            print_ast(ast->ret.expr, indent + 2);
            break;
        case AST_WalkStruct:
            printf(": %s\n", ast->walk_struct.field_name);
            print_ast(ast->walk_struct.struct_ptr, indent + 2);
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
    queue_t* tokens = jitc_lex(context, data, "test/test.c");
    queue_t* tokens1 = queue_new();
    queue_t* tokens2 = queue_new();
    if (!tokens) jitc_report_error(context->error, stderr);
    else {
        printf("== TOKEN DUMP ==\n");
        while (queue_size(tokens) > 0) {
            jitc_token_t* token = queue_pop_ptr(tokens);
            queue_push_ptr(tokens1, token);
            queue_push_ptr(tokens2, token);
            print_token(token);
        }
        queue_delete(tokens);
        smartptr(jitc_ast_t) ast = jitc_parse_ast(context, tokens1);
        while (jitc_pop_scope(context));
        printf("== AST DUMP ==\n");
        if (!ast) jitc_report_error(context->error, stderr);
        else {
            print_ast(ast, 0);
            printf("== GENERATED ASM ==\n");
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                jitc_compile(context, list_get_ptr(ast->list.inner, i));
            }
        }
    }
    while (queue_size(tokens2) > 0) free(queue_pop_ptr(tokens2));
    queue_delete(tokens1);
    queue_delete(tokens2);
    jitc_destroy_context(context);

    free(data);
}
