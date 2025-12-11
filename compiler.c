#include "compares.h"
#include "dynamics.h"
#include "jitc_internal.h"

#include <stdlib.h>
#include <stdarg.h>

typedef enum {
    ArgType_Int,
    ArgType_Float,
    ArgType_Pointer
} jitc_ir_argtype_t;

#define INT(...) ArgType_Int
#define FLT(...) ArgType_Float
#define DBL(...) ArgType_Double
#define PTR(...) ArgType_Pointer
#define IRFUNC(x, ...) [IROpCode_##x] = { sizeof((jitc_ir_argtype_t[]){__VA_ARGS__}) / sizeof(jitc_ir_argtype_t), (jitc_ir_argtype_t[]){__VA_ARGS__} },
static struct {
    int num_args;
    jitc_ir_argtype_t* arg_type;
} args[] = {
    OPCODES(IRFUNC)
};

static void jitc_asm(list_t* list, jitc_opcode_t opcode, ...) {
    va_list varargs;
    va_start(varargs, opcode);
    jitc_ir_t* ir = malloc(sizeof(jitc_ir_t));
    ir->opcode = opcode;
    for (int i = 0; i < args[opcode].num_args; i++) {
        switch (args[opcode].arg_type[i]) {
            case ArgType_Int: ir->params[i].as_integer = va_arg(varargs, uint64_t); break;
            case ArgType_Float: ir->params[i].as_float = va_arg(varargs, double); break;
            case ArgType_Pointer: ir->params[i].as_pointer = va_arg(varargs, void*); break;
        }
    }
    va_end(varargs);
    list_add_ptr(list, ir);
}

//#define DEBUG

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

