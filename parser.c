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

jitc_type_t* jitc_parse_base_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype) {
    bool is_const = false, is_unsigned = false;
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
        else if ((token = jitc_token_expect(tokens, TOKEN_const)))    is_const    = true;
        else if ((token = jitc_token_expect(tokens, TOKEN_unsigned))) is_unsigned = true;
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
    jitc_type_kind_t kind;
    if (specs == 0 && is_unsigned) specs |= Spec_Int;
    for (; i < sizeof(types) / sizeof(*types); i++) {
        if (types[i].specifiers == specs) {
            kind = types[i].type;
            break;
        }
    }
    if (i == sizeof(types) / sizeof(*types)) ERROR(first_token, "Invalid specifier combination");
    if (is_unsigned && !(
        kind == Type_Int8  || kind == Type_Int16 ||
        kind == Type_Int32 || kind == Type_Int64
    )) ERROR(unsigned_token, "Unsigned non-integer type");
    jitc_type_t* type = jitc_typecache_primitive(context, kind);
    if (is_const) type = jitc_typecache_const(context, type);
    if (is_unsigned) type = jitc_typecache_unsigned(context, type);
    return type;
}

bool jitc_parse_type_declarations(jitc_context_t* context, queue_t* tokens, jitc_type_t** type) {
    typedef enum {
        DeclID_InnerDeclaration,
        DeclID_Function,
        DeclID_Array
    } decl_id_t;

    if (!*type) return NULL;
    if (queue_size(tokens) == 1) return true;
    bool lhs_flag = true;
    jitc_token_t* token = NULL;
    smartptr(stack_t) inner = stack_new();
    const char* name = NULL;
    while (true) {
        if ((token = jitc_token_expect(tokens, TOKEN_ASTERISK))) {
            if (!lhs_flag) ERROR(token, "Pointer declaration on right-hand-side of type");
            *type = jitc_typecache_pointer(context, *type);
        }
        else if (jitc_token_expect(tokens, TOKEN_const)) {
            if (!lhs_flag) ERROR(token, "'const' declaration on right-hand-side of type");
            *type = jitc_typecache_const(context, *type);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) {
            name = token->value.string;
            lhs_flag = false;
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_BRACKET_OPEN))) {
            lhs_flag = false;
            size_t size = -1;
            if (!jitc_token_expect(tokens, TOKEN_BRACKET_CLOSE)) {
                token = queue_peek_ptr(tokens);
                smartptr(jitc_ast_t) ast = jitc_parse_expression(context, tokens, NULL);
                if (!ast) return NULL;
                if (ast->node_type != AST_Integer) ERROR(token, "Expected integer constant");
                size = ast->integer.value;
            }
            stack_push_int(inner, size);
            stack_push_int(inner, DeclID_Array);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN))) {
            int depth = 0;
            jitc_token_t* start = token;
            smartptr(queue_t) func = queue_new();
            while (true) {
                token = queue_pop_ptr(tokens);
                queue_push_ptr(func, token);
                if (token->type == TOKEN_END_OF_FILE) ERROR(token, "Unexpected EOF");
                if (token->type == TOKEN_PARENTHESIS_OPEN)  depth++;
                if (token->type == TOKEN_PARENTHESIS_CLOSE) {
                    depth--;
                    if (depth < 0) break;
                }
            }
            stack_push_ptr(inner, move(func));
            stack_push_int(inner, lhs_flag ? DeclID_InnerDeclaration : DeclID_Function);
            lhs_flag = false;
        }
        else break;
    }
    while (stack_size(inner) > 0) switch (stack_pop_int(inner)) {
        case DeclID_Array: {
            *type = jitc_typecache_array(context, *type, stack_pop_int(inner));
        } break;
        case DeclID_Function: {
            smartptr(list_t) list = list_new();
            smartptr(queue_t) queue = stack_pop_ptr(inner);
            jitc_token_t* comma = NULL;
            while (queue_size(queue) > 1) {
                comma = NULL;
                if (((jitc_token_t*)queue_peek_ptr(queue))->type == TOKEN_TRIPLE_DOT) {
                    queue_pop(queue);
                    list_add_ptr(list, jitc_typecache_primitive(context, Type_Varargs));
                    if (!jitc_token_expect(queue, TOKEN_PARENTHESIS_CLOSE)) ERROR(queue_pop_ptr(queue), "Expected ')'");
                    break;
                }
                jitc_type_t* param_type = jitc_parse_type(context, queue, NULL);
                if (!param_type) return NULL;
                list_add_ptr(list, param_type);
                comma = jitc_token_expect(queue, TOKEN_COMMA);
            }
            if (comma) ERROR(comma, "Expected type");
            *type = jitc_typecache_function(context, *type, list);
        } break;
        case DeclID_InnerDeclaration: {
            smartptr(queue_t) inner_tokens = stack_pop_ptr(inner);
            if (queue_size(inner_tokens) == 1) ERROR(queue_pop_ptr(inner_tokens), "Unexpected ')'");
            if (!jitc_parse_type_declarations(context, inner_tokens, type)) return false;
        } break;
    }
    if (name) (*type)->name = name;
    return true;
}

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype) {
    jitc_type_t* type = NULL;
    if (!(type = jitc_parse_base_type(context, tokens, decltype))) return NULL;
    if (!jitc_parse_type_declarations(context, tokens, &type)) return NULL;
    return type;
}

