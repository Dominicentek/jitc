#include "compares.h"
#include "dynamics.h"
#include "jitc.h"
#include "jitc_internal.h"

#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>

#if defined(_WIN32) && defined(__x86_64__)
#include "platform/win-x86_64.c"
#elif defined(__x86_64__)
#include "platform/sysv-x86_64.c"
#elif defined(__aarch64__)
#include "platform/sysv-aarch64.c"
#endif

#define type(x) x->kind, x->is_unsigned

typedef struct stackvar_t stackvar_t;
struct stackvar_t {
    bool is_leaf;
    bool is_global;
    union {
        list(stackvar_t)* list;
        struct {
            jitc_type_t* type;
            union {
                jitc_variable_t* ptr;
                size_t offset;
            };
        } var;
    };
};

static void* make_executable(jitc_context_t* context, void* ptr, size_t size) {
    size_t chunk_size = size;
    if (chunk_size % 16 != 0) chunk_size += 16 - (chunk_size % 16);
    for (size_t i = 0; i < list_size(context->memchunks); i++) {
        jitc_memchunk_t* memchunk = &list_get(context->memchunks, i);
        if (memchunk->avail >= chunk_size) {
            protect_rw(memchunk->ptr, memchunk->capacity);
            void* chunk = (char*)memchunk->ptr + memchunk->capacity - memchunk->avail;
            memchunk->avail -= chunk_size;
            memcpy(chunk, ptr, size);
            protect_rx(memchunk->ptr, memchunk->capacity);
            return chunk;
        }
    }
    size_t page_size = getpagesize();
    size_t num_pages = (chunk_size + page_size - 1) / page_size;
    void* chunk = alloc_page(num_pages * page_size);
    memcpy(chunk, ptr, size);
    jitc_memchunk_t* memchunk = &list_add(context->memchunks);
    memchunk->avail = num_pages * page_size - chunk_size;
    memchunk->capacity = num_pages * page_size;
    memchunk->ptr = chunk;
    protect_rx(chunk, num_pages * page_size);
    return chunk;
}

void jitc_delete_memchunks(jitc_context_t* context) {
    for (size_t i = 0; i < list_size(context->memchunks); i++) {
        jitc_memchunk_t* memchunk = &list_get(context->memchunks, i);
        munmap(memchunk->ptr, memchunk->capacity);
    }
    list_delete(context->memchunks);
}

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

static void append_to_size_tree(list_t* _list, jitc_ast_t* node) {
    list(stackvar_t)* list = _list;
    if (!node) return;
    switch (node->node_type) {
        case AST_Declaration: {
            stackvar_t* size = &list_add(list);
            size->is_leaf = true;
            size->is_global = false;
            size->var.type = node->decl.type;
            if (node->decl.decltype == Decltype_Extern) {
                size->is_global = true;
                size->var.ptr = node->decl.variable;
            }
        } break;
        case AST_List:
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_size_tree(list, list_get(node->list.inner, i));
            }
            break;
        case AST_Scope: {
            stackvar_t* size = &list_add(list);
            size->is_leaf = false;
            size->is_global = false;
            size->list = list_new(stackvar_t);
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_size_tree(size->list, list_get(node->list.inner, i));
            }
        } break;
        case AST_Ternary:
        case AST_Branch: {
            stackvar_t* size = &list_add(list);
            size->is_leaf = false;
            size->is_global = false;
            size->list = list_new(stackvar_t);
            append_to_size_tree(size->list, node->ternary.when);
            append_to_size_tree(size->list, node->ternary.then);
            append_to_size_tree(size->list, node->ternary.otherwise);
        } break;
        case AST_Loop: {
            stackvar_t* size = &list_add(list);
            size->is_leaf = false;
            size->is_global = false;
            size->list = list_new(stackvar_t);
            append_to_size_tree(size->list, node->loop.cond);
            append_to_size_tree(size->list, node->loop.body);
        } break;
        default: break;
    }
}

