#include "dynamics.h"
#include "jitc_internal.h"
#include "lexer.h"
#include "parser.h"
#include "cleanups.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define NEXT_TOKEN queue_pop_ptr(tokens)
#define ERROR(...) { jitc_error_set(context, jitc_error_parser(__VA_ARGS__)); return NULL; }

typedef enum {
    Spec_Int       = (1 << 0),
    Spec_Short     = (1 << 1),
    Spec_Long1     = (1 << 2),
    Spec_Long2     = (1 << 3),
    Spec_Char      = (1 << 4),
    Spec_Void      = (1 << 5),
    Spec_Float     = (1 << 6),
    Spec_Double    = (1 << 7),
} jitc_specifiers_t;

static struct {
    jitc_type_kind_t type;
    jitc_specifiers_t specifiers;
} types[] = {
    { Type_Void,    Spec_Void },
    { Type_Int8,    Spec_Char },
    { Type_Int16,   Spec_Short | Spec_Int },
    { Type_Int16,   Spec_Short },
    { Type_Int32,   Spec_Int },
    { Type_Int64,   Spec_Long1 },
    { Type_Int64,   Spec_Long1 | Spec_Int },
    { Type_Int64,   Spec_Long1 | Spec_Long2 },
    { Type_Int64,   Spec_Long1 | Spec_Long2 | Spec_Int },
    { Type_Float32, Spec_Float },
    { Type_Float64, Spec_Double },
    { Type_Float64, Spec_Long1 | Spec_Double },
};

jitc_ast_t* mknode(jitc_ast_type_t type) {
    jitc_ast_t* ast = calloc(sizeof(jitc_ast_t), 1);
    ast->node_type = type;
    return ast;
}

void jitc_delete_type(jitc_type_t* type) {
    switch (type->kind) {
        case Type_Pointer:
            jitc_delete_type(type->ptr.base);
            break;
        case Type_Array:
            jitc_delete_type(type->arr.base);
            break;
        case Type_Function:
            for (size_t i = 0; i < list_size(type->func.params); i++) {
                jitc_delete_type(list_get_ptr(type->func.params, i));
            }
            list_delete(type->func.params);
            jitc_delete_type(type->func.ret);
            break;
        case Type_Struct:
        case Type_Union:
            for (size_t i = 0; i < list_size(type->str.fields); i++) {
                jitc_delete_type(list_get_ptr(type->str.fields, i));
            }
            list_delete(type->str.fields);
            break;
        default: break;
    }
    free(type);
}