jitc_ast_t* jitc_parse_expression_operand(jitc_context_t* context, queue_t* tokens) {
    jitc_token_t* token;
    if ((token = jitc_token_expect(tokens, TOKEN_INTEGER))) {
        jitc_ast_t* ast = mknode(AST_Integer);
        ast->integer.is_unsigned = false;
        ast->integer.type_kind = Type_Int32;
        ast->integer.value = token->value.integer;
        return ast;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_FLOAT))) {
        jitc_ast_t* ast = mknode(AST_Floating);
        ast->floating.is_single_precision = false;
        ast->floating.value = token->value.floating;
        return ast;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_STRING))) {
        jitc_ast_t* ast = mknode(AST_StringLit);
        ast->string.ptr = token->value.string;
        return ast;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) {
        jitc_ast_t* ast = mknode(AST_Variable);
        ast->variable.name = token->value.string;
        return ast;
    }
    return NULL;
}

static struct {
    bool rtl_assoc;
    int precedence;
    jitc_binary_op_t type;
} op_info[] = {
    [TOKEN_ASTERISK]                   = { false, 12, Binary_Multiplication },
    [TOKEN_SLASH]                      = { false, 12, Binary_Division },
    [TOKEN_PERCENT]                    = { false, 12, Binary_Modulo },
    [TOKEN_PLUS]                       = { false, 11, Binary_Addition },
    [TOKEN_MINUS]                      = { false, 11, Binary_Subtraction },
    [TOKEN_DOUBLE_LESS_THAN]           = { false, 10, Binary_BitshiftLeft },
    [TOKEN_DOUBLE_GREATER_THAN]        = { false, 10, Binary_BitshiftRight },
    [TOKEN_LESS_THAN]                  = { false, 9,  Binary_LessThan },
    [TOKEN_GREATER_THAN]               = { false, 9,  Binary_GreaterThan },
    [TOKEN_LESS_THAN_EQUALS]           = { false, 9,  Binary_LessThanOrEqualTo },
    [TOKEN_GREATER_THAN_EQUALS]        = { false, 9,  Binary_GreaterThanOrEqualTo },
    [TOKEN_DOUBLE_EQUALS]              = { false, 8,  Binary_Equals },
    [TOKEN_NOT_EQUALS]                 = { false, 8,  Binary_NotEquals },
    [TOKEN_AMPERSAND]                  = { false, 7,  Binary_And },
    [TOKEN_HAT]                        = { false, 6,  Binary_Xor },
    [TOKEN_PIPE]                       = { false, 5,  Binary_Or },
    [TOKEN_DOUBLE_AMPERSAND]           = { false, 4,  Binary_LogicAnd },
    [TOKEN_DOUBLE_PIPE]                = { false, 3,  Binary_LogicOr },
    [TOKEN_QUESTION_MARK]              = { true,  2,  Binary_Tern1 },
    [TOKEN_COLON]                      = { true,  2,  Binary_Tern2 },
    [TOKEN_EQUALS]                     = { true,  1,  Binary_Assignment },
    [TOKEN_PLUS_EQUALS]                = { true,  1,  Binary_AssignAddition },
    [TOKEN_MINUS_EQUALS]               = { true,  1,  Binary_AssignSubtraction },
    [TOKEN_ASTERISK_EQUALS]            = { true,  1,  Binary_AssignMultiplication },
    [TOKEN_SLASH_EQUALS]               = { true,  1,  Binary_AssignDivision },
    [TOKEN_PERCENT_EQUALS]             = { true,  1,  Binary_AssignModulo },
    [TOKEN_DOUBLE_LESS_THAN_EQUALS]    = { true,  1,  Binary_AssignBitshiftLeft },
    [TOKEN_DOUBLE_GREATER_THAN_EQUALS] = { true,  1,  Binary_AssignBitshiftRight },
    [TOKEN_AMPERSAND_EQUALS]           = { true,  1,  Binary_AssignAnd },
    [TOKEN_PIPE_EQUALS]                = { true,  1,  Binary_AssignOr },
    [TOKEN_HAT_EQUALS]                 = { true,  1,  Binary_AssignXor },
};

