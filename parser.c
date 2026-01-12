#include "dynamics.h"
#include "jitc.h"
#include "jitc_internal.h"
#include "cleanups.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>

#define NEXT_TOKEN (&queue_peek(tokens))
#define throw_impl(...) jitc_error_set(context, jitc_error_parser(__VA_ARGS__))

// passed as "min_prec" into jitc_parse_expression
// commas have precedence of 1
#define EXPR_NO_COMMAS 2
#define EXPR_WITH_COMMAS 1

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

static jitc_ast_t* mknode(jitc_ast_type_t type, jitc_token_t* token) {
    jitc_ast_t* ast = calloc(sizeof(jitc_ast_t), 1);
    ast->node_type = type;
    ast->token = token;
    if (type == AST_List || type == AST_Scope) ast->list.inner = list_new(jitc_ast_t*);
    return ast;
}

static bool is_integer(jitc_type_t* type) {
    return type->kind == Type_Int8 || type->kind == Type_Int16 || type->kind == Type_Int32 || type->kind == Type_Int64 || type->kind == Type_Enum;
}

static bool is_floating(jitc_type_t* type) {
    return type->kind == Type_Float32 || type->kind == Type_Float64;
}

static bool is_pointer(jitc_type_t* type) {
    return type->kind == Type_Pointer;
}

static bool is_number(jitc_type_t* type) {
    return is_integer(type) || is_floating(type);
}

static bool is_scalar(jitc_type_t* type) {
    return is_number(type) || is_pointer(type);
}

static bool is_struct(jitc_type_t* type) {
    return type->kind == Type_Struct || type->kind == Type_Union;
}

static bool is_function(jitc_type_t* type) {
    return type->kind == Type_Pointer && (type->ptr.prev == Type_Function || type->ptr.base->kind == Type_Function);
}

static bool is_decayed_pointer(jitc_type_t* type) {
    return type->kind == Type_Pointer && type->ptr.prev != Type_Pointer;
}

static bool is_constant(jitc_ast_t* ast) {
    return ast->node_type == AST_Integer || ast->node_type == AST_Floating || ast->node_type == AST_StringLit;
}

static bool is_lvalue(jitc_ast_t* ast) {
    return ast->node_type == AST_Variable || (ast->node_type == AST_Unary && ast->unary.operation == Unary_Dereference) || ast->node_type == AST_WalkStruct;
}

bool jitc_peek_type(jitc_context_t* context, queue_t* _tokens) {
    queue(jitc_token_t)* tokens = _tokens;
    switch (NEXT_TOKEN->type) {
        case TOKEN_extern:
        case TOKEN_static:
        case TOKEN_typedef:
        case TOKEN_const:
        case TOKEN_unsigned:
        case TOKEN_char:
        case TOKEN_short:
        case TOKEN_int:
        case TOKEN_float:
        case TOKEN_double:
        case TOKEN_void:
        case TOKEN_long:
        case TOKEN_bool:
        case TOKEN_volatile:
        case TOKEN_register:
        case TOKEN_restrict:
        case TOKEN_inline:
        case TOKEN_typeof:
        case TOKEN_struct:
        case TOKEN_union:
        case TOKEN_enum:
        case TOKEN_preserve:
        case TOKEN_hotswap:
            return true;
        case TOKEN_IDENTIFIER: {
            jitc_variable_t* var = jitc_get_variable(context, NEXT_TOKEN->value.string);
            if (var == NULL) return false;
            return var->decltype == Decltype_Typedef;
        }
        default: return false;
    }
}

bool jitc_parse_type_declarations(jitc_context_t* context, queue_t* _tokens, jitc_type_t** type) {
    queue(jitc_token_t)* tokens = _tokens;
    typedef struct {
        enum {
            DeclID_InnerDeclaration,
            DeclID_Function,
            DeclID_Array
        } id;
        jitc_token_t* starting_token;
        union {
            size_t array_size;
            queue(jitc_token_t*)* tokens;
        };
    } declstack_t;

    if (!*type) return NULL;
    if (queue_size(tokens) == 1) return true;
    bool lhs_flag = true;
    jitc_token_t* token = NULL;
    smartptr(stack(declstack_t)) declstack = stack_new(declstack_t);
    const char* name = NULL;
    while (true) {
        if ((token = jitc_token_expect(tokens, TOKEN_ASTERISK))) {
            if (!lhs_flag) throw(token, "Pointer declaration on right-hand-side of type");
            *type = jitc_typecache_pointer(context, *type);
        }
        else if (jitc_token_expect(tokens, TOKEN_const)) {
            if (!lhs_flag) throw(token, "'const' declaration on right-hand-side of type");
            *type = jitc_typecache_const(context, *type);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) {
            name = token->value.string;
            lhs_flag = false;
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_BRACKET_OPEN))) {
            lhs_flag = false;
            size_t size = -1;
            jitc_token_t* starting_token = token;
            if (!jitc_token_expect(tokens, TOKEN_BRACKET_CLOSE)) {
                token = NEXT_TOKEN;
                smartptr(jitc_ast_t) ast = try(jitc_parse_expression(context, tokens, EXPR_NO_COMMAS, NULL));
                if (ast->node_type != AST_Integer) throw(token, "Expected integer constant");
                size = ast->integer.value;
                if (!jitc_token_expect(tokens, TOKEN_BRACKET_CLOSE)) throw(NEXT_TOKEN, "Expected ']'");
            }
            stack_push(declstack) = (declstack_t){
                .id = DeclID_Array,
                .starting_token = starting_token,
                .array_size = size
            };
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN))) {
            int depth = 0;
            jitc_token_t* starting_token = token;
            smartptr(queue(jitc_token_t)) func = queue_new(jitc_token_t);
            while (true) {
                token = &queue_pop(tokens);
                queue_push(func) = *token;
                if (token->type == TOKEN_END_OF_FILE) throw(token, "Unexpected EOF");
                if (token->type == TOKEN_PARENTHESIS_OPEN)  depth++;
                if (token->type == TOKEN_PARENTHESIS_CLOSE) {
                    depth--;
                    if (depth < 0) break;
                }
            }
            stack_push(declstack) = (declstack_t){
                .id = lhs_flag ? DeclID_InnerDeclaration : DeclID_Function,
                .starting_token = starting_token,
                .tokens = (void*)move(func)
            };
            lhs_flag = false;
        }
        else break;
    }
    while (stack_size(declstack) > 0) {
        declstack_t* item = &stack_pop(declstack);
        switch (item->id) {
            case DeclID_Array: {
                if (!jitc_validate_type(*type, TypePolicy_NoFunction)) throw(item->starting_token, "Array cannot contain a function");
                if (!jitc_validate_type(*type, TypePolicy_NoIncomplete)) throw(item->starting_token, "Array with incomplete type");
                *type = jitc_typecache_array(context, *type, item->array_size);
            } break;
            case DeclID_Function: {
                jitc_token_t* comma = NULL;
                smartptr(list(jitc_type_t*)) list = list_new(jitc_type_t*);
                smartptr(queue(jitc_token_t)) queue = (void*)item->tokens;
                if (!jitc_validate_type(*type, TypePolicy_NoDerived)) throw(item->starting_token, "Function cannot return an array or function");
                while (queue_size(queue) > 1) {
                    comma = NULL;
                    token = &queue_peek(queue);
                    if (token->type == TOKEN_TRIPLE_DOT) {
                        queue_pop(queue);
                        list_add(list) = jitc_typecache_primitive(context, Type_Varargs);
                        if (!jitc_token_expect(queue, TOKEN_PARENTHESIS_CLOSE)) throw(&queue_pop(queue), "Expected ')'");
                        break;
                    }
                    jitc_type_t* param_type = jitc_typecache_decay(context, try(jitc_parse_type(context, queue, NULL, NULL)));
                    if (param_type->kind == Type_Void) {
                        if (param_type->name) throw(token, "'void' cannot have a name");
                        if (list_size(list) > 0) throw(token, "'void' must be the only parameter");
                        break;
                    }
                    list_add(list) = param_type;
                    comma = jitc_token_expect(queue, TOKEN_COMMA);
                }
                if (queue_size(queue) > 1) throw(&queue_peek(queue), "Expected ')'");
                if (comma) throw(comma, "Expected type");
                *type = jitc_typecache_function(context, *type, list);
            } break;
            case DeclID_InnerDeclaration: {
                smartptr(queue(jitc_token_t*)) inner_tokens = (void*)item->tokens;
                if (queue_size(inner_tokens) == 1) throw(queue_pop(inner_tokens), "Unexpected ')'");
                if (!jitc_parse_type_declarations(context, inner_tokens, type)) return false;
            } break;
        }
    }
    if (name) *type = jitc_typecache_named(context, *type, name);
    return true;
}

