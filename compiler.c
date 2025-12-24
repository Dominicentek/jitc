#include "compares.h"
#include "dynamics.h"
#include "jitc.h"
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
    bool is_global;
    union {
        list_t* list;
        struct {
            jitc_type_t* type;
            union {
                void* ptr;
                size_t offset;
            };
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
            ast->su_number = get_su_number(ast->unary.inner);
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
            size->is_global = false;
            size->var.type = node->decl.type;
            if (node->decl.decltype == Decltype_Extern) {
                size->is_global = true;
                size->var.ptr = node->decl.symbol_ptr;
            }
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
            size->is_global = false;
            size->list = list_new();
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_size_tree(size->list, list_get_ptr(node->list.inner, i));
            }
            list_add_ptr(list, size);
        } break;
        case AST_Ternary: {
            stackvar_t* size = malloc(sizeof(stackvar_t));
            size->is_leaf = false;
            size->is_global = false;
            size->list = list_new();
            append_to_size_tree(size->list, node->ternary.when);
            append_to_size_tree(size->list, node->ternary.then);
            append_to_size_tree(size->list, node->ternary.otherwise);
            list_add_ptr(list, size);
        } break;
        case AST_Loop: {
            stackvar_t* size = malloc(sizeof(stackvar_t));
            size->is_leaf = false;
            size->is_global = false;
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
        if (!node->is_global) {
            node->var.offset = size + node->var.type->size;
            size += node->var.type->size;
        }
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
    size_t step = 1;
    switch (ast->node_type) {
        case AST_Unary: switch(ast->unary.operation) {
            case Unary_ArithPlus: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); break;
            case Unary_ArithNegate: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_neg); break;
            case Unary_LogicNegate: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_zero); break;
            case Unary_BinaryNegate: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_not); break;
            case Unary_Dereference: assemble(list, ast->unary.inner, variable_map); promote(list, ast->unary.inner); jitc_asm(list, IROpCode_load, ast->exprtype->kind, ast->exprtype->is_unsigned); break;
            case Unary_AddressOf: assemble(list, ast->unary.inner, variable_map); jitc_asm(list, IROpCode_addrof); break;
            case Unary_PtrPrefixIncrement: case Unary_PtrPrefixDecrement: case Unary_PtrSuffixIncrement: case Unary_PtrSuffixDecrement:
                step = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
            case Unary_PrefixIncrement: case Unary_PrefixDecrement: case Unary_SuffixIncrement: case Unary_SuffixDecrement:
                assemble(list, ast->unary.inner, variable_map);
                jitc_asm(list, IROpCode_inc, ast->unary.operation & 0b10, step * (ast->unary.operation & 0b01 ? -1 : 1));
                break;
        } return true;
        case AST_Binary:
            if (ast->binary.operation == Binary_Cast) {
                assemble(list, ast->binary.left, variable_map);
                jitc_asm(list, IROpCode_cvt, type(ast->binary.right->type.type));
            }
            else if (ast->binary.operation == Binary_CompoundExpr) {}
            else if (ast->binary.operation == Binary_FunctionCall) {
                jitc_type_t* signature = ast->binary.left->exprtype;
                if (signature->kind == Type_Pointer) signature = signature->ptr.base;
                for (size_t i = signature->func.num_params - 1; i < signature->func.num_params; i--) {
                    assemble(list, list_get_ptr(ast->binary.right->list.inner, i), variable_map);
                }
                assemble(list, ast->binary.left, variable_map);
                jitc_asm(list, IROpCode_call, signature);
            }
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
                    case Binary_Assignment: {
                        if (ast->binary.left->exprtype->kind == Type_Struct || ast->binary.left->exprtype->kind == Type_Union) {
                            jitc_type_t* type = ast->binary.left->exprtype;
                            jitc_asm(list, IROpCode_copy, type->size, type->alignment);
                        }
                        else jitc_asm(list, IROpCode_store);
                    } break;
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
                    case Binary_AssignAddition:       jitc_asm(list, IROpCode_sadd); break;
                    case Binary_AssignSubtraction:    jitc_asm(list, IROpCode_ssub); break;
                    case Binary_AssignMultiplication: jitc_asm(list, IROpCode_smul); break;
                    case Binary_AssignDivision:       jitc_asm(list, IROpCode_sdiv); break;
                    case Binary_AssignModulo:         jitc_asm(list, IROpCode_smod); break;
                    case Binary_AssignBitshiftLeft:   jitc_asm(list, IROpCode_sshl); break;
                    case Binary_AssignBitshiftRight:  jitc_asm(list, IROpCode_sshr); break;
                    case Binary_AssignAnd:            jitc_asm(list, IROpCode_sand); break;
                    case Binary_AssignOr:             jitc_asm(list, IROpCode_sor);  break;
                    case Binary_AssignXor:            jitc_asm(list, IROpCode_sxor); break;
                    case Binary_LogicAnd: break; // todo
                    case Binary_LogicOr:  break; // todo
                    case Binary_PtrAddition:
                    case Binary_PtrSubtraction:
                    case Binary_AssignPtrAddition:
                    case Binary_AssignPtrSubtraction: {
                        size_t size = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
                        if (size != 1) {
                            jitc_asm(list, IROpCode_pushi, size, ast->binary.right->exprtype->kind, ast->binary.right->exprtype->is_unsigned);
                            jitc_asm(list, IROpCode_mul);
                        }
                        jitc_asm(list, (jitc_opcode_t[]){
                            [Binary_PtrAddition] = IROpCode_add,
                            [Binary_PtrSubtraction] = IROpCode_sub,
                            [Binary_AssignPtrAddition] = IROpCode_sadd,
                            [Binary_AssignPtrSubtraction] = IROpCode_ssub,
                        }[ast->binary.operation]);
                    }
                    default: break;
                }
            }
            return true;
        case AST_Ternary:
            jitc_asm(list, IROpCode_if);
            if (ast->ternary.when) assemble(list, ast->ternary.when, variable_map);
            else jitc_asm(list, IROpCode_pushi, 0, Type_Int32, false);
            jitc_asm(list, IROpCode_then);
            if (assemble(list, ast->ternary.then, variable_map)) jitc_asm(list, IROpCode_pop);
            jitc_asm(list, IROpCode_else);
            if (assemble(list, ast->ternary.otherwise, variable_map)) jitc_asm(list, IROpCode_pop);
            jitc_asm(list, IROpCode_end);
            return false;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                if (assemble(list, list_get_ptr(ast->list.inner, i), variable_map)) jitc_asm(list, IROpCode_pop);
            }
            return false;
        case AST_Loop:
            jitc_asm(list, IROpCode_if);
            if (ast->loop.cond) assemble(list, ast->loop.cond, variable_map);
            else jitc_asm(list, IROpCode_pushi, 1, Type_Int32, false);
            jitc_asm(list, IROpCode_then);
            assemble(list, ast->loop.body, variable_map);
            jitc_asm(list, IROpCode_goto_start);
            jitc_asm(list, IROpCode_else);
            jitc_asm(list, IROpCode_end);
            return false;
        case AST_Break:
            jitc_asm(list, IROpCode_goto_end);
            return false;
        case AST_Continue:
            jitc_asm(list, IROpCode_goto_start);
            return false;
        case AST_Return:
            if (ast->ret.expr) assemble(list, ast->ret.expr, variable_map);
            else jitc_asm(list, IROpCode_pushi, 0, Type_Int64, true);
            jitc_asm(list, IROpCode_ret);
            return false;
        case AST_Integer:
            jitc_asm(list, IROpCode_pushi, ast->integer.value, ast->exprtype->kind, ast->exprtype->is_unsigned);
            return true;
        case AST_StringLit:
            jitc_asm(list, IROpCode_pushi, ast->string.ptr, ast->exprtype->kind, ast->exprtype->is_unsigned);
            return true;
        case AST_Floating:
            if (ast->floating.is_single_precision) jitc_asm(list, IROpCode_pushf, ast->floating.value);
            else jitc_asm(list, IROpCode_pushd, ast->floating.value);
            return true;
        case AST_Variable:
            map_find_ptr(variable_map, (char*)ast->variable.name);
            stackvar_t* var = map_as_ptr(variable_map);
            if (var->is_global) jitc_asm(list, IROpCode_laddr, var->var.ptr, type(var->var.type));
            else jitc_asm(list, IROpCode_lstack, var->var.offset, type(var->var.type));
            if (var->var.type->kind == Type_Array)
                jitc_asm(list, IROpCode_addrof);
            return true;
        case AST_WalkStruct:
            assemble(list, ast->walk_struct.struct_ptr, variable_map);
            jitc_asm(list, IROpCode_offset, ast->walk_struct.offset);
            break;
        default: break;
    }
    return false;
}