jitc_ast_t* jitc_shunting_yard(jitc_context_t* context, list_t* expr) {
    #define reduce() { \
        jitc_token_t* op = stack_pop_ptr(op_stack); \
        jitc_ast_t* node  = mknode(AST_Binary); \
        node->binary.left = stack_pop_ptr(node_stack); \
        node->binary.right = stack_pop_ptr(node_stack); \
        node->binary.operation = op_info[op->type].type; \
        stack_push_ptr(node_stack, node); \
    }
    smartptr(stack_t) op_stack = stack_new();
    smartptr(stack_t) node_stack = stack_new();
    for (int i = 0; i < list_size(expr); i++) {
        if (i % 2 == 0) {
            stack_push_ptr(node_stack, list_get_ptr(expr, i));
            continue;
        }
        jitc_token_t* op = list_get_ptr(expr, i);
        while (stack_size(op_stack) > 0) {
            jitc_token_t* top = stack_peek_ptr(op_stack);
            if (top->type == TOKEN_QUESTION_MARK) break;
            int top_precedence = op_info[top->type].precedence;
            int cur_precedence = op_info[ op->type].precedence;
            bool rtl_associativity = op_info[op->type].rtl_assoc;
            if (( rtl_associativity && top_precedence <= cur_precedence) ||
                (!rtl_associativity && top_precedence <  cur_precedence)
            ) break;
            reduce();
        }
        stack_push_ptr(op_stack, op);
    }
    while (stack_size(op_stack) > 0) reduce();
    return stack_pop_ptr(node_stack);
}

jitc_ast_t* jitc_precompute(jitc_context_t* context, jitc_ast_t* ast, jitc_type_t** exprtype) {
    return ast;
}

jitc_ast_t* jitc_parse_expression(jitc_context_t* context, queue_t* tokens, jitc_type_t** exprtype) {
    smartptr(list_t) expr = list_new();
    while (true) {
        jitc_ast_t* operand = try(jitc_parse_expression_operand(context, tokens));
        jitc_token_t* token = queue_peek_ptr(tokens);
        list_add_ptr(expr, operand);
        if (op_info[token->type].precedence == 0) break;
        list_add_ptr(expr, queue_pop_ptr(tokens));
    }
    return jitc_precompute(context, jitc_shunting_yard(context, expr), exprtype);
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
        smartptr(jitc_ast_t) node = try(jitc_parse_expression(context, tokens, NULL));
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON)) ERROR(NEXT_TOKEN, "Expected ';'");
        return move(node);
    }
    return NULL;
}

jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* tokens) {
    smartptr(jitc_ast_t) ast = mknode(AST_List);
    ast->list.inner = list_new();
    while (!jitc_token_expect(tokens, TOKEN_END_OF_FILE)) {
        list_add_ptr(ast->list.inner, try(jitc_parse_statement(context, tokens)));
    }
    return move(ast);
}

void jitc_destroy_ast(jitc_ast_t* ast) {
    if (!ast) return;
    switch (ast->node_type) {
        case AST_Unary:
            jitc_destroy_ast(ast->unary.inner);
            break;
        case AST_Binary:
            jitc_destroy_ast(ast->binary.left);
            jitc_destroy_ast(ast->binary.right);
            break;
        case AST_Ternary:
            jitc_destroy_ast(ast->ternary.when);
            jitc_destroy_ast(ast->ternary.then);
            jitc_destroy_ast(ast->ternary.otherwise);
            break;
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                jitc_destroy_ast(list_get_ptr(ast->list.inner, i));
            }
            list_delete(ast->list.inner);
            break;
        case AST_Function:
            jitc_destroy_ast(ast->function.body);
            break;
        case AST_Loop:
            jitc_destroy_ast(ast->loop.cond);
            jitc_destroy_ast(ast->loop.body);
            break;
        case AST_Return:
            jitc_destroy_ast(ast->ret.expr);
            break;
        case AST_WalkStruct:
            jitc_destroy_ast(ast->walk_struct.struct_ptr);
            break;
        default: break;
    }
    free(ast);
}