jitc_type_t* jitc_parse_base_type(jitc_context_t* context, queue_t* _tokens, jitc_decltype_t* decltype, jitc_preserve_t* preserve_policy) {
    queue(jitc_token_t)* tokens = _tokens;
    bool is_const = false, is_unsigned = false;
    jitc_specifiers_t specs = 0;
    jitc_token_t* token = NULL;
    jitc_token_t* unsigned_token = NULL, *first_token = NULL;
    jitc_type_t* type = NULL;
    size_t align = -1;
    while (true) {
        jitc_decltype_t decl = Decltype_None;
        jitc_preserve_t prsv = Preserve_IfConst;
        jitc_specifiers_t new_specs = 0;
        if      ((token = jitc_token_expect(tokens, TOKEN_extern)))   decl = Decltype_Extern;
        else if ((token = jitc_token_expect(tokens, TOKEN_static)))   decl = Decltype_Static;
        else if ((token = jitc_token_expect(tokens, TOKEN_typedef)))  decl = Decltype_Typedef;
        else if ((token = jitc_token_expect(tokens, TOKEN_preserve))) prsv = Preserve_Always;
        else if ((token = jitc_token_expect(tokens, TOKEN_hotswap)))  prsv = Preserve_Never;
        else if ((token = jitc_token_expect(tokens, TOKEN_const)))    is_const    = true;
        else if ((token = jitc_token_expect(tokens, TOKEN_unsigned))) is_unsigned = true;
        else if ((token = jitc_token_expect(tokens, TOKEN_char)))     new_specs |= Spec_Char;
        else if ((token = jitc_token_expect(tokens, TOKEN_short)))    new_specs |= Spec_Short;
        else if ((token = jitc_token_expect(tokens, TOKEN_int)))      new_specs |= Spec_Int;
        else if ((token = jitc_token_expect(tokens, TOKEN_float)))    new_specs |= Spec_Float;
        else if ((token = jitc_token_expect(tokens, TOKEN_double)))   new_specs |= Spec_Double;
        else if ((token = jitc_token_expect(tokens, TOKEN_void)))     new_specs |= Spec_Void;
        else if ((token = jitc_token_expect(tokens, TOKEN_long)))     new_specs |= specs & Spec_Long1 ? Spec_Long2 : Spec_Long1;
        else if (
            jitc_token_expect(tokens, TOKEN_volatile) ||
            jitc_token_expect(tokens, TOKEN_register) ||
            jitc_token_expect(tokens, TOKEN_restrict) ||
            jitc_token_expect(tokens, TOKEN_inline)
        ) (void)0; // no-op, maybe implement later?
        else if ((token = jitc_token_expect(tokens, TOKEN_bool))) {
            new_specs |= Spec_Char;
            is_unsigned = true;
        }
        else if ((token = NEXT_TOKEN)->type == TOKEN_IDENTIFIER) {
            jitc_variable_t* variable = jitc_get_variable(context, token->value.string);
            if (specs != 0 || !variable || variable->decltype != Decltype_Typedef) break;
            type = jitc_typecache_named(context, variable->type, NULL);
            queue_pop(tokens);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_typeof))) {
            if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN)) throw(NEXT_TOKEN, "Expected '('");
            if (jitc_peek_type(context, tokens)) type = try(jitc_parse_type(context, tokens, NULL, NULL));
            else jitc_destroy_ast(try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, &type)));
            if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_struct)) || (token = jitc_token_expect(tokens, TOKEN_union))) {
            jitc_token_t* name_token = jitc_token_expect(tokens, TOKEN_IDENTIFIER);
            if (jitc_token_expect(tokens, TOKEN_BRACE_OPEN)) {
                smartptr(list(jitc_type_t*)) fields = list_new(jitc_type_t*);
                while (!jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) {
                    while (NEXT_TOKEN->type == TOKEN_SEMICOLON) queue_pop(tokens);
                    jitc_type_t* field_type = try(jitc_parse_base_type(context, tokens, NULL, NULL));
                    while (true) {
                        field_type = jitc_typecache_named(context, field_type, NULL);
                        try(jitc_parse_type_declarations(context, tokens, &field_type));
                        if (!jitc_validate_type(field_type, TypePolicy_NoIncomplete)) throw(NEXT_TOKEN, "Field has incomplete type");
                        if (field_type->name) if (jitc_struct_field_exists_list(fields, field_type->name))
                            throw(NEXT_TOKEN, "Duplicate field '%s'", field_type->name);
                        list_add(fields) = field_type;
                        if (jitc_token_expect(tokens, TOKEN_COMMA)) continue;
                        if (jitc_token_expect(tokens, TOKEN_SEMICOLON)) break;
                        throw(NEXT_TOKEN, "Expected ';' or ','");
                    }
                }
                type = (token->type == TOKEN_struct ? jitc_typecache_struct : jitc_typecache_union)(context, fields, token);
                if (name_token) if (!jitc_declare_tagged_type(context, type, name_token->value.string))
                    throw(name_token, "%s '%s' already defined", token->type == TOKEN_struct ? "Struct" : "Union", name_token->value.string);
            }
            else if (!name_token) throw(NEXT_TOKEN, "Expected identifier or '{'");
            else {
                type = jitc_get_tagged_type(context, token->type ? Type_Struct : Type_Union, name_token->value.string);
                if (!type) type = (token->type == TOKEN_struct ? jitc_typecache_structref : jitc_typecache_unionref)(context, name_token->value.string);
            }
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_enum))) {
            jitc_token_t* name_token = jitc_token_expect(tokens, TOKEN_IDENTIFIER);
            jitc_type_kind_t kind = Type_Int32;
            bool is_unsigned = false;
            bool force_definition = false;
            if (jitc_token_expect(tokens, TOKEN_COLON)) {
                force_definition = true;
                jitc_token_t* type_token = NEXT_TOKEN;
                jitc_type_t* type = try(jitc_parse_base_type(context, tokens, NULL, NULL));
                if (!is_integer(type)) throw(type_token, "Enum base type must be an integer");
                kind = type->kind;
            }
            jitc_type_t* value_type = jitc_typecache_primitive(context, kind);
            if (is_unsigned) value_type = jitc_typecache_unsigned(context, value_type);
            if (jitc_token_expect(tokens, TOKEN_BRACE_OPEN)) {
                uint64_t prev_value = -1;
                while (!jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) {
                    jitc_token_t* id = NULL;
                    if (!(id = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) throw(NEXT_TOKEN, "Expected identifier");
                    const char* name = id->value.string;
                    uint64_t value = prev_value + 1;
                    if ((jitc_token_expect(tokens, TOKEN_EQUALS))) {
                        jitc_ast_t* ast = try(jitc_parse_expression(context, tokens, EXPR_NO_COMMAS, NULL));
                        if (ast->node_type != AST_Integer) throw(ast->token, "Value must be an integer");
                        value = ast->integer.value;
                    }
                    prev_value = value;
                    if (!jitc_declare_variable(context, jitc_typecache_named(context, value_type, name), Decltype_EnumItem, Preserve_IfConst, value))
                        throw(id, "Symbol '%s' already defined", name);
                    if (jitc_token_expect(tokens, TOKEN_COMMA)) continue;
                    if (jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) break;
                    throw(NEXT_TOKEN, "Expected ',' or '}'");
                }
                type = jitc_typecache_enum(context, value_type);
                if (name_token) jitc_declare_tagged_type(context, type, name_token->value.string);
            }
            else {
                if (force_definition) throw(NEXT_TOKEN, "Expected '{'");
                if (!name_token) throw(NEXT_TOKEN, "Expected identifier or '{'");
                type = jitc_get_tagged_type(context, Type_Enum, name_token->value.string);
                if (!type) type = jitc_typecache_enumref(context, name_token->value.string);
                else type = type->ptr.base;
            }
        }
        else {
            if (!first_token) throw(NEXT_TOKEN, "Expected type");
            break;
        }
        if ((specs & new_specs) != 0) throw(token, "Duplicate specifier");
        specs |= new_specs;
        if (decl != Decltype_None) {
            if (!decltype) throw(token, "Declaration type illegal here");
            if (*decltype != Decltype_None) throw(token, "Duplicate declaration type");
            *decltype = decl;
        }
        if (prsv != Preserve_IfConst) {
            if (!preserve_policy) throw(token, "Preservation policy illegal here");
            if (*preserve_policy != Preserve_IfConst) throw(token, "Duplicate preservation policy");
            *preserve_policy = prsv;
        }
        if (token->type == TOKEN_unsigned && !unsigned_token) unsigned_token = token;
        if (!first_token) first_token = token;
    }
    if (!type) {
        int i = 0;
        jitc_type_kind_t kind;
        if (specs == 0 && is_unsigned) specs |= Spec_Int;
        for (; i < sizeof(types) / sizeof(*types); i++) {
            if (types[i].specifiers == specs) {
                kind = types[i].type;
                break;
            }
        }
        if (i == sizeof(types) / sizeof(*types)) throw(first_token, "Invalid specifier combination");
        if (is_unsigned && !(
            kind == Type_Int8  || kind == Type_Int16 ||
            kind == Type_Int32 || kind == Type_Int64
        )) throw(unsigned_token, "Unsigned non-integer type");
        type = jitc_typecache_primitive(context, kind);
    }
    if (is_const) type = jitc_typecache_const(context, type);
    if (is_unsigned) type = jitc_typecache_unsigned(context, type);
    if (align != -1) type = jitc_typecache_align(context, type, align);
    return type;
}

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype, jitc_preserve_t* preserve_policy) {
    jitc_type_t* type = NULL;
    if (!(type = jitc_parse_base_type(context, tokens, decltype, preserve_policy))) return NULL;
    if (!jitc_parse_type_declarations(context, tokens, &type)) return NULL;
    return type;
}

jitc_type_t* jitc_type_promotion(jitc_context_t* context, jitc_type_t* left, jitc_type_t* right, bool min_int32) {
    if (!is_number(left))  return left;
    if (!is_number(right)) return right;
    jitc_type_kind_t kind = left->kind > right->kind ? left->kind : right->kind;
    bool is_unsigned = left->is_unsigned || right->is_unsigned;
    if (min_int32 && kind < Type_Int32) kind = Type_Int32;
    if (kind == Type_Float32 || kind == Type_Float64) is_unsigned = false;
    jitc_type_t* type = jitc_typecache_primitive(context, kind);
    if (is_unsigned) type = jitc_typecache_unsigned(context, type);
    return type;
}

bool jitc_can_cast(jitc_type_t* from, jitc_type_t* to, bool explicit, bool is_zero) {
    if (from->kind == Type_Void || to->kind == Type_Void) return false;
    if (from == to) return true;
    if (
        from->kind == Type_Struct ||
        from->kind == Type_Union ||
          to->kind == Type_Struct ||
          to->kind == Type_Union
    ) return false;
    if (is_floating(from) && is_pointer(to)) return false;
    if (is_floating(to) && is_pointer(from)) return false;
    if (!explicit && is_integer(from) && is_pointer(to) && from->kind != Type_Int64 && !is_zero) return false;
    if (!explicit && is_integer(to) && is_pointer(from) &&   to->kind != Type_Int64 && !is_zero) return false;
    return true;
}

