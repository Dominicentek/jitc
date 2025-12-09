#include "arch.h"
#include "compares.h"
#include "dynamics.h"
#include "jitc_internal.h"

#include <stdlib.h>

#define DEBUG

#if defined(DEBUG)
#include "platform/debug.c"
#elif defined(_WIN32) && defined(__x86_64__)
#include "platform/win-x86_64.c"
#elif defined(__x86_64__)
#include "platform/sysv-x86_64.c"
#elif defined(__aarch64__)
#include "platform/sysv-aarch64.c"
#endif

#define type(x) x->kind, x->is_unsigned

typedef struct {
    bool is_leaf;
    union {
        list_t* list;
        struct {
            jitc_type_t* type;
            size_t offset;
        } var;
    };
} stackvar_t;

static void promote(bytewriter_t* writer, jitc_ast_t* ast) {
    if (ast->exprtype->kind < Type_Int32)
        jitc_asm_cvt(writer, Type_Int32, ast->exprtype->is_unsigned);
}

static size_t get_su_number(jitc_ast_t* ast) {
    if (ast->su_number != 0) return ast->su_number;
    switch (ast->node_type) {
        case AST_Unary: {
            ast->su_number = get_su_number(ast);
        } break;
        case AST_Binary: {
            size_t l = get_su_number(ast->binary.left);
            size_t r = get_su_number(ast->binary.right);
            if (l == r) ast->su_number = l + 1;
            else ast->su_number = l > r ? l : r;
        } break;
        case AST_Ternary: {
            size_t l = get_su_number(ast->ternary.when);
            size_t t = get_su_number(ast->ternary.then);
            size_t o = get_su_number(ast->ternary.otherwise);
            size_t r = t > o ? t : o;
            if (l == r) ast->su_number = l + 1;
            else ast->su_number = l > r ? l : r;
        } break;
        default: ast->su_number = 1;
    }
    return ast->su_number;
}

static void append_to_size_tree(list_t* list, jitc_ast_t* node) {
    if (!node) return;
    switch (node->node_type) {
        case AST_Declaration: {
            stackvar_t* size = malloc(sizeof(stackvar_t));
            size->is_leaf = true;
            size->var.type = node->decl.type;
            list_add_ptr(list, size);
        } break;
        case AST_List:
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_size_tree(list, list_get_ptr(node->list.inner, i));
            }
            break;
        case AST_Scope: {
            stackvar_t* size = malloc(sizeof(stackvar_t));
            size->is_leaf = false;
            size->list = list_new();
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_size_tree(size->list, list_get_ptr(node->list.inner, i));
            }
            list_add_ptr(list, size);
        } break;
        case AST_Ternary: {
            stackvar_t* size = malloc(sizeof(stackvar_t));
            size->is_leaf = false;
            size->list = list_new();
            append_to_size_tree(size->list, node->ternary.when);
            append_to_size_tree(size->list, node->ternary.then);
            append_to_size_tree(size->list, node->ternary.otherwise);
            list_add_ptr(list, size);
        } break;
        case AST_Loop: {
            stackvar_t* size = malloc(sizeof(stackvar_t));
            size->is_leaf = false;
            size->list = list_new();
            append_to_size_tree(size->list, node->loop.cond);
            append_to_size_tree(size->list, node->loop.body);
            list_add_ptr(list, size);
        } break;
        default: break;
    }
}

static size_t process_size_tree(map_t* variable_map, stackvar_t* tree, size_t parent_size) {
    size_t size = parent_size;
    for (size_t i = 0; i < list_size(tree->list); i++) {
        stackvar_t* node = list_get_ptr(tree->list, i);
        if (!node->is_leaf) continue;
        if (!node->var.type->name) continue;
        if (size % node->var.type->alignment != 0) size += node->var.type->alignment - size % node->var.type->alignment;
        node->var.offset = size;
        size += node->var.type->size;
        map_get_ptr(variable_map, (char*)node->var.type->name);
        map_store_ptr(variable_map, node);
    }
    size_t max_size = size;
    for (size_t i = 0; i < list_size(tree->list); i++) {
        stackvar_t* node = list_get_ptr(tree->list, i);
        if (node->is_leaf) {
            if (!node->var.type->name) free(node);
            continue;
        }
        size_t node_size = process_size_tree(variable_map, node, size);
        if (max_size < node_size) max_size = node_size;
    }
    list_delete(tree->list);
    free(tree);
    return max_size;
}