void jitc_compile(jitc_context_t* context, jitc_ast_t* ast) {
    switch (ast->node_type) {
        case AST_Binary: {
            void* ptr = jitc_get_or_static(context, ast->binary.left->variable.name);
            memcpy(ptr, &ast->binary.right->integer.value, ast->exprtype->size);
        } break;
        case AST_Function: {
            list_t* list = list_new();
            map_t* variable_map = map_new(compare_int64);
            bool is_return = false;
            jitc_scope_t* global_scope = list_get_ptr(context->scopes, 0);
            for (size_t i = 0; i < map_size(global_scope->variables); i++) {
                map_index(global_scope->variables, i);
                const char* name = map_get_ptr_key(global_scope->variables, i);
                jitc_variable_t* var = map_as_ptr(global_scope->variables);
                stackvar_t* stackvar = malloc(sizeof(stackvar_t));
                stackvar->is_global = stackvar->is_leaf = true;
                stackvar->var.type = var->type;
                if (var->decltype == Decltype_Extern) stackvar->var.ptr = (void*)var->value;
                else stackvar->var.ptr = &var->value;
                map_get_ptr(variable_map, (char*)name);
                map_store_ptr(variable_map, stackvar);
            }
            jitc_asm(list, IROpCode_func, ast->func.variable, get_stack_size(variable_map, ast->func.body));
            for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
                jitc_ast_t* node = list_get_ptr(ast->func.body->list.inner, i);
                if (assemble(list, node, variable_map)) jitc_asm(list, IROpCode_pop);
                is_return = node->node_type == AST_Return;
            }
            if (!is_return) {
                jitc_asm(list, IROpCode_pushi, 0, Type_Int32, false);
                jitc_asm(list, IROpCode_ret);
            }
            jitc_asm(list, IROpCode_func_end);
            *(void**)jitc_get_or_static(context, ast->func.variable->name) = jitc_assemble(list);
        } break;
        default: break;
    }
}