jitc_ast_t* jitc_cast(jitc_context_t* context, jitc_ast_t* node, jitc_type_t* type, bool explicit, jitc_token_t* cast_token) {
    if (jitc_typecmp(context, node->exprtype, type)) return node;
    bool is_zero = node->node_type == AST_Integer && node->integer.value == 0;
    if (!jitc_can_cast(node->exprtype, type, explicit, is_zero)) throw(cast_token, "Unable to perform cast");
    switch (node->node_type) {
        case AST_Integer:
            if (is_floating(type)) {
                if (type->is_unsigned) node->floating.value = node->integer.value;
                else node->floating.value = (int64_t)node->integer.value;
                node->floating.is_single_precision = type->kind == Type_Float32;
                node->node_type = AST_Floating;
                node->exprtype = jitc_typecache_primitive(context,
                    node->floating.is_single_precision
                    ? Type_Float32
                    : Type_Float64
                );
            }
            else {
                bool is_negative = !node->integer.is_unsigned && ((node->integer.value >> 63) & 1);
                node->integer.type_kind = is_pointer(type) ? Type_Int64 : type->kind;
                node->integer.is_unsigned = is_pointer(type) ? true : type->is_unsigned;
                memset((char*)&node->integer.value + type->size, is_negative ? 0xFF : 0x00, 8 - type->size);
                node->exprtype = jitc_typecache_primitive(context, node->integer.type_kind);
                if (node->integer.is_unsigned) node->exprtype = jitc_typecache_unsigned(context, node->exprtype);
            }
            break;
        case AST_Floating:
            if (is_floating(type)) {
                node->floating.is_single_precision = type->kind == Type_Float32;
                node->exprtype = jitc_typecache_primitive(context,
                    node->floating.is_single_precision
                    ? Type_Float32
                    : Type_Float64
                );
            }
            else {
                node->node_type = AST_Integer;
                node->integer.value = node->floating.value;
                node->integer.type_kind = type->kind;
                node->integer.is_unsigned = type->is_unsigned;
                node->exprtype = jitc_typecache_primitive(context, node->integer.type_kind);
                if (node->integer.is_unsigned) node->exprtype = jitc_typecache_unsigned(context, node->exprtype);
            }
            break;
        case AST_StringLit:
            node->node_type = AST_Integer;
            node->integer.type_kind = is_pointer(type) ? Type_Int64 : type->kind;
            node->integer.is_unsigned = is_pointer(type) ? true : type->is_unsigned;
            node->exprtype = jitc_typecache_primitive(context, Type_Int8);
            node->exprtype = jitc_typecache_const(context, node->exprtype);
            node->exprtype = jitc_typecache_pointer(context, node->exprtype);
            break;
        default: {
            jitc_ast_t* cast = mknode(AST_Binary, node->token);
            jitc_ast_t* type_node = mknode(AST_Type, node->token);
            type_node->type.type = type;
            cast->exprtype = type;
            cast->binary.operation = Binary_Cast;
            cast->binary.left = node;
            cast->binary.right = type_node;
            node = cast;
        } break;
    }
    return node;
}