static void promote(list_t* list, jitc_ast_t* ast) {
    if (ast->exprtype->kind < Type_Int32)
        jitc_asm(list, IROpCode_cvt, Type_Int32, ast->exprtype->is_unsigned);
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

static bool assemble(list_t* list, jitc_ast_t* ast, map_t* variable_map) {
    if (!ast) return false;
    switch (ast->node_type) {
        case AST_Unary: switch(ast->unary.operation) {
            case Unary_ArithPlus: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); break;
            case Unary_ArithNegate: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_neg); break;
            case Unary_LogicNegate: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_zero); break;
            case Unary_BinaryNegate: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_not); break;
            case Unary_Dereference: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_load, ast->exprtype->kind, ast->exprtype->is_unsigned); break;
            case Unary_AddressOf: assemble(list, ast->unary.inner, variable_map); jitc_asm(list, IROpCode_addrof); break;
            case Unary_PrefixIncrement: assemble(list, ast->unary.inner, variable_map); jitc_asm(list, IROpCode_inc); break;
            case Unary_PrefixDecrement: assemble(list, ast->unary.inner, variable_map); jitc_asm(list, IROpCode_dec); break;
            case Unary_SuffixIncrement:
            case Unary_SuffixDecrement:
                assemble(list, ast->unary.inner, variable_map);
                jitc_asm(list, ast->unary.operation == Unary_SuffixIncrement ? IROpCode_inc : IROpCode_dec);
                break;
        }
        case AST_Binary:
            if (ast->binary.operation == Binary_Cast) {
                assemble(list, ast->binary.left, variable_map);
                jitc_asm(list, IROpCode_cvt, type(ast->binary.right->type.type));
            }
            else if (ast->binary.operation == Binary_CompoundExpr) {}
            else if (ast->binary.operation == Binary_FunctionCall) {}
            else {
                if (get_su_number(ast->binary.left) < get_su_number(ast->binary.right)) {
                    assemble(list, ast->binary.right, variable_map);
                    assemble(list, ast->binary.left, variable_map);
                    jitc_asm(list, IROpCode_swp);
                }
                else {
                    assemble(list, ast->binary.left, variable_map);
                    assemble(list, ast->binary.right, variable_map);
                }
                switch (ast->binary.operation) {
                    case Binary_Addition:             jitc_asm(list, IROpCode_add); break;
                    case Binary_Subtraction:          jitc_asm(list, IROpCode_sub); break;
                    case Binary_Multiplication:       jitc_asm(list, IROpCode_mul); break;
                    case Binary_Division:             jitc_asm(list, IROpCode_div); break;
                    case Binary_Modulo:               jitc_asm(list, IROpCode_mod); break;
                    case Binary_BitshiftLeft:         jitc_asm(list, IROpCode_shl); break;
                    case Binary_BitshiftRight:        jitc_asm(list, IROpCode_shr); break;
                    case Binary_And:                  jitc_asm(list, IROpCode_and); break;
                    case Binary_Or:                   jitc_asm(list, IROpCode_or);  break;
                    case Binary_Xor:                  jitc_asm(list, IROpCode_xor); break;
                    case Binary_Equals:               jitc_asm(list, IROpCode_eql); break;
                    case Binary_NotEquals:            jitc_asm(list, IROpCode_neq); break;
                    case Binary_LessThan:             jitc_asm(list, IROpCode_lst); break;
                    case Binary_LessThanOrEqualTo:    jitc_asm(list, IROpCode_lte); break;
                    case Binary_GreaterThan:          jitc_asm(list, IROpCode_grt); break;
                    case Binary_GreaterThanOrEqualTo: jitc_asm(list, IROpCode_gte); break;
                    case Binary_Assignment:           jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignAddition:       jitc_asm(list, IROpCode_add); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignSubtraction:    jitc_asm(list, IROpCode_sub); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignMultiplication: jitc_asm(list, IROpCode_mul); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignDivision:       jitc_asm(list, IROpCode_div); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignModulo:         jitc_asm(list, IROpCode_mod); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignBitshiftLeft:   jitc_asm(list, IROpCode_shl); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignBitshiftRight:  jitc_asm(list, IROpCode_shr); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignAnd:            jitc_asm(list, IROpCode_and); jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignOr:             jitc_asm(list, IROpCode_or);  jitc_asm(list, IROpCode_store); break;
                    case Binary_AssignXor:            jitc_asm(list, IROpCode_xor); jitc_asm(list, IROpCode_store); break;
                    case Binary_LogicAnd: break; // todo
                    case Binary_LogicOr:  break; // todo
                    default: break;
                }
            }
            break;
        case AST_Ternary:
            jitc_asm(list, IROpCode_if);
            if (!assemble(list, ast->ternary.when, variable_map)) jitc_asm(list, IROpCode_pushi, 0, Type_Int32, false);
            jitc_asm(list, IROpCode_then);
            assemble(list, ast->ternary.then, variable_map);
            jitc_asm(list, IROpCode_else);
            assemble(list, ast->ternary.otherwise, variable_map);
            jitc_asm(list, IROpCode_end);
            break;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                assemble(list, list_get_ptr(ast->list.inner, i), variable_map);
            }
            break;
        case AST_Loop:
            jitc_asm(list, IROpCode_if);
            if (!assemble(list, ast->loop.cond, variable_map)) jitc_asm(list, IROpCode_pushi, 1, Type_Int32, false);
            jitc_asm(list, IROpCode_then);
            assemble(list, ast->loop.body, variable_map);
            jitc_asm(list, IROpCode_goto_start);
            jitc_asm(list, IROpCode_end);
            break;
        case AST_Break:
            jitc_asm(list, IROpCode_goto_end);
            break;
        case AST_Continue:
            jitc_asm(list, IROpCode_goto_start);
            break;
        case AST_Return:
            if (ast->ret.expr) assemble(list, ast->ret.expr, variable_map);
            else jitc_asm(list, IROpCode_pushi, 0, Type_Int64, true);
            jitc_asm(list, IROpCode_ret);
            break;
        case AST_Integer:
        case AST_StringLit:
            jitc_asm(list, IROpCode_pushi, ast->integer.value, ast->integer.type_kind, ast->integer.is_unsigned);
            break;
        case AST_Floating:
            if (ast->floating.is_single_precision) jitc_asm(list, IROpCode_pushf, ast->floating.value);
            else jitc_asm(list, IROpCode_pushd, ast->floating.value);
            break;
        case AST_Variable:
            map_find_ptr(variable_map, (char*)ast->variable.name);
            stackvar_t* var = map_as_ptr(variable_map);
            jitc_asm(list, IROpCode_lstack, var->var.offset, type(var->var.type));
            break;
        case AST_WalkStruct:
            break;
        default: break;
    }
    return true;
}

void* jitc_compile(jitc_context_t* context, jitc_ast_t* ast) {
    list_t* list = list_new();
    map_t* variable_map = map_new(compare_int64);
    bool is_return = false;
    jitc_asm(list, IROpCode_func, ast->func.variable, get_stack_size(variable_map, ast->func.body));
    for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
        jitc_ast_t* node = list_get_ptr(ast->func.body->list.inner, i);
        assemble(list, node, variable_map);
        is_return = node->node_type == AST_Return;
    }
    if (!is_return) {
        jitc_asm(list, IROpCode_pushi, 0, Type_Int32, false);
        jitc_asm(list, IROpCode_ret);
    }
    jitc_asm(list, IROpCode_end);
    return jitc_assemble(list);
}