static size_t process_size_tree(map_t* _variable_map, stackvar_t* tree, size_t parent_size) {
    map(char*, stackvar_t)* variable_map = _variable_map;
    size_t size = parent_size;
    for (size_t i = 0; i < list_size(tree->list); i++) {
        stackvar_t* node = &list_get(tree->list, i);
        if (!node->is_leaf) continue;
        if (!node->var.type->name) continue;
        if (size % node->var.type->alignment != 0) size += node->var.type->alignment - size % node->var.type->alignment;
        if (!node->is_global) {
            node->var.offset = size + node->var.type->size;
            size += node->var.type->size;
        }
        map_add(variable_map) = (char*)node->var.type->name;
        map_commit(variable_map);
        map_get_value(variable_map) = *node;
    }
    size_t max_size = size;
    for (size_t i = 0; i < list_size(tree->list); i++) {
        stackvar_t* node = &list_get(tree->list, i);
        if (node->is_leaf) continue;
        size_t node_size = process_size_tree(variable_map, node, size);
        if (max_size < node_size) max_size = node_size;
    }
    list_delete(tree->list);
    return max_size;
}

static size_t get_stack_size(map_t* variable_map, jitc_ast_t* ast, jitc_type_t* type) {
    stackvar_t* size = malloc(sizeof(stackvar_t));
    size->is_leaf = false;
    size->list = list_new(stackvar_t);
    for (size_t i = 0; i < type->func.num_params; i++) {
        if (!type->func.params[i]->name) continue;
        stackvar_t* param = &list_add(size->list);
        param->is_leaf = true;
        param->is_global = false;
        param->var.type = type->func.params[i];
    }
    append_to_size_tree(size->list, ast);
    return process_size_tree(variable_map, size, 0);
}