bool jitc_parse_base_type(jitc_context_t* context, queue_t* tokens, jitc_type_t* type, jitc_decltype_t* decltype) {
    type->is_const = false;
    type->is_unsigned = false;
    type->kind = Type_Void;
    jitc_specifiers_t specs = 0;
    jitc_token_t* token = NULL;
    jitc_token_t* unsigned_token = NULL, *first_token = NULL;
    bool has_any = false;
    while (true) {
        jitc_decltype_t decl = Decltype_None;
        jitc_specifiers_t new_specs = 0;
        if      ((token = jitc_token_expect(tokens, TOKEN_extern)))   decl = Decltype_Extern;
        else if ((token = jitc_token_expect(tokens, TOKEN_static)))   decl = Decltype_Static;
        else if ((token = jitc_token_expect(tokens, TOKEN_typedef)))  decl = Decltype_Typedef;
        else if ((token = jitc_token_expect(tokens, TOKEN_const)))    type->is_const    = true;
        else if ((token = jitc_token_expect(tokens, TOKEN_unsigned))) type->is_unsigned = true;
        else if ((token = jitc_token_expect(tokens, TOKEN_char)))     new_specs |= Spec_Char;
        else if ((token = jitc_token_expect(tokens, TOKEN_short)))    new_specs |= Spec_Short;
        else if ((token = jitc_token_expect(tokens, TOKEN_int)))      new_specs |= Spec_Int;
        else if ((token = jitc_token_expect(tokens, TOKEN_float)))    new_specs |= Spec_Float;
        else if ((token = jitc_token_expect(tokens, TOKEN_double)))   new_specs |= Spec_Double;
        else if ((token = jitc_token_expect(tokens, TOKEN_void)))     new_specs |= Spec_Void;
        else if ((token = jitc_token_expect(tokens, TOKEN_long)))     new_specs |= specs & Spec_Long1 ? Spec_Long2 : Spec_Long1;
        else break;
        has_any = true;
        if ((specs & new_specs) != 0) ERROR(token, "Duplicate specifier");
        specs |= new_specs;
        if (decl != Decltype_None) {
            if (!decltype) ERROR(token, "Declaration type illegal here");
            if (*decltype != Decltype_None) ERROR(token, "Duplicate declaration type");
            *decltype = decl;
        }
        if (token->type == TOKEN_unsigned && !unsigned_token) unsigned_token = token;
        if (!first_token) first_token = token;
    }
    if (!has_any) return false;
    int i = 0;
    if (specs == 0 && type->is_unsigned) specs |= Spec_Int;
    for (; i < sizeof(types) / sizeof(*types); i++) {
        if (types[i].specifiers == specs) {
            type->kind = types[i].type;
            break;
        }
    }
    if (i == sizeof(types) / sizeof(*types)) ERROR(first_token, "Invalid specifier combination");
    if (type->is_unsigned && !(
        type->kind == Type_Int8  || type->kind == Type_Int16 ||
        type->kind == Type_Int32 || type->kind == Type_Int64
    )) ERROR(unsigned_token, "Unsigned non-integer type");
    return true;
}

bool jitc_parse_type_declarations(jitc_context_t* context, queue_t* tokens, jitc_type_t** type) {
    if (!*type) return NULL;
    if (queue_size(tokens) == 1) return true;
    bool lhs_flag = true;
    jitc_token_t* token = NULL;
    smartptr(queue_t) inner_arr  = queue_new();
    smartptr(queue_t) inner_func = queue_new();
    smartptr(queue_t) inner_decl = NULL;
    const char* name = NULL;
    while (true) {
        if ((token = jitc_token_expect(tokens, TOKEN_ASTERISK))) {
            if (!lhs_flag) ERROR(token, "Pointer declaration on right-hand-side of type");
            jitc_type_t* ptr = malloc(sizeof(jitc_type_t));
            ptr->is_const = ptr->is_unsigned = false;
            ptr->kind = Type_Pointer;
            ptr->alignment = ptr->size = 8;
            ptr->ptr.base = *type;
            *type = ptr;
        }
        else if (jitc_token_expect(tokens, TOKEN_const)) {
            if (!lhs_flag) ERROR(token, "'const' declaration on right-hand-side of type");
            (*type)->is_const = true;
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) {
            name = token->value.string;
            lhs_flag = false;
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_BRACKET_OPEN))) {
            size_t size = -1;
            if (!jitc_token_expect(tokens, TOKEN_BRACKET_CLOSE)) {
                token = queue_peek_ptr(tokens);
                jitc_ast_t* ast = jitc_parse_ast(context, tokens);
                if (!ast) return NULL;
                if (ast->node_type != AST_Integer) ERROR(token, "Expected integer constant");
                size = ast->integer.value;
            }
            queue_push_int(inner_arr, size);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN))) {
            int depth = 0;
            jitc_token_t* start = token;
            smartptr(queue_t) inner = queue_new();
            while (true) {
                token = queue_pop_ptr(tokens);
                if (token->type == TOKEN_END_OF_FILE) ERROR(token, "Unexpected EOF");
                if (token->type == TOKEN_PARENTHESIS_OPEN)  depth++;
                if (token->type == TOKEN_PARENTHESIS_CLOSE) {
                    depth--;
                    if (depth < 0) break;
                }
                queue_push_ptr(inner, token);
            }
            jitc_token_t* eof = malloc(sizeof(jitc_token_t));
            memcpy(eof, queue_peek_ptr(tokens), sizeof(jitc_token_t));
            eof->type = TOKEN_END_OF_FILE;
            queue_push_ptr(inner, eof);

            if (lhs_flag) inner_decl = move(inner);
            else queue_push_ptr(inner_func, move(inner));
            lhs_flag = false;
        }
        else break;
    }
    while (queue_size(inner_arr) > 0) {
        jitc_type_t* arr = malloc(sizeof(jitc_type_t));
        arr->is_const = arr->is_unsigned = false;
        arr->kind = Type_Array;
        arr->arr.base = *type;
        arr->arr.size = queue_pop_int(inner_arr);
        arr->alignment = (*type)->alignment;
        arr->size = arr->arr.size == -1 ? 0 : queue_pop_int(inner_arr) * (*type)->size;
        *type = arr;
    }
    while (queue_size(inner_func) > 0) {
        smartptr(list_t) list = list_new();
        smartptr(queue_t) queue = queue_pop_ptr(inner_func);
        jitc_token_t* comma = NULL;
        while (queue_size(queue) > 1) {
            comma = NULL;
            jitc_type_t* param_type = jitc_parse_type(context, queue, NULL);
            if (!param_type) return NULL;
            list_add_ptr(list, param_type);
            comma = jitc_token_expect(queue, TOKEN_COMMA);
        }
        if (comma) ERROR(comma, "Expected type");
        jitc_type_t* func = malloc(sizeof(jitc_type_t));
        func->is_const = func->is_unsigned = false;
        func->kind = Type_Function;
        func->alignment = func->size = 8;
        func->func.ret = *type;
        func->func.params = move(list);
        *type = func;
    }
    if (inner_decl) if (!jitc_parse_type_declarations(context, inner_decl, type)) return false;
    if (name) (*type)->name = name;
    return true;
}

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype) {
    smartptr(jitc_type_t) type = calloc(sizeof(jitc_type_t), 1);
    if (!jitc_parse_base_type(context, tokens, type, decltype)) return NULL;
    if (!jitc_parse_type_declarations(context, tokens, &type)) return NULL;
    return move(type);
}