jitc_ast_t* jitc_process_ast(jitc_context_t* context, jitc_ast_t* ast, jitc_type_t** exprtype) {
    jitc_ast_t* node = ast;
    switch (node->node_type) {
        case AST_Integer: if (!node->exprtype) {
            node->exprtype = jitc_typecache_primitive(context, node->integer.type_kind);
            if (node->integer.is_unsigned) node->exprtype = jitc_typecache_unsigned(context, node->exprtype);
        } break;
        case AST_Floating: if (!node->exprtype) {
            node->exprtype = jitc_typecache_primitive(context,
                node->floating.is_single_precision
                ? Type_Float32
                : Type_Float64
            );
        } break;
        case AST_StringLit: if (!node->exprtype) {
            node->exprtype = jitc_typecache_primitive(context, Type_Int8);
            node->exprtype = jitc_typecache_const(context, node->exprtype);
            node->exprtype = jitc_typecache_pointer(context, node->exprtype);
        } break;
        case AST_Variable: if (!node->exprtype) {
            jitc_variable_t* variable = jitc_get_variable(context, node->variable.name);
            if (!variable) throw(node->token, "Undefined variable '%s'", node->variable.name);
            if (variable->decltype == Decltype_EnumItem) {
                node->node_type = AST_Integer;
                node->integer.type_kind = variable->type->kind;
                node->integer.is_unsigned = variable->type->is_unsigned;
                node->integer.value = variable->enum_value;
            }
            node->exprtype = jitc_typecache_decay(context, variable->type);
        } break;
        case AST_WalkStruct: if (!node->exprtype) {
            jitc_type_t* type;
            node->walk_struct.struct_ptr = try(jitc_process_ast(context, node->walk_struct.struct_ptr, &type));
            if (!jitc_validate_type(type, TypePolicy_NoUndefTags)) {
                jitc_type_t* resolved = jitc_get_tagged_type(context, type->kind, type->ref.name);
                if (!resolved) throw(node->token, "Cannot access an incomplete struct");
                type = node->walk_struct.struct_ptr->exprtype = resolved;
            }
            if (!is_struct(type)) throw(node->token, "Not a struct type");
            if (!jitc_walk_struct(type, node->walk_struct.field_name, &node->exprtype, &node->walk_struct.offset))
                throw(node->token, "Field '%s' not found", node->walk_struct.field_name);
        } break;
        case AST_Initializer:
            node->init.store_to = try(jitc_process_ast(context, node->init.store_to, NULL));
            node->exprtype = node->init.type;
            break;

        case AST_Unary: switch (node->unary.operation) {
            case Unary_SuffixIncrement:
            case Unary_SuffixDecrement:
            case Unary_PrefixIncrement:
            case Unary_PrefixDecrement:
            case Unary_AddressOf:
                node->unary.inner = try(jitc_process_ast(context, node->unary.inner, &node->exprtype));
                if (node->unary.operation == Unary_AddressOf) {
                    if (node->unary.inner->node_type == AST_Unary && node->unary.inner->unary.operation == Unary_Dereference) {
                        jitc_ast_t* tmp = node->unary.inner->unary.inner;
                        replace(node->unary.inner) = tmp;
                        replace(node) = tmp;
                        break;
                    }
                    else node->exprtype = jitc_typecache_pointer(context, node->exprtype);
                }
                else {
                    if (node->exprtype->is_const) throw(node->token, "Assigning to a const");
                    if (!is_scalar(node->exprtype)) throw(node->token, "Operand must be a scalar type");
                    if (is_pointer(node->exprtype)) node->unary.operation += Unary_PtrSuffixIncrement - Unary_SuffixIncrement;
                }
                if (!is_lvalue(node->unary.inner)) throw(node->token, "Operand must be an lvalue");
                break;
            case Unary_Dereference:
                node->unary.inner = try(jitc_process_ast(context, node->unary.inner, &node->exprtype));
                if (node->unary.inner->node_type == AST_Unary && node->unary.inner->unary.operation == Unary_AddressOf) {
                    jitc_ast_t* tmp = node->unary.inner->unary.inner;
                    replace(node->unary.inner) = tmp;
                    replace(node) = tmp;
                }
                else {
                    if (!is_pointer(node->exprtype)) throw(node->token, "Operand must be a pointer type");
                    if (is_function(node->exprtype)) throw(node->token, "Cannot dereference a function");
                    if (node->exprtype->ptr.base->kind == Type_Void) throw(node->token, "Dereferencing void pointer");
                    node->exprtype = node->exprtype->ptr.base;
                }
                break;
            case Unary_ArithPlus:
            case Unary_ArithNegate:
                node->unary.inner = try(jitc_process_ast(context, node->unary.inner, &node->exprtype));
                if (!is_number(node->exprtype)) throw(node->token, "Operand must be a numeric type");
                if (node->unary.operation == Unary_ArithPlus) replace(node) = node->unary.inner;
                else {
                    if (node->unary.inner->node_type == AST_Integer) {
                        jitc_ast_t* inner = node->unary.inner;
                        inner->integer.value = -inner->integer.value;
                        replace(node) = inner;
                    }
                    else if (node->unary.inner->node_type == AST_Floating) {
                        jitc_ast_t* inner = node->unary.inner;
                        inner->floating.value = -inner->floating.value;
                        replace(node) = inner;
                    }
                    else if (node->unary.inner->node_type == AST_Unary && node->unary.inner->unary.operation == Unary_ArithNegate) {
                        jitc_ast_t* tmp = node->unary.inner->unary.inner;
                        replace(node->unary.inner) = tmp;
                        replace(node) = tmp;
                    }
                }
                break;
            case Unary_BinaryNegate:
                node->unary.inner = try(jitc_process_ast(context, node->unary.inner, &node->exprtype));
                if (!is_integer(node->exprtype)) throw(node->token, "Operand must be an integer type");
                if (is_constant(node->unary.inner)) {
                    jitc_ast_t* inner = node->unary.inner;
                    inner->integer.value = ~inner->integer.value;
                    replace(node) = inner;
                }
                else if (node->unary.inner->node_type == AST_Unary && node->unary.inner->unary.operation == Unary_BinaryNegate) {
                    jitc_ast_t* tmp = node->unary.inner->unary.inner;
                    replace(node->unary.inner) = tmp;
                    replace(node) = tmp;
                }
                break;
            case Unary_LogicNegate:
                node->unary.inner = try(jitc_process_ast(context, node->unary.inner, &node->exprtype));
                if (is_struct(node->exprtype)) throw(node->token, "Negating a non-scalar type");
                if (node->unary.inner->node_type == AST_Unary && node->unary.inner->unary.operation == Unary_LogicNegate) {
                    jitc_ast_t* inner = node->unary.inner;
                    if (inner->unary.inner->node_type == AST_Unary && inner->unary.inner->unary.operation == Unary_LogicNegate) {
                        jitc_ast_t* tmp = inner->unary.inner->unary.inner;
                        replace(inner->unary.inner) = tmp;
                        replace(node->unary.inner) = tmp;
                        replace(node) = tmp;
                    }
                }
                if (is_constant(node->unary.inner)) {
                    jitc_ast_t* inner = node->unary.inner;
                    node->node_type = AST_Integer;
                    node->integer.value = !(is_decayed_pointer(node->exprtype) ? 1 : node->unary.inner->node_type == AST_Floating
                        ? inner->floating.value
                        : inner->integer.value
                    );
                    node->integer.type_kind = Type_Int8;
                    node->integer.is_unsigned = true;
                    jitc_destroy_ast(inner);
                }
                node->exprtype = jitc_typecache_primitive(context, Type_Int8);
                node->exprtype = jitc_typecache_unsigned(context, node->exprtype);
                break;
            default: break;
        } break;
        case AST_Binary: switch (node->binary.operation) {
            case Binary_Cast: {
                jitc_ast_t* left = node->binary.left;
                jitc_ast_t* right = node->binary.right;
                replace(node) = try(jitc_cast(context,
                    try(jitc_process_ast(context, left, NULL)),
                    right->type.type, true, right->token
                ));
            } break;
            case Binary_FunctionCall: {
                node->binary.left = try(jitc_process_ast(context, node->binary.left, &node->exprtype));
                if (!is_function(node->exprtype)) throw(node->token, "Calling a non-function");
                jitc_type_t* func = node->exprtype->ptr.base;
                list(jitc_ast_t*)* list = (void*)node->binary.right->list.inner;
                size_t num_fixed_args = func->func.num_params;
                bool has_varargs = func->func.num_params != 0 && func->func.params[func->func.num_params - 1]->kind == Type_Varargs;
                node->exprtype = func->func.ret;
                if (has_varargs) num_fixed_args--;
                if (list_size(list) < num_fixed_args || (!has_varargs && list_size(list) != num_fixed_args)) throw(node->token,
                    "Incorrect number of arguments (got %d, %s %d)",
                    list_size(list), has_varargs ? "minimum" : "expected", func->func.num_params
                );
                for (size_t i = 0; i < list_size(list); i++) {
                    jitc_ast_t** param = &list_get(list, i);
                    *param = try(jitc_process_ast(context, *param, NULL));
                    if (i < num_fixed_args)
                        *param = try(jitc_cast(context, *param, func->func.params[i], false, (*param)->token));
                    else if ((*param)->exprtype->kind == Type_Float32)
                        *param = try(jitc_cast(context, *param, jitc_typecache_primitive(context, Type_Float64), false, (*param)->token));
                }
            } break;
            #define ARITHMETIC(op, can_be_ptr) \
                node->binary.left = try(jitc_process_ast(context, node->binary.left, NULL)); \
                node->binary.right = try(jitc_process_ast(context, node->binary.right, NULL)); \
                node->exprtype = jitc_type_promotion(context, node->binary.left->exprtype, node->binary.right->exprtype, true); \
                if (can_be_ptr && is_pointer(node->exprtype)) { \
                    if (is_pointer(node->binary.right->exprtype)) { \
                        if (node->binary.operation == Binary_Subtraction) { \
                            if (is_pointer(node->binary.left->exprtype)) { \
                                if (node->binary.left->exprtype->ptr.base->size != node->binary.right->exprtype->ptr.base->size) \
                                    throw(node->token, "Pointed-to objects have different sizes"); \
                                node->binary.operation = Binary_PtrDiff; \
                                node->exprtype = jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int64)); \
                                break; \
                            } \
                            else throw(node->token, "Pointer subtraction with pointer on RHS of expression"); \
                        } \
                        jitc_ast_t* tmp = node->binary.left; \
                        node->binary.left = node->binary.right; \
                        node->binary.right = tmp; \
                    } \
                    if (is_pointer(node->binary.left->exprtype) && !is_integer(node->binary.right->exprtype)) \
                        throw(node->token, "Pointer arithmetic on a non-integer type"); \
                    node->binary.operation += Binary_PtrAddition - Binary_Addition; \
                    break; \
                } \
                if (!is_number(node->exprtype)) throw(node->token, "Arithmetic operation on a non-%s", can_be_ptr ? "scalar" : "number"); \
                node->binary.left = try(jitc_cast(context, node->binary.left, node->exprtype, false, node->token)); \
                node->binary.right = try(jitc_cast(context, node->binary.right, node->exprtype, false, node->token)); \
                if (is_constant(node->binary.left) && is_constant(node->binary.right)) { \
                    smartptr(jitc_ast_t) left = node->binary.left; \
                    smartptr(jitc_ast_t) right = node->binary.right; \
                    if (is_floating(node->exprtype)) { \
                        node->node_type = AST_Floating; \
                        node->floating.is_single_precision = node->exprtype->kind == Type_Float32; \
                        node->floating.value = left->floating.value op right->floating.value; \
                    } \
                    else { \
                        node->node_type = AST_Integer; \
                        node->integer.type_kind = node->exprtype->kind; \
                        node->integer.is_unsigned = node->exprtype->is_unsigned; \
                        node->integer.value = left->integer.value op right->integer.value; \
                    } \
                } \
                break
            #define BINARY(op) \
                node->binary.left = try(jitc_process_ast(context, node->binary.left, NULL)); \
                node->binary.right = try(jitc_process_ast(context, node->binary.right, NULL)); \
                node->exprtype = jitc_type_promotion(context, node->binary.left->exprtype, node->binary.right->exprtype, true); \
                if (!is_integer(node->exprtype)) throw(node->token, "Bitwise operation on a non-integer"); \
                node->binary.left = try(jitc_cast(context, node->binary.left, node->exprtype, false, node->token)); \
                node->binary.right = try(jitc_cast(context, node->binary.right, node->exprtype, false, node->token)); \
                if (is_constant(node->binary.left) && is_constant(node->binary.right)) { \
                    smartptr(jitc_ast_t) left = node->binary.left; \
                    smartptr(jitc_ast_t) right = node->binary.right; \
                    node->node_type = AST_Integer; \
                    node->integer.type_kind = node->exprtype->kind; \
                    node->integer.is_unsigned = node->exprtype->is_unsigned; \
                    node->integer.value = left->integer.value op right->integer.value; \
                } \
                break
            #define COMPARE(op) { \
                node->binary.left = try(jitc_process_ast(context, node->binary.left, NULL)); \
                node->binary.right = try(jitc_process_ast(context, node->binary.right, NULL)); \
                jitc_type_t* type = jitc_type_promotion(context, node->binary.left->exprtype, node->binary.right->exprtype, true); \
                node->binary.left = try(jitc_cast(context, node->binary.left, type, false, node->token)); \
                node->binary.right = try(jitc_cast(context, node->binary.right, type, false, node->token)); \
                if (is_constant(node->binary.left) && is_constant(node->binary.right)) { \
                    smartptr(jitc_ast_t) left = node->binary.left; \
                    smartptr(jitc_ast_t) right = node->binary.right; \
                    node->node_type = AST_Integer; \
                    node->integer.type_kind = Type_Int8; \
                    node->integer.is_unsigned = true; \
                    if (type->kind == Type_Float32 || type->kind == Type_Float64) \
                        node->integer.value = left->floating.value op node->binary.right->floating.value; \
                    else { \
                        bool lneg = !left ->integer.is_unsigned && ((left ->integer.value >> 63) & 1); \
                        bool rneg = !right->integer.is_unsigned && ((right->integer.value >> 63) & 1); \
                        if (lneg != rneg) node->integer.value = rneg op lneg; \
                        else node->integer.value = left->integer.value op right->integer.value; \
                    } \
                } \
                node->exprtype = jitc_typecache_primitive(context, Type_Int8); \
                node->exprtype = jitc_typecache_unsigned(context, node->exprtype); \
            } break
            #define LOGIC(op, shortcircuit) { \
                node->binary.left = try(jitc_process_ast(context, node->binary.left, NULL)); \
                node->binary.right = try(jitc_process_ast(context, node->binary.right, NULL)); \
                bool lval = false, lconst = is_constant(node->binary.left); \
                bool rval = false, rconst = is_constant(node->binary.right); \
                if (is_struct(node->binary.left->exprtype) || is_struct(node->binary.right->exprtype)) \
                    throw(node->token, "Performing logic on a non-scalar type"); \
                if (is_decayed_pointer(node->binary.right->exprtype)) lconst = lval = true; \
                else if (lconst) lval = !(node->node_type == AST_Floating \
                    ? !node->binary.left->floating.value \
                    : !node->binary.left->integer.value \
                ); \
                if (is_decayed_pointer(node->binary.right->exprtype)) rconst = rval = true; \
                else if (rconst) rval = !(node->node_type == AST_Floating \
                    ? !node->binary.right->floating.value \
                    : !node->binary.right->integer.value \
                ); \
                if ((lval == shortcircuit && lconst) || (rval == shortcircuit && rconst) || (lconst && rconst)) { \
                    smartptr(jitc_ast_t) left = node->binary.left; \
                    smartptr(jitc_ast_t) right = node->binary.right; \
                    node->node_type = AST_Integer; \
                    node->integer.value = lval op rval; \
                    node->integer.type_kind = Type_Int8; \
                    node->integer.is_unsigned = true; \
                } \
                node->exprtype = jitc_typecache_primitive(context, Type_Int8); \
                node->exprtype = jitc_typecache_unsigned(context, node->exprtype); \
            } break
            #define ASSIGNMENT(allow_const, check, errmsg) \
                node->binary.left = try(jitc_process_ast(context, node->binary.left, &node->exprtype)); \
                node->binary.right = try(jitc_process_ast(context, node->binary.right, NULL)); \
                if (node->exprtype->is_const && !allow_const) throw(node->token, "Assigning to const"); \
                if (!is_lvalue(node->binary.left)) throw(node->token, "Assigning to an rvalue"); \
                if (is_decayed_pointer(node->unary.inner->exprtype)) throw(node->token, "Assigning to an object"); \
                if (!(check)) throw(node->token, errmsg); \
                if (is_pointer(node->binary.left->exprtype) && node->binary.operation != Binary_Assignment) { \
                    if (is_pointer(node->binary.right->exprtype) && node->binary.operation == Binary_AssignSubtraction) { \
                        if (node->binary.left->exprtype->ptr.base->size != node->binary.right->exprtype->ptr.base->size) \
                            throw(node->token, "Pointed-to objects have different sizes"); \
                        node->binary.operation = Binary_AssignPtrDiff; \
                        node->exprtype = jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int64)); \
                    } \
                    else { \
                        if (!is_integer(node->binary.right->exprtype)) \
                            throw(node->token, "Pointer arithmetic with a non-integer type"); \
                        node->binary.operation += Binary_AssignPtrAddition - Binary_AssignAddition; \
                    } \
                } \
                else node->binary.right = try(jitc_cast(context, node->binary.right, node->exprtype, false, node->token)); \
                break
            case Binary_Addition: ARITHMETIC(+, true);
            case Binary_Subtraction: ARITHMETIC(-, true);
            case Binary_Multiplication: ARITHMETIC(*, false);
            case Binary_Division: ARITHMETIC(/, false);
            case Binary_Modulo: BINARY(%);
            case Binary_BitshiftLeft: BINARY(<<);
            case Binary_BitshiftRight: BINARY(>>);
            case Binary_And: BINARY(&);
            case Binary_Or: BINARY(|);
            case Binary_Xor: BINARY(^);
            case Binary_LessThan: COMPARE(<);
            case Binary_LessThanOrEqualTo: COMPARE(<=);
            case Binary_GreaterThan: COMPARE(>);
            case Binary_GreaterThanOrEqualTo: COMPARE(>=);
            case Binary_Equals: COMPARE(==);
            case Binary_NotEquals: COMPARE(!=);
            case Binary_LogicAnd: LOGIC(&&, false);
            case Binary_LogicOr: LOGIC(||, true);
            case Binary_AssignConst: ASSIGNMENT(true, true, "");
            case Binary_Assignment: ASSIGNMENT(false, true, "");
            case Binary_AssignAddition:
            case Binary_AssignSubtraction: ASSIGNMENT(false, is_number(node->exprtype) || node->exprtype->kind == Type_Pointer, "Arithmetic on a non-scalar type");
            case Binary_AssignMultiplication:
            case Binary_AssignDivision: ASSIGNMENT(false, is_number(node->exprtype), "Arithmetic on a non-numeric type");
            case Binary_AssignModulo:
            case Binary_AssignBitshiftLeft:
            case Binary_AssignBitshiftRight:
            case Binary_AssignAnd:
            case Binary_AssignOr:
            case Binary_AssignXor: ASSIGNMENT(false, is_integer(node->exprtype), "Bitwise operation on a non-integer type");
            case Binary_Comma:
                node->binary.left = try(jitc_process_ast(context, node->binary.left, NULL));
                node->binary.right = try(jitc_process_ast(context, node->binary.right, &node->exprtype));
                if (is_constant(node->binary.left)) replace(node) = node->binary.right;
                break;
            default: break;
        } break;
        case AST_Ternary:
        case AST_Branch: {
            node->ternary.when = try(jitc_process_ast(context, node->ternary.when, NULL));
            node->ternary.then = try(jitc_process_ast(context, node->ternary.then, NULL));
            node->ternary.otherwise = node->ternary.otherwise ? try(jitc_process_ast(context, node->ternary.otherwise, NULL)) : NULL;
            if (is_struct(node->ternary.when->exprtype)) throw(node->token, "Condition must be a scalar value");
            if (is_constant(node->ternary.when)) {
                bool cond = is_decayed_pointer(node->ternary.when->exprtype) ? 1 : node->ternary.when->node_type == AST_Floating
                    ? node->ternary.when->floating.value
                    : node->ternary.when->integer.value;
                jitc_ast_t* result = cond ? node->ternary.then : node->ternary.otherwise;
                jitc_destroy_ast(node->ternary.when);
                jitc_destroy_ast(cond ? node->ternary.otherwise : node->ternary.then);
                free(node);
                node = result;
            }
        } break;
        default: break;
    }
    if (exprtype) *exprtype = node->exprtype;
    return node;
}