static bool assemble(bytewriter_t* writer, jitc_ast_t* ast, map_t* _variable_map, jitc_binary_op_t parent_op) {
    map(char*, stackvar_t)* variable_map = _variable_map;
    if (!ast) return false;
    size_t step = 1;
    switch (ast->node_type) {
        case AST_Unary: switch(ast->unary.operation) {
            case Unary_ArithPlus: assemble(writer, ast->unary.inner, variable_map, 0); promote(writer, ast->unary.inner); break;
            case Unary_ArithNegate: assemble(writer, ast->unary.inner, variable_map, 0); promote(writer, ast->unary.inner); jitc_asm_neg(writer); break;
            case Unary_LogicNegate: assemble(writer, ast->unary.inner, variable_map, 0); promote(writer, ast->unary.inner); jitc_asm_zero(writer); break;
            case Unary_BinaryNegate: assemble(writer, ast->unary.inner, variable_map, 0); promote(writer, ast->unary.inner); jitc_asm_not(writer); break;
            case Unary_Dereference:
                assemble(writer, ast->unary.inner, variable_map, 0);
                promote(writer, ast->unary.inner);
                jitc_asm_load(writer, type(ast->exprtype));
                if (ast->exprtype->kind == Type_Array) jitc_asm_addrof(writer);
                break;
            case Unary_AddressOf: assemble(writer, ast->unary.inner, variable_map, 0); jitc_asm_addrof(writer); break;
            case Unary_PtrPrefixIncrement: case Unary_PtrPrefixDecrement: case Unary_PtrSuffixIncrement: case Unary_PtrSuffixDecrement:
                step = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
            case Unary_PrefixIncrement: case Unary_PrefixDecrement: case Unary_SuffixIncrement: case Unary_SuffixDecrement:
                assemble(writer, ast->unary.inner, variable_map, 0);
                jitc_asm_inc(writer, ast->unary.operation & 0b10, step * (ast->unary.operation & 0b01 ? -1 : 1));
                break;
        } return true;
        case AST_Binary:
            if (ast->binary.operation == Binary_Cast) {
                jitc_type_t* type = ast->binary.right->type.type->kind == Type_Void
                    ? ast->binary.left->exprtype
                    : ast->binary.right->type.type;
                assemble(writer, ast->binary.left, variable_map, 0);
                jitc_asm_cvt(writer, type(type));
            }
            else if (ast->binary.operation == Binary_FunctionCall) {
                size_t num_args = list_size(ast->binary.right->list.inner);
                jitc_type_t* signature = ast->binary.left->exprtype;
                jitc_type_t* args[num_args];
                if (signature->kind == Type_Pointer) signature = signature->ptr.base;
                for (size_t i = num_args - 1; i < num_args; i--) {
                    jitc_ast_t* arg = list_get(ast->binary.right->list.inner, i);
                    args[i] = arg->exprtype;
                    assemble(writer, arg, variable_map, 0);
                }
                assemble(writer, ast->binary.left, variable_map, 0);
                jitc_asm_call(writer, signature, args, num_args);
            }
            else if (ast->binary.operation == Binary_Comma) {
                assemble(writer, ast->binary.left, variable_map, 0);
                jitc_asm_pop(writer);
                assemble(writer, ast->binary.right, variable_map, 0);
            }
            else {
                if (ast->binary.operation != Binary_LogicAnd && ast->binary.operation != Binary_LogicOr) {
                    if (get_su_number(ast->binary.left) < get_su_number(ast->binary.right) && ast->binary.operation < Binary_Assignment) {
                        assemble(writer, ast->binary.right, variable_map, 0);
                        assemble(writer, ast->binary.left, variable_map, 0);
                        jitc_asm_swp(writer);
                    }
                    else {
                        assemble(writer, ast->binary.left, variable_map, 0);
                        assemble(writer, ast->binary.right, variable_map, 0);
                    }
                }
                switch (ast->binary.operation) {
                    case Binary_Assignment:
                    case Binary_AssignConst: {
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
                    case Binary_LogicAnd:
                    case Binary_LogicOr: {
                        if (parent_op != ast->binary.operation) jitc_asm_sc_begin(writer);
                        assemble(writer, ast->binary.left, variable_map, ast->binary.operation);
                        (void(*[])(bytewriter_t*)){
                            [Binary_LogicAnd] = jitc_asm_land,
                            [Binary_LogicOr] = jitc_asm_lor,
                        }[ast->binary.operation](writer);
                        assemble(writer, ast->binary.right, variable_map, ast->binary.operation);
                        if (parent_op != ast->binary.operation) jitc_asm_sc_end(writer);
                    } break;
                    case Binary_PtrAddition:
                    case Binary_PtrSubtraction:
                    case Binary_AssignPtrAddition:
                    case Binary_AssignPtrSubtraction: {
                        size_t size = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
                        jitc_asm_normalize(writer, size);
                        (void(*[])(bytewriter_t*)){
                            [Binary_PtrAddition] = jitc_asm_add,
                            [Binary_PtrSubtraction] = jitc_asm_sub,
                            [Binary_AssignPtrAddition] = jitc_asm_sadd,
                            [Binary_AssignPtrSubtraction] = jitc_asm_ssub,
                        }[ast->binary.operation](writer);
                    } break;
                    case Binary_PtrDiff:
                    case Binary_AssignPtrDiff: {
                        jitc_type_t* type = ast->binary.left->exprtype->ptr.base;
                        size_t size = type->kind == Type_Function ? 1 : type->size;
                        (void(*[])(bytewriter_t*)){
                            [Binary_PtrDiff] = jitc_asm_sub,
                            [Binary_AssignPtrDiff] = jitc_asm_ssub,
                        }[ast->binary.operation](writer);
                        jitc_asm_normalize(writer, -size);
                    } break;
                    default: break;
                }
            }
            return true;
        case AST_Ternary:
            jitc_asm_if(writer, false);
            if (ast->ternary.when) assemble(writer, ast->ternary.when, variable_map, 0);
            else jitc_asm_pushi(writer, 0, Type_Int32, false);
            jitc_asm_then(writer);
            assemble(writer, ast->ternary.then, variable_map, 0);
            jitc_asm_rval(writer);
            jitc_asm_pop(writer);
            jitc_asm_else(writer);
            assemble(writer, ast->ternary.otherwise, variable_map, 0);
            jitc_asm_rval(writer);
            jitc_asm_end(writer);
            return true;
        case AST_Branch:
            jitc_asm_if(writer, false);
            if (ast->ternary.when) assemble(writer, ast->ternary.when, variable_map, 0);
            else jitc_asm_pushi(writer, 0, Type_Int32, false);
            jitc_asm_then(writer);
            if (assemble(writer, ast->ternary.then, variable_map, 0)) jitc_asm_pop(writer);
            jitc_asm_else(writer);
            if (assemble(writer, ast->ternary.otherwise, variable_map, 0)) jitc_asm_pop(writer);
            jitc_asm_end(writer);
            return false;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                if (assemble(writer, list_get(ast->list.inner, i), variable_map, 0)) jitc_asm_pop(writer);
            }
            return false;
        case AST_Loop:
            jitc_asm_if(writer, true);
            if (ast->loop.cond) assemble(writer, ast->loop.cond, variable_map, 0);
            else jitc_asm_pushi(writer, 1, Type_Int32, false);
            jitc_asm_then(writer);
            assemble(writer, ast->loop.body, variable_map, 0);
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
            if (ast->ret.expr) assemble(writer, ast->ret.expr, variable_map, 0);
            else jitc_asm_pushi(writer, 0, Type_Int64, true);
            jitc_asm_ret(writer);
            return false;
        case AST_Integer:
        case AST_StringLit:
            jitc_asm_pushi(writer, ast->integer.value, type(ast->exprtype));
            return true;
        case AST_Floating:
            if (ast->floating.is_single_precision) jitc_asm_pushf(writer, ast->floating.value);
            else jitc_asm_pushd(writer, ast->floating.value);
            return true;
        case AST_Variable:
            map_find(variable_map, &ast->variable.name);
            stackvar_t* var = &map_get_value(variable_map);
            jitc_type_kind_t kind = var->var.type->kind;
            bool is_unsigned = var->var.type->is_unsigned;
            if (var->var.type->kind == Type_Array) {
                kind = var->is_global ? Type_Pointer : var->var.type->arr.base->kind;
                is_unsigned = var->var.type->arr.base->is_unsigned;
            }
            if (var->is_global) jitc_asm_laddr(writer, var->var.ptr, kind, is_unsigned);
            else jitc_asm_lstack(writer, var->var.offset, kind, is_unsigned);
            if ((var->var.type->kind == Type_Array || var->var.type->kind == Type_Function) && !ast->variable.write_dest)
                jitc_asm_addrof(writer);
            return true;
        case AST_WalkStruct:
            assemble(writer, ast->walk_struct.struct_ptr, variable_map, 0);
            jitc_asm_type(writer, type(ast->exprtype));
            jitc_asm_offset(writer, ast->walk_struct.offset);
            if (ast->exprtype->kind == Type_Array) jitc_asm_addrof(writer);
            return false;
        case AST_Initializer: {
            assemble(writer, ast->init.store_to, variable_map, 0);
            jitc_asm_init(writer, ast->exprtype->size, ast->exprtype->alignment);
            size_t curr_offset = 0;
            for (size_t i = 0; i < list_size(ast->init.items); i++) {
                size_t diff = list_get(ast->init.offsets, i) - curr_offset;
                jitc_asm_offset(writer, diff);
                assemble(writer, list_get(ast->init.items, i), variable_map, 0);
                jitc_asm_store(writer);
                curr_offset += diff;
            }
            jitc_asm_offset(writer, -curr_offset);
            if (ast->exprtype->kind == Type_Array) jitc_asm_addrof(writer);
        } return true;
        case AST_Goto: {
            jitc_asm_goto(writer, ast->label.name);
        } return false;
        case AST_Label: {
            jitc_asm_label(writer, ast->label.name);
        } return false;
        case AST_Interrupt: {
            jitc_asm_int(writer);
        } return false;
        default: break;
    }
    return false;
}