jitc_ast_t* jitc_parse_statement(jitc_context_t* context, queue_t* tokens) {
    jitc_token_t* token = NULL;
    if (jitc_token_expect(tokens, TOKEN_if)) {

    }
    else if (jitc_token_expect(tokens, TOKEN_while)) {

    }
    else if (jitc_token_expect(tokens, TOKEN_do)) {

    }
    else if (jitc_token_expect(tokens, TOKEN_for)) {

    }
    else if (jitc_token_expect(tokens, TOKEN_continue)) {

    }
    else if (jitc_token_expect(tokens, TOKEN_break)) {

    }
    else if (jitc_token_expect(tokens, TOKEN_return)) {

    }
    else {
        /*jitc_decltype_t decltype;
        jitc_type_t* type = jitc_parse_type(context, tokens, &decltype);
        if (!type) {
            jitc_ast_t* node = jitc_parse_expression(context, tokens, NULL);
            if (jitc_token_expect(tokens, TOKEN_SEMICOLON)) return node;
            ERROR(NEXT_TOKEN, "Expected ';'");
        }
        if (type->name) return NULL;
        jitc_declare_variable(context, type, decltype);
        while (true) {
            if (jitc_token_expect(tokens, TOKEN_EQUALS)) {
            }
            else if (jitc_token_expect(tokens, TOKEN_COMMA)) {
                jitc_parse_type_declarations(context, tokens, &type);
            }
            else if (jitc_token_expect(tokens, TOKEN_SEMICOLON)) break;
            else ERROR(NEXT_TOKEN, "Expected '=', ';' or ','");
        }*/
    }
    return NULL;
}

jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* tokens) {
    jitc_ast_t* ast = mknode(AST_List);
    ast->list.inner = list_new();
    while (!jitc_token_expect(tokens, TOKEN_END_OF_FILE)) {
        list_add_ptr(ast->list.inner, jitc_parse_statement(context, tokens));
    }
    return ast;
}