jitc_ast_t* jitc_flatten_ast(jitc_ast_t* ast, list_t* _list) {
    list(jitc_ast_t*)* list = _list;
    if (!ast) return NULL;
    if (ast->node_type != AST_List && ast->node_type != AST_Scope) {
        if (ast->node_type == AST_Loop) {
            ast->loop.cond = jitc_flatten_ast(ast->loop.cond, NULL);
            ast->loop.body = jitc_flatten_ast(ast->loop.body, NULL);
        }
        else if (ast->node_type == AST_Branch) {
            ast->ternary.when = jitc_flatten_ast(ast->ternary.when, NULL);
            ast->ternary.then = jitc_flatten_ast(ast->ternary.then, NULL);
            ast->ternary.otherwise = jitc_flatten_ast(ast->ternary.otherwise, NULL);
        }
        return ast;
    }
    if (list && list_size(ast->list.inner) == 0) {
        free(ast);
        return NULL;
    }
    if (list_size(ast->list.inner) == 1) {
        jitc_ast_t* inner = list_get(ast->list.inner, 0);
        if (inner->node_type == AST_Scope) {
            jitc_ast_t* flattened = jitc_flatten_ast(inner, NULL);
            flattened->node_type = ast->node_type;
            free(ast);
            return flattened;
        }
    }
    if (ast->node_type == AST_List && list) {
        for (size_t i = 0; i < list_size(ast->list.inner); i++) {
            jitc_ast_t* child = jitc_flatten_ast(list_get(ast->list.inner, i), list);
            if (child) list_add(list) = child;
        }
        free(ast);
        return NULL;
    }
    list(jitc_ast_t*)* new_list = list_new(jitc_ast_t*);
    for (size_t i = 0; i < list_size(ast->list.inner); i++) {
        jitc_ast_t* child = jitc_flatten_ast(list_get(ast->list.inner, i), new_list);
        if (child) list_add(new_list) = child;
    }
    list_delete(ast->list.inner);
    ast->list.inner = (void*)new_list;
    return ast;
}

typedef struct {
    jitc_type_t* type;
    int base_index;
    int end_index;
} init_frame_t;

typedef struct {
    jitc_type_t* type;
    jitc_type_t* aggregate;
    int offset;
} init_element_t;

static init_element_t* jitc_init_append(list_t* _elements, jitc_type_t* type, int base_offset) {
    list(init_element_t)* elements = _elements;
    init_element_t* element = NULL;
    if ((type->kind == Type_Struct || type->kind == Type_Union) && type->str.num_fields != 0) for (int i = 0; i < type->str.num_fields; i++) {
        init_element_t* inner = jitc_init_append(elements, type->str.fields[i], base_offset + type->str.offsets[i]);
        if (i == 0) element = inner;
    }
    else if ((type->kind == Type_Array) && type->arr.size != 0) for (int i = 0; i < (type->arr.size == -1 ? 1 : type->arr.size); i++) {
        init_element_t* inner = jitc_init_append(elements, type->arr.base, base_offset + i * type->arr.base->size);
        if (i == 0) element = inner;
    }
    else {
        element = &list_add(elements);
        element->type = type;
        element->offset = base_offset;
    }
    element->aggregate = type;
    return element;
}

static int jitc_init_num_elements(jitc_type_t* type) {
    int total = 0;
    if (type->kind == Type_Struct || type->kind == Type_Union) for (int i = 0; i < type->str.num_fields; i++)
        total += jitc_init_num_elements(type->str.fields[i]);
    else if (type->kind == Type_Array) total = jitc_init_num_elements(type->arr.base) * type->arr.size;
    else total = 1;
    return total;
}

static int jitc_init_designate_array(jitc_type_t* type, int index) {
    if (index == 0) return 0;
    return jitc_init_num_elements(type->arr.base) * index;
}

static int jitc_init_designate_struct(jitc_type_t* type, const char* name) {
    int offset = 0;
    for (int i = 0; i < type->str.num_fields; i++) {
        jitc_type_t* field = type->str.fields[i];
        if ((field->kind == Type_Struct || field->kind == Type_Union) && !field->name) {
            int off = jitc_init_designate_struct(field, name);
            if (off >= 0) return offset + off;
            offset += ~off;
            continue;
        }
        if (field->name && strcmp(field->name, name) == 0) return offset;
        offset += jitc_init_num_elements(field);
    }
    return ~offset;
}

