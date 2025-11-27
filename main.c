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
            printf(type->kind == Type_Struct ? "struct" : "union");
            if (type->str.name) printf(" %s", type->str.name);
            printf("\n");
            for (size_t i = 0; i < list_size(type->str.fields); i++) {
                print_type(list_get(type->str.fields, i), indent + 2);
            }
            return;
        default: printf("unk\n");
    }
}

int main() {
    FILE* f = fopen("test.txt", "r");
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
        jitc_decltype_t decltype = Decltype_None;
        jitc_type_t* type = jitc_parse_type(context, tokens, &decltype);
        if (!type) {
            if (!context->error) printf("nothing to parse\n");
            else jitc_report_error(context->error, stderr);
        }
        else {
            printf(type->name ? "variable %s\n" : "unnamed variable\n", type->name);
            printf("%s", (const char*[]){ "", "static ", "extern ", "typedef " }[decltype]);
            print_type(type, 0);
        }
    }

    free(data);
}