void jitc_compile(jitc_context_t* context, jitc_ast_t* ast) {
    switch (ast->node_type) {
        case AST_Declaration: {
            if (!ast->decl.type->name) break;
            jitc_variable_t* var = jitc_get_or_static(context, ast->decl.type->name);
            if ((var->decltype == Decltype_Static || var->decltype == Decltype_None) && var->type->kind != Type_Function)
                var->ptr = var->ptr ?: calloc(var->type->size, 1);
            ast->decl.variable = var;
        } break;
        case AST_Binary:
        case AST_Initializer: {
            jitc_variable_t* var = jitc_get_or_static(context, ast->binary.left->variable.name);
            if (!var->initial) {
                if (var->preserve_policy == Preserve_Always) break;
                if (var->preserve_policy == Preserve_IfConst) {
                    if (var->type->is_const || var->type->kind == Type_Function || var->type->kind == Type_Array) break;
                }
            }
            var->initial = false;
            if (ast->node_type == AST_Binary) memcpy(var->ptr, &ast->binary.right->integer.value, ast->exprtype->size);
            else for (size_t i = 0; i < list_size(ast->init.items); i++) {
                jitc_ast_t* node = list_get(ast->init.items, i);
                void* ptr = (uint8_t*)var->ptr + list_get(ast->init.offsets, i);
                memcpy(ptr, &node->integer.value, node->exprtype->size);
            }
        } break;
        case AST_Function: {
            jitc_scope_t* global_scope = &list_get(context->scopes, 0);
            map_find(global_scope->variables, &ast->func.variable->name);
            jitc_variable_t* var = map_get_value(global_scope->variables);
            if (var->decltype == Decltype_Extern || var->decltype == Decltype_Typedef) break;
            if (var->func && var->preserve_policy == Preserve_Always) break;

            smartptr(map(char*, stackvar_t)) variable_map = map_new(compare_string, char*, stackvar_t);
            bytewriter_t* writer = bytewriter_new();
            bool is_return = false;
            for (size_t i = 0; i < map_size(global_scope->variables); i++) {
                map_index(global_scope->variables, i);
                const char* name = map_get_key(global_scope->variables);
                jitc_variable_t* var = map_get_value(global_scope->variables);
                map_add(variable_map) = (char*)name;
                map_commit(variable_map);
                stackvar_t* stackvar = &map_get_value(variable_map);
                stackvar->is_global = stackvar->is_leaf = true;
                stackvar->var.type = var->type;
                stackvar->var.ptr = var;
            }
            jitc_asm_func(writer, ast->func.variable, get_stack_size(variable_map, ast->func.body, ast->func.variable));
            for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
                jitc_ast_t* node = list_get(ast->func.body->list.inner, i);
                if (assemble(writer, node, variable_map, 0)) jitc_asm_pop(writer);
                is_return = node->node_type == AST_Return;
            }
            if (!is_return) {
                jitc_type_t* ret = ast->func.variable->func.ret;
                if (ret->kind != Type_Void) {
                    jitc_asm_pushi(writer, 0, ret->kind, ret->is_unsigned);
                    jitc_asm_ret(writer);
                }
            }
            jitc_asm_func_end(writer);
            size_t size = bytewriter_size(writer);
            autofree void* data = bytewriter_delete(writer);
            void* func_ptr = make_executable(context, data, size);
#if JITC_DEBUG || JITC_DEBUG_GDB
            jitc_gdb_map_function(func_ptr, (char*)func_ptr + size, ast->func.variable->name);
#endif
            if (var->func) {
                var->func->addr->ptr = func_ptr;
                var->func->addr->size = size;
            }
            else {
                autofree jitc_func_trampoline_t* func = malloc(sizeof(jitc_func_trampoline_t));
                func->addr = malloc(sizeof(jitc_func_cell_t));
                func->addr->ptr = func_ptr;
                func->addr->size = size;
                func->mov_rax[0] = 0x48; func->mov_rax[1] = 0xB8;
                func->jmp_rax[0] = 0xFF; func->jmp_rax[1] = 0x20;
                var->func = make_executable(context, func, sizeof(jitc_func_trampoline_t));
#if JITC_DEBUG || JITC_DEBUG_GDB
                jitc_gdb_map_function(var->func, (char*)var->func + sizeof(jitc_func_trampoline_t), ast->func.variable->name);
#endif
            }
        } break;
        default: break;
    }
    jitc_link(context);
}