jitc_ast_t* jitc_parse_initializer(jitc_context_t* context, queue_t* _tokens, jitc_token_t* token, jitc_type_t* type, size_t* array_size, bool constant_only) {
    queue(jitc_token_t)* tokens = _tokens;
    smartptr(stack(init_frame_t)) frames = stack_new(init_frame_t);
    smartptr(list(init_element_t)) elements = list_new(init_element_t);
    if (type) jitc_init_append(elements, type, 0);
    init_frame_t* frame = &stack_push(frames);
    frame->type = type;
    frame->base_index = 0;
    frame->end_index = list_size(elements);
    int cursor = 0;

    smartptr(jitc_ast_t) node = mknode(AST_Initializer, token);
    node->init.type = type;
    node->init.items = list_new(jitc_ast_t*);
    node->init.offsets = list_new(size_t);
    size_t curr_item = 0;
    if (array_size) *array_size = 0;

    while (!jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) {
        bool has_designator = false;
        int designator_offset = 0;
        jitc_type_t* designator_type = type;
        while (true) {
            if (!(token = jitc_token_expect(tokens, TOKEN_DOT)) && !(token = jitc_token_expect(tokens, TOKEN_BRACKET_OPEN))) break;
            has_designator = true;
            if (token->type == TOKEN_DOT) {
                if (designator_type->kind != Type_Struct && designator_type->kind != Type_Union) throw(token, "Initializer is not a struct or union");
                if (!(token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) throw(token, "Expected identifier");
                int offset = jitc_init_designate_struct(designator_type, token->value.string);
                if (offset < 0) throw(token, "Cannot find field '%s'", token->value.string);
                designator_offset += offset;
            }
            else {
                if (designator_type->kind != Type_Array) throw(token, "Initializer is not an array");
                smartptr(jitc_ast_t) index = try(jitc_parse_expression(context, tokens, EXPR_NO_COMMAS, NULL));
                if (!jitc_token_expect(tokens, TOKEN_BRACKET_CLOSE)) throw(token, "Expected ']'");
                if (index->node_type != AST_Integer) throw(index->token, "Expected integer literal");
                if (!index->integer.is_unsigned && (index->integer.value & (1L << 63))) throw(index->token, "Negative index");
                if (index->integer.value >= type->arr.size) throw(index->token, "Array designator exceeds array size");
                designator_offset += jitc_init_designate_array(designator_type, index->integer.value);
            }
        }
        if (has_designator && !jitc_token_expect(tokens, TOKEN_EQUALS)) throw(NEXT_TOKEN, "Expected '='");
        if (has_designator) curr_item = designator_offset;
        if (array_size) *array_size = curr_item / list_size(elements) + 1;
        if ((token = jitc_token_expect(tokens, TOKEN_BRACE_OPEN))) {
            init_element_t* element = curr_item >= list_size(elements)
                ? type->kind == Type_Array && type->arr.size == -1
                    ? &list_get(elements, curr_item % list_size(elements))
                    : NULL
                : &list_get(elements, curr_item);
            smartptr(jitc_ast_t) inner = try(jitc_parse_initializer(context, tokens, token, element ? element->aggregate : NULL, NULL, constant_only));
            if (element) for (size_t i = 0; i < list_size(inner->init.offsets); i++) {
                list_add(node->init.items) = list_get(inner->init.items, 0);
                list_add(node->init.offsets) = list_get(inner->init.offsets, i) + element->offset;
                list_remove(inner->init.items, 0);
            }
            curr_item += element ? jitc_init_num_elements(element->aggregate) : 0;
        }
        else {
            init_element_t* element = curr_item >= list_size(elements)
                ? type->kind == Type_Array && type->arr.size == -1
                    ? &list_get(elements, curr_item % list_size(elements))
                    : NULL
                : &list_get(elements, curr_item);
            smartptr(jitc_ast_t) expr = try(jitc_parse_expression(context, tokens, EXPR_NO_COMMAS, NULL));
            if (element) {
                if (element->type->kind == Type_Array || element->type->kind == Type_Struct || element->type->kind == Type_Union)
                    throw(expr->token, "Aggregate type with 0 elements must have an explicit initializer");
                bool is_aggregate = false;
                if (!(expr = jitc_cast(context, expr, element->type, false, expr->token))) {
                    jitc_error_set(context, NULL);
                    expr = try(jitc_cast(context, expr, element->aggregate, false, expr->token));
                    is_aggregate = true;
                }
                list_add(node->init.items) = move(expr);
                list_add(node->init.offsets) = element->offset + curr_item / list_size(elements) * (type->kind == Type_Array ? type->arr.base->size : 0);
                if (is_aggregate) curr_item += jitc_init_num_elements(element->aggregate);
                else {
                    int offset = element->offset;
                    do curr_item++;
                    while (
                        list_get(elements, curr_item % list_size(elements)).offset == offset &&
                        curr_item % list_size(elements) != 0
                    );
                }
            }
        }
        if (jitc_token_expect(tokens, TOKEN_COMMA)) continue;
        if (jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) break;
        throw(NEXT_TOKEN, "Expected ',' or '}'");
    }
    return move(node);
}

jitc_ast_t* jitc_parse_expression_operand(jitc_context_t* context, queue_t* _tokens) {
    queue(jitc_token_t)* tokens = _tokens;
    jitc_token_t* token;
    bool force_parse_parentheses = false;
    smartptr(stack(jitc_ast_t*)) unary_stack = stack_new(jitc_ast_t*);
    smartptr(jitc_ast_t) node = NULL;
    while (true) {
        smartptr(jitc_ast_t) node = NULL;
        if      ((token = jitc_token_expect(tokens, TOKEN_PLUS))) (node = mknode(AST_Unary, token))->unary.operation = Unary_ArithPlus;
        else if ((token = jitc_token_expect(tokens, TOKEN_MINUS))) (node = mknode(AST_Unary, token))->unary.operation = Unary_ArithNegate;
        else if ((token = jitc_token_expect(tokens, TOKEN_TILDE))) (node = mknode(AST_Unary, token))->unary.operation = Unary_BinaryNegate;
        else if ((token = jitc_token_expect(tokens, TOKEN_EXCLAMATION_MARK))) (node = mknode(AST_Unary, token))->unary.operation = Unary_LogicNegate;
        else if ((token = jitc_token_expect(tokens, TOKEN_ASTERISK))) (node = mknode(AST_Unary, token))->unary.operation = Unary_Dereference;
        else if ((token = jitc_token_expect(tokens, TOKEN_AMPERSAND))) (node = mknode(AST_Unary, token))->unary.operation = Unary_AddressOf;
        else if ((token = jitc_token_expect(tokens, TOKEN_DOUBLE_PLUS))) (node = mknode(AST_Unary, token))->unary.operation = Unary_PrefixIncrement;
        else if ((token = jitc_token_expect(tokens, TOKEN_DOUBLE_MINUS))) (node = mknode(AST_Unary, token))->unary.operation = Unary_PrefixDecrement;
        else if ((token = jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN))) {
            if (!jitc_peek_type(context, tokens)) {
                force_parse_parentheses = true;
                break;
            }
            smartptr(jitc_ast_t) type = mknode(AST_Type, token);
            type->type.type = try(jitc_parse_type(context, tokens, NULL, NULL));
            if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
            node = mknode(AST_Binary, token);
            node->binary.operation = Binary_Cast;
            node->binary.right = move(type);
            // binary.left is filled later, its the same as unary.inner
        }
        else break;
        stack_push(unary_stack) = move(node);
    }
    if (force_parse_parentheses || jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN)) {
        node = try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL));
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_INTEGER))) {
        node = mknode(AST_Integer, token);
        node->integer.is_unsigned = token->flags.int_flags.is_unsigned;
        node->integer.type_kind = token->flags.int_flags.type_kind;
        node->integer.value = token->value.integer;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_FLOAT))) {
        node = mknode(AST_Floating, token);
        node->floating.is_single_precision = token->flags.float_flags.is_single_precision;
        node->floating.value = token->value.floating;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_STRING))) {
        node = mknode(AST_StringLit, token);
        node->string.ptr = token->value.string;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) {
        node = mknode(AST_Variable, token);
        node->variable.name = token->value.string;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_true))) {
        node = mknode(AST_Integer, token);
        node->integer.is_unsigned = true;
        node->integer.type_kind = Type_Int8;
        node->integer.value = true;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_false))) {
        node = mknode(AST_Integer, token);
        node->integer.is_unsigned = true;
        node->integer.type_kind = Type_Int8;
        node->integer.value = false;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_nullptr))) {
        node = mknode(AST_Integer, token);
        node->integer.is_unsigned = true;
        node->integer.type_kind = Type_Int64;
        node->integer.value = 0;
    }
    else if ((token = jitc_token_expect(tokens, TOKEN_sizeof)) || (token = jitc_token_expect(tokens, TOKEN_alignof))) {
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN)) throw(NEXT_TOKEN, "Expected '('");
        jitc_type_t* type = NULL;
        if (jitc_peek_type(context, tokens)) type = try(jitc_parse_type(context, tokens, NULL, NULL));
        else jitc_destroy_ast(try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, &type)));
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
        node = mknode(AST_Integer, token);
        node->integer.is_unsigned = true;
        node->integer.type_kind = Type_Int64;
        node->integer.value = token->type == TOKEN_sizeof ? type->size : type->alignment;
        if (type->kind == Type_Pointer && type->ptr.prev == Type_Array)
            node->integer.value = type->ptr.base->size * type->ptr.arr_size;
    }
    else throw(NEXT_TOKEN, "Expected expression");
    while (true) {
        smartptr(jitc_ast_t) op = NULL;
        if ((token = jitc_token_expect(tokens, TOKEN_DOUBLE_PLUS))) {
            op = mknode(AST_Unary, token);
            op->unary.operation = Unary_SuffixIncrement;
            op->unary.inner = move(node);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_DOUBLE_MINUS))) {
            op = mknode(AST_Unary, token);
            op->unary.operation = Unary_SuffixDecrement;
            op->unary.inner = move(node);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_DOT))) {
            jitc_token_t* dot = token;
            if (!(token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) throw(NEXT_TOKEN, "Expected identifier");
            op = mknode(AST_WalkStruct, dot);
            op->walk_struct.struct_ptr = move(node);
            op->walk_struct.field_name = token->value.string;
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_ARROW))) {
            jitc_token_t* arrow = token;
            if (!(token = jitc_token_expect(tokens, TOKEN_IDENTIFIER))) throw(NEXT_TOKEN, "Expected identifier");
            jitc_ast_t* deref = mknode(AST_Unary, arrow);
            deref->unary.operation = Unary_Dereference;
            deref->unary.inner = move(node);
            op = mknode(AST_WalkStruct, arrow);
            op->walk_struct.struct_ptr = deref;
            op->walk_struct.field_name = token->value.string;
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_BRACKET_OPEN))) {
            smartptr(jitc_ast_t) addition = mknode(AST_Binary, token);
            addition->binary.operation = Binary_Addition;
            addition->binary.left = move(node);
            addition->binary.right = try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL));
            if (!jitc_token_expect(tokens, TOKEN_BRACKET_CLOSE)) throw(NEXT_TOKEN, "Expected ']'");
            op = mknode(AST_Unary, token);
            op->unary.operation = Unary_Dereference;
            op->unary.inner = move(addition);
        }
        else if ((token = jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN))) {
            smartptr(jitc_ast_t) list = mknode(AST_List, token);
            if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) while (true) {
                list_add(list->list.inner) = try(jitc_parse_expression(context, tokens, EXPR_NO_COMMAS, NULL));
                if (jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) break;
                if (jitc_token_expect(tokens, TOKEN_COMMA)) continue;
                throw(NEXT_TOKEN, "Expected ')' or ','");
            }
            op = mknode(AST_Binary, token);
            op->binary.operation = Binary_FunctionCall;
            op->binary.left = move(node);
            op->binary.right = move(list);
        }
        else break;
        node = move(op);
    }
    while (stack_size(unary_stack) > 0) {
        jitc_ast_t* op = stack_pop(unary_stack);
        op->unary.inner = move(node);
        node = op;
    }
    return move(node);
}

