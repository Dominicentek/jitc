#include "../jitc_internal.h"

queue_t* print_tokens(const char* source, queue_t* _tokens) {
    printf("-- %s --\n", source);
    queue(jitc_token_t)* tokens = _tokens;
    queue(jitc_token_t)* out = queue_new(jitc_token_t);
    while (queue_size(tokens) > 0) {
        jitc_token_t* token = &queue_peek(tokens);
        printf("(%s %d:%d) ", token->filename, token->row, token->col);
        switch (token->type) {
            case TOKEN_END_OF_FILE: printf("eof\n"); break;
            case TOKEN_IDENTIFIER: printf("id (%s)\n", token->value.string); break;
            case TOKEN_INTEGER: printf("int (%ld)\n", token->value.integer); break;
            case TOKEN_FLOAT: printf("float (%f)\n", token->value.floating); break;
            case TOKEN_STRING: printf("str (%s)\n", token->value.string); break;
            default: printf("%s\n", token_table[token->type]);
        }
        queue_push(out) = queue_pop(tokens);
    }
    return out;
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
        case Type_Enum: printf("enum of base "); print_type(type->ptr.base, indent + 2); break;
        case Type_Varargs: printf("varargs\n"); break;
        case Type_StructRef: printf("structref %s\n", type->ref.name); break;
        case Type_UnionRef: printf("unionref %s\n", type->ref.name); break;
        case Type_EnumRef: printf("enumref %s\n", type->ref.name); break;
        case Type_Template:
            printf("template\n");
            print_type(type->templ.base, indent + 2);
            return;
        case Type_Placeholder: printf("placeholder %s\n", type->placeholder.name); break;
    }
}

void print_ast(jitc_ast_t* ast, int indent) {
    if (!ast) {
        printf("%*s%s", indent, "", "(null)\n");
        return;
    }
    printf("%*s%s", indent, "", jitc_ast_type_t_names[ast->node_type]);
    switch (ast->node_type) {
        case AST_Unary:
            printf(": %s\n", jitc_unary_op_t_names[ast->unary.operation]);
            print_ast(ast->unary.inner, indent + 2);
            break;
        case AST_Binary:
            printf(": %s\n", jitc_binary_op_t_names[ast->unary.operation]);
            print_ast(ast->binary.left, indent + 2);
            print_ast(ast->binary.right, indent + 2);
            break;
        case AST_Branch:
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
                print_ast(list_get(ast->list.inner, i), indent + 2);
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
            printf(": %s %s\n", jitc_decltype_t_names[ast->decl.decltype], ast->decl.type->name);
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
        case AST_Goto:
        case AST_Label:
            printf(": %s\n", ast->label.name);
            break;
        case AST_Break:
        case AST_Continue: break;
        case AST_Initializer: break;
    }
}

int main(int argc, char** argv) {
    if (argc <= 1) {
        printf("Expected file argument\n");
        return 1;
    }
    jitc_context_t* context = jitc_create_context();
    if (!jitc_parse_file(context, argv[1])) {
        printf("Unable to inspect file\n");
        jitc_report_error(context, stdout);
        return 1;
    }
    jitc_scope_t* scope = &list_get(context->scopes, 0);
    for (int i = 0; i < map_size(scope->variables); i++) {
        map_index(scope->variables, i);
        jitc_variable_t* var = &map_get_value(scope->variables);
        if (var->type->kind != Type_Function) continue;
        if (var->decltype != Decltype_None && var->decltype != Decltype_Static) continue;
        printf("%s:\n", var->type->name);
        for (size_t j = 0; j < var->func->addr->size; j++) {
            if (j != 0 && j % 16 == 0) printf("\n");
            printf("%02x ", ((uint8_t*)var->func->addr->ptr)[j]);
        }
        printf("\n\n");
    }
    return 0;
}
