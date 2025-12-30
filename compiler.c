#include "compares.h"
#include "dynamics.h"
#include "jitc.h"
#include "jitc_internal.h"

#include <stdlib.h>
#include <stdarg.h>

#if defined(_WIN32) && defined(__x86_64__)
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

static void promote(bytewriter_t* writer, jitc_ast_t* ast) {
    if (ast->exprtype->kind < Type_Int32)
        jitc_asm_cvt(writer, Type_Int32, ast->exprtype->is_unsigned);
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

static bool assemble(bytewriter_t* writer, jitc_ast_t* ast, map_t* variable_map) {
    size_t step = 1;
    switch (ast->node_type) {
        case AST_Unary: switch(ast->unary.operation) {
            case Unary_ArithPlus: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); break;
            case Unary_ArithNegate: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_neg(writer); break;
            case Unary_LogicNegate: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_zero(writer); break;
            case Unary_BinaryNegate: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_not(writer); break;
            case Unary_Dereference: assemble(writer, ast->unary.inner, variable_map); promote(writer, ast->unary.inner); jitc_asm_load(writer, ast->exprtype->kind, ast->exprtype->is_unsigned); break;
            case Unary_AddressOf: assemble(writer, ast->unary.inner, variable_map); jitc_asm_addrof(writer); break;
            case Unary_PtrPrefixIncrement: case Unary_PtrPrefixDecrement: case Unary_PtrSuffixIncrement: case Unary_PtrSuffixDecrement:
                step = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
            case Unary_PrefixIncrement: case Unary_PrefixDecrement: case Unary_SuffixIncrement: case Unary_SuffixDecrement:
                assemble(writer, ast->unary.inner, variable_map);
                jitc_asm_inc(writer, ast->unary.operation & 0b10, step * (ast->unary.operation & 0b01 ? -1 : 1));
                break;
        } return true;
        case AST_Binary:
            if (ast->binary.operation == Binary_Cast) {
                assemble(writer, ast->binary.left, variable_map);
                jitc_asm_cvt(writer, type(ast->binary.right->type.type));
            }
            else if (ast->binary.operation == Binary_CompoundExpr) {}
            else if (ast->binary.operation == Binary_FunctionCall) {
                size_t num_args = list_size(ast->binary.right->list.inner);
                jitc_type_t* signature = ast->binary.left->exprtype;
                jitc_type_t* args[num_args];
                if (signature->kind == Type_Pointer) signature = signature->ptr.base;
                for (size_t i = num_args - 1; i < num_args; i--) {
                    jitc_ast_t* arg = list_get_ptr(ast->binary.right->list.inner, i);
                    args[i] = arg->exprtype;
                    assemble(writer, arg, variable_map);
                }
                assemble(writer, ast->binary.left, variable_map);
                jitc_asm_call(writer, signature, args, num_args);
            }
            else {
                if (ast->binary.operation == Binary_Comma) {
                    assemble(writer, ast->binary.left, variable_map);
                    jitc_asm_pop(writer);
                    assemble(writer, ast->binary.right, variable_map);
                    break;
                }
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
                    case Binary_Assignment: {
                        if (ast->binary.left->exprtype->kind == Type_Struct || ast->binary.left->exprtype->kind == Type_Union) {
                            jitc_type_t* type = ast->binary.left->exprtype;
                            jitc_asm_copy(writer, type->size, type->alignment);
                        }
                        else jitc_asm_store(writer);
                    } break;
                    case Binary_Addition:             jitc_asm_add(writer); break;
                    case Binary_Subtraction:          jitc_asm_sub(writer); break;
                    case Binary_Multiplication:       jitc_asm_mul(writer); break;
                    case Binary_Division:             jitc_asm_div(writer); break;
                    case Binary_Modulo:               jitc_asm_mod(writer); break;
                    case Binary_BitshiftLeft:         jitc_asm_shl(writer); break;
                    case Binary_BitshiftRight:        jitc_asm_shr(writer); break;
                    case Binary_And:                  jitc_asm_and(writer); break;
                    case Binary_Or:                   jitc_asm_or(writer);  break;
                    case Binary_Xor:                  jitc_asm_xor(writer); break;
                    case Binary_Equals:               jitc_asm_eql(writer); break;
                    case Binary_NotEquals:            jitc_asm_neq(writer); break;
                    case Binary_LessThan:             jitc_asm_lst(writer); break;
                    case Binary_LessThanOrEqualTo:    jitc_asm_lte(writer); break;
                    case Binary_GreaterThan:          jitc_asm_grt(writer); break;
                    case Binary_GreaterThanOrEqualTo: jitc_asm_gte(writer); break;
                    case Binary_AssignAddition:       jitc_asm_sadd(writer); break;
                    case Binary_AssignSubtraction:    jitc_asm_ssub(writer); break;
                    case Binary_AssignMultiplication: jitc_asm_smul(writer); break;
                    case Binary_AssignDivision:       jitc_asm_sdiv(writer); break;
                    case Binary_AssignModulo:         jitc_asm_smod(writer); break;
                    case Binary_AssignBitshiftLeft:   jitc_asm_sshl(writer); break;
                    case Binary_AssignBitshiftRight:  jitc_asm_sshr(writer); break;
                    case Binary_AssignAnd:            jitc_asm_sand(writer); break;
                    case Binary_AssignOr:             jitc_asm_sor(writer);  break;
                    case Binary_AssignXor:            jitc_asm_sxor(writer); break;
                    case Binary_LogicAnd: break; // todo
                    case Binary_LogicOr:  break; // todo
                    case Binary_PtrAddition:
                    case Binary_PtrSubtraction:
                    case Binary_AssignPtrAddition:
                    case Binary_AssignPtrSubtraction: {
                        size_t size = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
                        if (size != 1) {
                            jitc_asm_pushi(writer, size, ast->binary.right->exprtype->kind, ast->binary.right->exprtype->is_unsigned);
                            jitc_asm_mul(writer);
                        }
                        (void(*[])(bytewriter_t*)){
                            jitc_asm_add,
                            jitc_asm_sub,
                            jitc_asm_sadd,
                            jitc_asm_ssub,
                        }[ast->binary.operation](writer);
                    }
                    default: break;
                }
            }
            return true;
        case AST_Ternary:
            jitc_asm_if(writer, false);
            if (ast->ternary.when) assemble(writer, ast->ternary.when, variable_map);
            else jitc_asm_pushi(writer, 0, Type_Int32, false);
            jitc_asm_then(writer);
            assemble(writer, ast->ternary.then, variable_map);
            jitc_asm_rval(writer);
            jitc_asm_pop(writer);
            jitc_asm_else(writer);
            assemble(writer, ast->ternary.otherwise, variable_map);
            jitc_asm_rval(writer);
            jitc_asm_end(writer);
            return true;
        case AST_Branch:
            jitc_asm_if(writer, false);
            if (ast->ternary.when) assemble(writer, ast->ternary.when, variable_map);
            else jitc_asm_pushi(writer, 0, Type_Int32, false);
            jitc_asm_then(writer);
            if (assemble(writer, ast->ternary.then, variable_map)) jitc_asm_pop(writer);
            jitc_asm_else(writer);
            if (assemble(writer, ast->ternary.otherwise, variable_map)) jitc_asm_pop(writer);
            jitc_asm_end(writer);
            return false;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                if (assemble(writer, list_get_ptr(ast->list.inner, i), variable_map)) jitc_asm_pop(writer);
            }
            return false;
        case AST_Loop:
            jitc_asm_if(writer, true);
            if (ast->loop.cond) assemble(writer, ast->loop.cond, variable_map);
            else jitc_asm_pushi(writer, 1, Type_Int32, false);
            jitc_asm_then(writer);
            assemble(writer, ast->loop.body, variable_map);
            jitc_asm_goto_start(writer);
            jitc_asm_else(writer);
            jitc_asm_end(writer);
            return false;
        case AST_Break:
            jitc_asm_goto_end(writer);
            return false;
        case AST_Continue:
            jitc_asm_goto_start(writer);
            return false;
        case AST_Return:
            if (ast->ret.expr) assemble(writer, ast->ret.expr, variable_map);
            else jitc_asm_pushi(writer, 0, Type_Int64, true);
            jitc_asm_ret(writer);
            return false;
        case AST_Integer:
            jitc_asm_pushi(writer, ast->integer.value, ast->exprtype->kind, ast->exprtype->is_unsigned);
            return true;
        case AST_StringLit:
            jitc_asm_pushi(writer, (uint64_t)ast->string.ptr, ast->exprtype->kind, ast->exprtype->is_unsigned);
            return true;
        case AST_Floating:
            if (ast->floating.is_single_precision) jitc_asm_pushf(writer, ast->floating.value);
            else jitc_asm_pushd(writer, ast->floating.value);
            return true;
        case AST_Variable:
            map_find_ptr(variable_map, (char*)ast->variable.name);
            stackvar_t* var = map_as_ptr(variable_map);
            if (var->is_global) jitc_asm_laddr(writer, var->var.ptr, type(var->var.type));
            else jitc_asm_lstack(writer, var->var.offset, type(var->var.type));
            if (var->var.type->kind == Type_Array)
                jitc_asm_addrof(writer);
            return true;
        case AST_WalkStruct:
            assemble(writer, ast->walk_struct.struct_ptr, variable_map);
            jitc_asm_offset(writer, ast->walk_struct.offset);
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
            bytewriter_t* writer = bytewriter_new();
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
            jitc_asm_func(writer, ast->func.variable, get_stack_size(variable_map, ast->func.body));
            for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
                jitc_ast_t* node = list_get_ptr(ast->func.body->list.inner, i);
                if (assemble(writer, node, variable_map)) jitc_asm_pop(writer);
                is_return = node->node_type == AST_Return;
            }
            if (!is_return) {
                jitc_asm_pushi(writer, 0, Type_Int32, false);
                jitc_asm_ret(writer);
            }
            jitc_asm_func_end(writer);
            size_t size = bytewriter_size(writer);
            uint8_t* data = bytewriter_data(writer);
            printf("%s:", ast->func.variable->name);
            for (size_t i = 0; i < size; i++) {
                if (i % 16 == 0) printf("\n  ");
                printf("%02x ", data[i]);
            }
            printf("\n");
            *(void**)jitc_get_or_static(context, ast->func.variable->name) = bytewriter_delete(writer);
        } break;
        default: break;
    }
}