static struct {
    bool rtl_assoc;
    int precedence;
    jitc_binary_op_t type;
} op_info[TOKEN_COUNT] = {
    [TOKEN_ASTERISK]                   = { false, 13, Binary_Multiplication },
    [TOKEN_SLASH]                      = { false, 13, Binary_Division },
    [TOKEN_PERCENT]                    = { false, 13, Binary_Modulo },
    [TOKEN_PLUS]                       = { false, 12, Binary_Addition },
    [TOKEN_MINUS]                      = { false, 12, Binary_Subtraction },
    [TOKEN_DOUBLE_LESS_THAN]           = { false, 11, Binary_BitshiftLeft },
    [TOKEN_DOUBLE_GREATER_THAN]        = { false, 11, Binary_BitshiftRight },
    [TOKEN_LESS_THAN]                  = { false, 10, Binary_LessThan },
    [TOKEN_GREATER_THAN]               = { false, 10, Binary_GreaterThan },
    [TOKEN_LESS_THAN_EQUALS]           = { false, 10, Binary_LessThanOrEqualTo },
    [TOKEN_GREATER_THAN_EQUALS]        = { false, 10, Binary_GreaterThanOrEqualTo },
    [TOKEN_DOUBLE_EQUALS]              = { false, 9,  Binary_Equals },
    [TOKEN_NOT_EQUALS]                 = { false, 9,  Binary_NotEquals },
    [TOKEN_AMPERSAND]                  = { false, 8,  Binary_And },
    [TOKEN_HAT]                        = { false, 7,  Binary_Xor },
    [TOKEN_PIPE]                       = { false, 6,  Binary_Or },
    [TOKEN_DOUBLE_AMPERSAND]           = { false, 5,  Binary_LogicAnd },
    [TOKEN_DOUBLE_PIPE]                = { false, 4,  Binary_LogicOr },
    [TOKEN_QUESTION_MARK]              = { false, 3   },
    [TOKEN_EQUALS]                     = { true,  2,  Binary_Assignment },
    [TOKEN_PLUS_EQUALS]                = { true,  2,  Binary_AssignAddition },
    [TOKEN_MINUS_EQUALS]               = { true,  2,  Binary_AssignSubtraction },
    [TOKEN_ASTERISK_EQUALS]            = { true,  2,  Binary_AssignMultiplication },
    [TOKEN_SLASH_EQUALS]               = { true,  2,  Binary_AssignDivision },
    [TOKEN_PERCENT_EQUALS]             = { true,  2,  Binary_AssignModulo },
    [TOKEN_DOUBLE_LESS_THAN_EQUALS]    = { true,  2,  Binary_AssignBitshiftLeft },
    [TOKEN_DOUBLE_GREATER_THAN_EQUALS] = { true,  2,  Binary_AssignBitshiftRight },
    [TOKEN_AMPERSAND_EQUALS]           = { true,  2,  Binary_AssignAnd },
    [TOKEN_PIPE_EQUALS]                = { true,  2,  Binary_AssignOr },
    [TOKEN_HAT_EQUALS]                 = { true,  2,  Binary_AssignXor },
    [TOKEN_COMMA]                      = { false, 1,  Binary_Comma },
};

jitc_ast_t* jitc_parse_expression(jitc_context_t* context, queue_t* _tokens, int min_prec, jitc_type_t** exprtype) {
    queue(jitc_token_t)* tokens = _tokens;
    smartptr(jitc_ast_t) left = try(jitc_parse_expression_operand(context, tokens));
    while (true) {
        jitc_token_t* token = NEXT_TOKEN;
        int precedence = op_info[token->type].precedence;
        if (precedence < min_prec) break;
        queue_pop(tokens);
        if (token->type == TOKEN_QUESTION_MARK) {
            jitc_type_t *then_type, *else_type;
            smartptr(jitc_ast_t) then_expr = try(jitc_parse_expression(context, tokens, precedence, &then_type));
            if (!jitc_token_expect(tokens, TOKEN_COLON)) throw(NEXT_TOKEN, "Expected ':'");
            smartptr(jitc_ast_t) else_expr = try(jitc_parse_expression(context, tokens, precedence, &else_type));
            then_type = jitc_type_promotion(context, then_type, else_type, false);
            then_expr = try(jitc_cast(context, move(then_expr), then_type, false, token));
            else_expr = try(jitc_cast(context, move(else_expr), then_type, false, token));
            jitc_ast_t* ternary = mknode(AST_Ternary, token);
            ternary->ternary.when = move(left);
            ternary->ternary.then = move(then_expr);
            ternary->ternary.otherwise = move(else_expr);
            left = try(jitc_process_ast(context, ternary, NULL));
            left->exprtype = then_type;
            continue;
        }
        jitc_ast_t* right = try(jitc_parse_expression(context, tokens, precedence + !op_info[token->type].rtl_assoc, NULL));
        jitc_ast_t* node = mknode(AST_Binary, token);
        node->binary.operation = op_info[token->type].type;
        node->binary.left = move(left);
        node->binary.right = right;
        left = node;
    }
    return jitc_process_ast(context, move(left), exprtype);
}

typedef enum {
    ParseType_Command     = (1 << 0),
    ParseType_Declaration = (1 << 1),
    ParseType_Expression  = (1 << 2),
    ParseType_Any = ParseType_Command | ParseType_Declaration | ParseType_Expression
} jitc_parse_type_t;