static size_t get_stack_size(map_t* variable_map, jitc_ast_t* ast) {
    stackvar_t* size = malloc(sizeof(stackvar_t));
    size->is_leaf = false;
    size->list = list_new();
    append_to_size_tree(size->list, ast);
    return process_size_tree(variable_map, size, 0);
}

static bool assemble(bytewriter_t* writer, jitc_ast_t* ast, map_t* variable_map) {
    if (!ast) return false;
    switch (ast->node_type) {
        case AST_Unary: switch(ast->unary.operation) {
            case Unary_ArithPlus: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); break;
            case Unary_ArithNegate: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_neg(writer); break;
            case Unary_LogicNegate: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_zero(writer); break;
            case Unary_BinaryNegate: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_not(writer); break;
            case Unary_Dereference: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_load(writer, ast->exprtype->kind, ast->exprtype->is_unsigned); break;
            case Unary_AddressOf: assemble(writer, ast->unary.inner, variable_map); jitc_asm_addrof(writer); break;
            case Unary_PrefixIncrement: assemble(writer, ast->unary.inner, variable_map); jitc_asm_inc(writer); break;
            case Unary_PrefixDecrement: assemble(writer, ast->unary.inner, variable_map); jitc_asm_dec(writer); break;
            case Unary_SuffixIncrement:
            case Unary_SuffixDecrement:
                assemble(writer, ast->unary.inner, variable_map);
                jitc_asm_dup(writer);
                (ast->unary.operation == Unary_SuffixIncrement ? jitc_asm_inc : jitc_asm_dec)(writer);
                jitc_asm_pop(writer);
                break;
        }
        case AST_Binary:
            if (ast->binary.operation == Binary_Cast) {
                assemble(writer, ast->binary.left, variable_map);
                jitc_asm_cvt(writer, type(ast->binary.right->type.type));
            }
            else if (ast->binary.operation == Binary_CompoundExpr) {}
            else if (ast->binary.operation == Binary_FunctionCall) {}
            else {
                if (get_su_number(ast->binary.left) < get_su_number(ast->binary.right)) {
                    assemble(writer, ast->binary.right, variable_map);
                    assemble(writer, ast->binary.left, variable_map);
                    jitc_asm_swp(writer);
                }
                else {
                    assemble(writer, ast->binary.left, variable_map);
                    assemble(writer, ast->binary.right, variable_map);
                }
                switch (ast->binary.operation) {
                    case Binary_Addition:             jitc_asm_add(writer); break;
                    case Binary_Subtraction:          jitc_asm_sub(writer); break;
                    case Binary_Multiplication:       jitc_asm_mul(writer); break;
                    case Binary_Division:             jitc_asm_div(writer); break;
                    case Binary_Modulo:               jitc_asm_mod(writer); break;
                    case Binary_BitshiftLeft:         jitc_asm_shl(writer); break;
                    case Binary_BitshiftRight:        jitc_asm_shr(writer); break;
                    case Binary_And:                  jitc_asm_and(writer); break;
                    case Binary_Or:                   jitc_asm_or (writer); break;
                    case Binary_Xor:                  jitc_asm_xor(writer); break;
                    case Binary_Equals:               jitc_asm_eql(writer); break;
                    case Binary_NotEquals:            jitc_asm_neq(writer); break;
                    case Binary_LessThan:             jitc_asm_lst(writer); break;
                    case Binary_LessThanOrEqualTo:    jitc_asm_lte(writer); break;
                    case Binary_GreaterThan:          jitc_asm_grt(writer); break;
                    case Binary_GreaterThanOrEqualTo: jitc_asm_gte(writer); break;
                    case Binary_Assignment:           jitc_asm_store(writer); break;
                    case Binary_AssignAddition:       jitc_asm_add(writer); jitc_asm_store(writer); break;
                    case Binary_AssignSubtraction:    jitc_asm_sub(writer); jitc_asm_store(writer); break;
                    case Binary_AssignMultiplication: jitc_asm_mul(writer); jitc_asm_store(writer); break;
                    case Binary_AssignDivision:       jitc_asm_div(writer); jitc_asm_store(writer); break;
                    case Binary_AssignModulo:         jitc_asm_mod(writer); jitc_asm_store(writer); break;
                    case Binary_AssignBitshiftLeft:   jitc_asm_shl(writer); jitc_asm_store(writer); break;
                    case Binary_AssignBitshiftRight:  jitc_asm_shr(writer); jitc_asm_store(writer); break;
                    case Binary_AssignAnd:            jitc_asm_and(writer); jitc_asm_store(writer); break;
                    case Binary_AssignOr:             jitc_asm_or (writer); jitc_asm_store(writer); break;
                    case Binary_AssignXor:            jitc_asm_xor(writer); jitc_asm_store(writer); break;
                    case Binary_LogicAnd: break; // todo
                    case Binary_LogicOr:  break; // todo
                    default: break;
                }
            }
            break;
        case AST_Ternary:
            jitc_asm_if(writer);
            if (!assemble(writer, ast->ternary.when, variable_map)) jitc_asm_pushi(writer, 0, Type_Int32, false);
            jitc_asm_then(writer);
            assemble(writer, ast->ternary.then, variable_map);
            jitc_asm_else(writer);
            assemble(writer, ast->ternary.otherwise, variable_map);
            jitc_asm_end(writer);
            break;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                assemble(writer, list_get_ptr(ast->list.inner, i), variable_map);
            }
            break;
        case AST_Loop:
            jitc_asm_if(writer);
            if (!assemble(writer, ast->loop.cond, variable_map)) jitc_asm_pushi(writer, 1, Type_Int32, false);
            jitc_asm_then(writer);
            assemble(writer, ast->loop.body, variable_map);
            jitc_asm_goto_start(writer);
            jitc_asm_end(writer);
            break;
        case AST_Break:
            jitc_asm_goto_end(writer);
            break;
        case AST_Continue:
            jitc_asm_goto_start(writer);
            break;
        case AST_Return:
            if (ast->ret.expr) assemble(writer, ast->ret.expr, variable_map);
            else jitc_asm_pushi(writer, 0, Type_Int64, true);
            jitc_asm_ret(writer);
            break;
        case AST_Integer:
        case AST_StringLit:
            jitc_asm_pushi(writer, ast->integer.value, ast->integer.type_kind, ast->integer.is_unsigned);
            break;
        case AST_Floating:
            if (ast->floating.is_single_precision) jitc_asm_pushf(writer, ast->floating.value);
            else jitc_asm_pushd(writer, ast->floating.value);
            break;
        case AST_Variable:
            map_find_ptr(variable_map, (char*)ast->variable.name);
            stackvar_t* var = map_as_ptr(variable_map);
            jitc_asm_lstack(writer, var->var.offset, type(var->var.type));
            break;
        case AST_WalkStruct:
            break;
        default: break;
    }
    return true;
}

void* jitc_compile(jitc_context_t* context, jitc_ast_t* ast) {
    bytewriter_t* writer = bytewriter_new();
    map_t* variable_map = map_new(compare_int64);
    bool is_return = false;
    jitc_asm_func(writer, ast->func.variable, get_stack_size(variable_map, ast->func.body));
    for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
        jitc_ast_t* node = list_get_ptr(ast->func.body->list.inner, i);
        assemble(writer, node, variable_map);
        is_return = node->node_type == AST_Return;
    }
    if (!is_return) {
        jitc_asm_pushi(writer, 0, Type_Int32, false);
        jitc_asm_ret(writer);
    }
    jitc_asm_end(writer);
    return bytewriter_delete(writer);
}