jitc_ast_t* jitc_parse_statement(jitc_context_t* context, queue_t* _tokens, jitc_parse_type_t allowed) {
    queue(jitc_token_t)* tokens = _tokens;
    jitc_token_t* token = NULL;
    if ((token = jitc_token_expect(tokens, TOKEN_if))) {
        if (!(allowed & ParseType_Command)) throw(token, "'if' not allowed here");
        smartptr(jitc_ast_t) node = mknode(AST_Branch, token);
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN)) throw(NEXT_TOKEN, "Expected '('");
        node->ternary.when = try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL));
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
        jitc_push_scope(context);
        node->ternary.then = try(jitc_parse_statement(context, tokens, ParseType_Command | ParseType_Expression));
        jitc_pop_scope(context);
        if (jitc_token_expect(tokens, TOKEN_else)) {
            jitc_push_scope(context);
            node->ternary.otherwise = try(jitc_parse_statement(context, tokens, ParseType_Command | ParseType_Expression));
            jitc_pop_scope(context);
        }
        node = jitc_process_ast(context, move(node), NULL);
        if (!node) return mknode(AST_List, token);
        return move(node);
    }
    if ((token = jitc_token_expect(tokens, TOKEN_while))) {
        if (!(allowed & ParseType_Command)) throw(token, "'while' not allowed here");
        smartptr(jitc_ast_t) node = mknode(AST_Loop, token);
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN)) throw(NEXT_TOKEN, "Expected '('");
        node->loop.cond = try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL));
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
        jitc_push_scope(context);
        node->loop.body = try(jitc_parse_statement(context, tokens, ParseType_Command | ParseType_Expression));
        jitc_pop_scope(context);
        return move(node);
    }
    if (jitc_token_expect(tokens, TOKEN_do)) {
        if (!(allowed & ParseType_Command)) throw(token, "'do' not allowed here");
        smartptr(jitc_ast_t) node = mknode(AST_Scope, token);
        smartptr(jitc_ast_t) loop = mknode(AST_Loop, token);
        smartptr(jitc_ast_t) scope = mknode(AST_Scope, token);
        list_add(scope->list.inner) = try(jitc_parse_statement(context, tokens, ParseType_Command | ParseType_Expression));
        if (!jitc_token_expect(tokens, TOKEN_while)) throw(NEXT_TOKEN, "Expected 'while'");
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN))  throw(NEXT_TOKEN, "Expected '('");
        smartptr(jitc_ast_t) condition = try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL));
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON)) throw(NEXT_TOKEN, "Expected ';'");
        smartptr(jitc_ast_t) ternary = mknode(AST_Branch, token);
        ternary->ternary.when = move(condition);
        ternary->ternary.then = mknode(AST_List, token);
        ternary->ternary.otherwise = mknode(AST_Break, token);
        list_add(scope->list.inner) = try(jitc_process_ast(context, move(ternary), NULL));
        ternary = NULL;
        loop->loop.body = move(scope);
        list_add(node->list.inner) = move(loop);
        return move(node);
    }
    if (jitc_token_expect(tokens, TOKEN_for)) {
        if (!(allowed & ParseType_Command)) throw(token, "'for' not allowed here");
        smartptr(jitc_ast_t) node = mknode(AST_Scope, token);
        smartptr(jitc_ast_t) loop = mknode(AST_Loop, token);
        smartptr(jitc_ast_t) body = mknode(AST_List, token);
        smartptr(jitc_ast_t) init = NULL;
        smartptr(jitc_ast_t) cond = NULL;
        smartptr(jitc_ast_t) expr = NULL;
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_OPEN)) throw(NEXT_TOKEN, "Expected '('");
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON))
            init = try(jitc_parse_statement(context, tokens, ParseType_Expression | ParseType_Declaration));
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON))
            cond = try(jitc_parse_statement(context, tokens, ParseType_Expression));
        if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) {
            expr = try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL));
            if (!jitc_token_expect(tokens, TOKEN_PARENTHESIS_CLOSE)) throw(NEXT_TOKEN, "Expected ')'");
        }
        list_add(body->list.inner) = try(jitc_parse_statement(context, tokens, ParseType_Any));
        if (expr) list_add(body->list.inner) = move(expr);
        if (init) list_add(node->list.inner) = move(init);
        loop->loop.body = move(body);
        loop->loop.cond = move(cond);
        list_add(node->list.inner) = move(loop);
        return move(node);
    }
    if (jitc_token_expect(tokens, TOKEN_switch)) {
        if (!(allowed & ParseType_Command)) throw(token, "'switch' not allowed here");
        // todo
    }
    if ((token = jitc_token_expect(tokens, TOKEN_continue))) {
        if (!(allowed & ParseType_Command)) throw(token, "'continue' not allowed here");
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON)) throw(NEXT_TOKEN, "Expected ';'");
        return mknode(AST_Continue, token);
    }
    if ((token = jitc_token_expect(tokens, TOKEN_break))) {
        if (!(allowed & ParseType_Command)) throw(token, "'break' not allowed here");
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON)) throw(NEXT_TOKEN, "Expected ';'");
        return mknode(AST_Break, token);
    }
    if ((token = jitc_token_expect(tokens, TOKEN_return))) {
        if (!(allowed & ParseType_Command)) throw(token, "'return' not allowed here");
        smartptr(jitc_ast_t) node = mknode(AST_Return, token);
        jitc_variable_t* retvar = jitc_get_variable(context, "return");
        if (retvar->type->kind != Type_Void) {
            node->ret.expr = try(jitc_cast(context,
                try(jitc_parse_expression(context, tokens, EXPR_WITH_COMMAS, NULL)),
                retvar->type, false, token
            ));
        }
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON)) throw(NEXT_TOKEN, "Expected ';'");
        return move(node);
    }
    if ((token = jitc_token_expect(tokens, TOKEN_BRACE_OPEN))) {
        if (!(allowed & ParseType_Command)) throw(token, "Code block not allowed here");
        smartptr(jitc_ast_t) node = mknode(AST_Scope, token);
        jitc_push_scope(context);
        while (!jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) {
            while (NEXT_TOKEN->type == TOKEN_SEMICOLON) queue_pop(tokens);
            list_add(node->list.inner) = try(jitc_parse_statement(context, tokens, ParseType_Any));
        }
        jitc_pop_scope(context);
        return jitc_flatten_ast(move(node), NULL);
    }
    if (jitc_peek_type(context, tokens)) {
        if (!(allowed & ParseType_Declaration)) throw(NEXT_TOKEN, "Declaration not allowed here");
        token = NEXT_TOKEN;
        smartptr(jitc_ast_t) list = mknode(AST_List, token);
        jitc_decltype_t decltype = Decltype_None;
        jitc_preserve_t preserve_policy = Preserve_IfConst;
        jitc_type_t* base_type = try(jitc_parse_base_type(context, tokens, &decltype, list_size(context->scopes) == 1 ? &preserve_policy : NULL));
        while (true) {
            jitc_type_t* type = base_type;
            jitc_ast_t* decl_node;
            smartptr(jitc_ast_t) node = decl_node = mknode(AST_Declaration, token);
            if (!jitc_parse_type_declarations(context, tokens, &type)) return NULL;
            if (type->kind == Type_Function && NEXT_TOKEN->type == TOKEN_SEMICOLON && decltype != Decltype_Typedef) decltype = Decltype_Extern;
            node->decl.type = type;
            node->decl.decltype = decltype;
            if (node->decl.decltype != Decltype_Typedef && type->name && !jitc_validate_type(type, TypePolicy_NoVoid | TypePolicy_NoUndefTags))
                throw(token, "Declaration of incomplete type");
            bool incomplete_array = !jitc_validate_type(type, TypePolicy_NoUnkArrSize);
            if (decltype != Decltype_Typedef) list_add(list->list.inner) = move(node);
            if (!jitc_declare_variable(context, type, decltype, preserve_policy, 0)) throw(token, "Symbol '%s' already declared", type->name);
            jitc_variable_t* var = jitc_get_variable(context, type->name);

            if ((token = jitc_token_expect(tokens, TOKEN_BRACE_OPEN))) {
                if (decltype == Decltype_Extern) throw(token, "Cannot attach code to an extern function");
                if (type->kind != Type_Function) throw(token, "Cannot attach code to a non-function");
                if (list_size(context->scopes) > 1) throw(token, "Function definition illegal here");
                smartptr(jitc_ast_t) func = mknode(AST_Function, token);
                smartptr(jitc_ast_t) body = mknode(AST_List, token);
                func->func.variable = type;
                jitc_push_scope(context);
                jitc_declare_variable(context, jitc_typecache_named(context, type->func.ret, "return"), Decltype_None, Preserve_IfConst, 0);
                for (size_t i = 0; i < type->func.num_params; i++) {
                    jitc_declare_variable(context, type->func.params[i], Decltype_None, Preserve_IfConst, 0);
                }
                while (!jitc_token_expect(tokens, TOKEN_BRACE_CLOSE)) {
                    list_add(body->list.inner) = try(jitc_parse_statement(context, tokens, ParseType_Any));
                }
                jitc_pop_scope(context);
                func->func.body = jitc_flatten_ast(move(body), NULL);
                list_add(list->list.inner) = move(func);
                break;
            }
            else if (type->kind == Type_Function && var) var->decltype = Decltype_Extern;
            if ((token = jitc_token_expect(tokens, TOKEN_EQUALS))) {
                if (decltype == Decltype_Extern) throw(token, "Cannot assign to an extern variable");
                if (type->kind == Type_Function) throw(token, "Assigning to a function");
                if (!type->name) throw(token, "Assigning to unnamed declaration");
                if ((token = jitc_token_expect(tokens, TOKEN_BRACE_OPEN))) {
                    size_t arr_size;
                    smartptr(jitc_ast_t) initializer = try(jitc_parse_initializer(context, tokens, token, type, &arr_size, list_size(context->scopes) == 1));
                    if (incomplete_array) type = jitc_typecache_named(context, jitc_typecache_array(context, type->arr.base, arr_size), type->name);
                    jitc_ast_t* variable = mknode(AST_Variable, token);
                    variable->variable.name = type->name;
                    variable->variable.write_dest = true;
                    initializer->init.store_to = variable;
                    initializer->init.type = type;
                    list_add(list->list.inner) = try(jitc_process_ast(context, move(initializer), NULL));
                    if (incomplete_array) {
                        decl_node->decl.type = type;
                        if (var) var->type = decl_node->decl.type;
                    }
                }
                else if (incomplete_array) throw(NEXT_TOKEN, "Expected '{'");
                else {
                    if (type->kind == Type_Array) throw(token, "Assigning to an array");
                    smartptr(jitc_ast_t) assign = mknode(AST_Binary, token);
                    smartptr(jitc_ast_t) variable = mknode(AST_Variable, token);
                    variable->variable.name = type->name;
                    assign->binary.operation = Binary_AssignConst;
                    assign->binary.right = try(jitc_parse_expression(context, tokens, EXPR_NO_COMMAS, NULL));
                    assign->binary.left = move(variable);
                    if (list_size(context->scopes) == 1 && !is_constant(assign->binary.right))
                        throw(token, "Assigning a non-literal to a global variable");
                    list_add(list->list.inner) = try(jitc_process_ast(context, move(assign), NULL));
                }
            }
            else if (incomplete_array) throw(NEXT_TOKEN, "Expected '='");
            if (jitc_token_expect(tokens, TOKEN_COMMA)) continue;
            if (jitc_token_expect(tokens, TOKEN_SEMICOLON)) break;
            throw(NEXT_TOKEN, "Expected ',' or ';'");
        }
        return move(list);
    }
    if ((token = jitc_token_expect(tokens, TOKEN_SEMICOLON))) return mknode(AST_List, token);
    if (allowed & ParseType_Expression) {
        smartptr(jitc_ast_t) node = try(jitc_parse_expression(context, tokens, true, NULL));
        if (!jitc_token_expect(tokens, TOKEN_SEMICOLON)) throw(NEXT_TOKEN, "Expected ';'");
        return move(node);
    }
    throw(NEXT_TOKEN, "Invalid statement");
}

jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* _tokens) {
    queue(jitc_token_t)* tokens = _tokens;
    smartptr(jitc_ast_t) ast = mknode(AST_List, NEXT_TOKEN);
    while (!jitc_token_expect(tokens, TOKEN_END_OF_FILE)) {
        while (NEXT_TOKEN->type == TOKEN_SEMICOLON) queue_pop(tokens);
        list_add(ast->list.inner) = try(jitc_parse_statement(context, tokens, ParseType_Declaration));
    }
    return jitc_flatten_ast(move(ast), NULL);
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
        case AST_Branch:
            jitc_destroy_ast(ast->ternary.when);
            jitc_destroy_ast(ast->ternary.then);
            jitc_destroy_ast(ast->ternary.otherwise);
            break;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                jitc_destroy_ast(list_get(ast->list.inner, i));
            }
            list_delete(ast->list.inner);
            break;
        case AST_Function:
            jitc_destroy_ast(ast->func.body);
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
        case AST_Initializer:
            jitc_destroy_ast(ast->init.store_to);
            for (size_t i = 0; i < list_size(ast->init.items); i++) {
                jitc_destroy_ast(list_get(ast->init.items, i));
            }
            list_delete(ast->init.items);
            list_delete(ast->init.offsets);
            break;
        default: break;
    }
    free(ast);
}
