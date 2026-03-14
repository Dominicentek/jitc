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

#define INT(x) { .i = (x) }
#define FLT(x) { .f = (x) }
#define DBL(x) { .d = (x) }
#define PTR(x) { .p = (void*)(x) }
#define IR(opc, ...) (jitc_ir_t){ opc, { __VA_ARGS__ }}
#define type(x) INT(x->kind), INT(x->is_unsigned)

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

static void promote(list_t* _ir, jitc_ast_t* ast) {
    list(jitc_ir_t)* ir = _ir;
    if (ast->exprtype->kind < Type_Int32)
        list_add(ir) = IR(IR_cvt, INT(Type_Int32), INT(ast->exprtype->is_unsigned));
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

static bool assemble(list_t* _ir, jitc_ast_t* ast, map_t* _variable_map, jitc_binary_op_t parent_op) {
    list(jitc_ir_t)* ir = _ir;
    map(char*, stackvar_t)* variable_map = _variable_map;
    if (!ast) return false;
    size_t step = 1;
    switch (ast->node_type) {
        case AST_Unary: switch(ast->unary.operation) {
            case Unary_ArithPlus: assemble(ir, ast->unary.inner, variable_map, 0); promote(ir, ast->unary.inner); break;
            case Unary_ArithNegate: assemble(ir, ast->unary.inner, variable_map, 0); promote(ir, ast->unary.inner); list_add(ir) = IR(IR_neg); break;
            case Unary_LogicNegate: assemble(ir, ast->unary.inner, variable_map, 0); promote(ir, ast->unary.inner); list_add(ir) = IR(IR_zero); break;
            case Unary_BinaryNegate: assemble(ir, ast->unary.inner, variable_map, 0); promote(ir, ast->unary.inner); list_add(ir) = IR(IR_not); break;
            case Unary_Dereference:
                assemble(ir, ast->unary.inner, variable_map, 0);
                promote(ir, ast->unary.inner);
                list_add(ir) = IR(IR_load, type(ast->exprtype));
                if (ast->exprtype->kind == Type_Array) list_add(ir) = IR(IR_addrof);
                break;
            case Unary_AddressOf: assemble(ir, ast->unary.inner, variable_map, 0); list_add(ir) = IR(IR_addrof); break;
            case Unary_PtrPrefixIncrement: case Unary_PtrPrefixDecrement: case Unary_PtrSuffixIncrement: case Unary_PtrSuffixDecrement:
                step = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
            case Unary_PrefixIncrement: case Unary_PrefixDecrement: case Unary_SuffixIncrement: case Unary_SuffixDecrement:
                assemble(ir, ast->unary.inner, variable_map, 0);
                list_add(ir) = IR(IR_inc, INT(ast->unary.operation & 0b10), INT(step * (ast->unary.operation & 0b01 ? -1 : 1)));
                break;
        } return true;
        case AST_Binary:
            if (ast->binary.operation == Binary_Cast) {
                jitc_type_t* type = ast->binary.right->type.type->kind == Type_Void
                    ? ast->binary.left->exprtype
                    : ast->binary.right->type.type;
                assemble(ir, ast->binary.left, variable_map, 0);
                list_add(ir) = IR(IR_cvt, type(type));
            }
            else if (ast->binary.operation == Binary_FunctionCall) {
                size_t num_args = list_size(ast->binary.right->list.inner);
                jitc_type_t* signature = ast->binary.left->exprtype;
                jitc_type_t** args = malloc(sizeof(jitc_type_t*) * num_args);
                if (signature->kind == Type_Pointer) signature = signature->ptr.base;
                for (size_t i = num_args - 1; i < num_args; i--) {
                    jitc_ast_t* arg = list_get(ast->binary.right->list.inner, i);
                    args[i] = arg->exprtype;
                    assemble(ir, arg, variable_map, 0);
                }
                assemble(ir, ast->binary.left, variable_map, 0);
                list_add(ir) = IR(IR_call, PTR(signature), PTR(args), INT(num_args));
            }
            else if (ast->binary.operation == Binary_Comma) {
                assemble(ir, ast->binary.left, variable_map, 0);
                list_add(ir) = IR(IR_pop);
                assemble(ir, ast->binary.right, variable_map, 0);
            }
            else {
                if (ast->binary.operation != Binary_LogicAnd && ast->binary.operation != Binary_LogicOr) {
                    if (get_su_number(ast->binary.left) < get_su_number(ast->binary.right) && ast->binary.operation < Binary_Assignment) {
                        assemble(ir, ast->binary.right, variable_map, 0);
                        assemble(ir, ast->binary.left, variable_map, 0);
                        list_add(ir) = IR(IR_swp);
                    }
                    else {
                        assemble(ir, ast->binary.left, variable_map, 0);
                        assemble(ir, ast->binary.right, variable_map, 0);
                    }
                }
                switch (ast->binary.operation) {
                    case Binary_Assignment:
                    case Binary_AssignConst: {
                        if (ast->binary.left->exprtype->kind == Type_Struct || ast->binary.left->exprtype->kind == Type_Union) {
                            jitc_type_t* type = ast->binary.left->exprtype;
                            list_add(ir) = IR(IR_copy, INT(type->size), INT(type->alignment));
                        }
                        else list_add(ir) = IR(IR_store);
                    } break;
                    case Binary_Addition:             list_add(ir) = IR(IR_add); break;
                    case Binary_Subtraction:          list_add(ir) = IR(IR_sub); break;
                    case Binary_Multiplication:       list_add(ir) = IR(IR_mul); break;
                    case Binary_Division:             list_add(ir) = IR(IR_div); break;
                    case Binary_Modulo:               list_add(ir) = IR(IR_mod); break;
                    case Binary_BitshiftLeft:         list_add(ir) = IR(IR_shl); break;
                    case Binary_BitshiftRight:        list_add(ir) = IR(IR_shr); break;
                    case Binary_And:                  list_add(ir) = IR(IR_and); break;
                    case Binary_Or:                   list_add(ir) = IR(IR_or);  break;
                    case Binary_Xor:                  list_add(ir) = IR(IR_xor); break;
                    case Binary_Equals:               list_add(ir) = IR(IR_eql); break;
                    case Binary_NotEquals:            list_add(ir) = IR(IR_neq); break;
                    case Binary_LessThan:             list_add(ir) = IR(IR_lst); break;
                    case Binary_LessThanOrEqualTo:    list_add(ir) = IR(IR_lte); break;
                    case Binary_GreaterThan:          list_add(ir) = IR(IR_grt); break;
                    case Binary_GreaterThanOrEqualTo: list_add(ir) = IR(IR_gte); break;
                    case Binary_AssignAddition:       list_add(ir) = IR(IR_sadd); break;
                    case Binary_AssignSubtraction:    list_add(ir) = IR(IR_ssub); break;
                    case Binary_AssignMultiplication: list_add(ir) = IR(IR_smul); break;
                    case Binary_AssignDivision:       list_add(ir) = IR(IR_sdiv); break;
                    case Binary_AssignModulo:         list_add(ir) = IR(IR_smod); break;
                    case Binary_AssignBitshiftLeft:   list_add(ir) = IR(IR_sshl); break;
                    case Binary_AssignBitshiftRight:  list_add(ir) = IR(IR_sshr); break;
                    case Binary_AssignAnd:            list_add(ir) = IR(IR_sand); break;
                    case Binary_AssignOr:             list_add(ir) = IR(IR_sor);  break;
                    case Binary_AssignXor:            list_add(ir) = IR(IR_sxor); break;
                    case Binary_LogicAnd:
                    case Binary_LogicOr: {
                        if (parent_op != ast->binary.operation) list_add(ir) = IR(IR_sc_begin);
                        assemble(ir, ast->binary.left, variable_map, ast->binary.operation);
                        list_add(ir) = IR(((jitc_ir_opcode_t[]){
                            [Binary_LogicAnd] = IR_land,
                            [Binary_LogicOr] = IR_lor,
                        })[ast->binary.operation]);
                        assemble(ir, ast->binary.right, variable_map, ast->binary.operation);
                        if (parent_op != ast->binary.operation) list_add(ir) = IR(IR_sc_end);
                    } break;
                    case Binary_PtrAddition:
                    case Binary_PtrSubtraction:
                    case Binary_AssignPtrAddition:
                    case Binary_AssignPtrSubtraction: {
                        size_t size = ast->exprtype->ptr.base->kind == Type_Function ? 1 : ast->exprtype->ptr.base->size;
                        list_add(ir) = IR(IR_normalize, INT(size));
                        list_add(ir) = IR(((jitc_ir_opcode_t[]){
                            [Binary_PtrAddition] = IR_add,
                            [Binary_PtrSubtraction] = IR_sub,
                            [Binary_AssignPtrAddition] = IR_sadd,
                            [Binary_AssignPtrSubtraction] = IR_ssub,
                        })[ast->binary.operation]);
                    } break;
                    case Binary_PtrDiff:
                    case Binary_AssignPtrDiff: {
                        jitc_type_t* type = ast->binary.left->exprtype->ptr.base;
                        size_t size = type->kind == Type_Function ? 1 : type->size;
                        list_add(ir) = IR(((jitc_ir_opcode_t[]){
                            [Binary_PtrDiff] = IR_sub,
                            [Binary_AssignPtrDiff] = IR_ssub,
                        })[ast->binary.operation]);
                        list_add(ir) = IR(IR_normalize, INT(-size));
                    } break;
                    default: break;
                }
            }
            return true;
        case AST_Ternary:
            list_add(ir) = IR(IR_if, INT(false));
            if (ast->ternary.when) assemble(ir, ast->ternary.when, variable_map, 0);
            else list_add(ir) = IR(IR_pushi, INT(0), INT(Type_Int32), INT(false));
            list_add(ir) = IR(IR_then);
            assemble(ir, ast->ternary.then, variable_map, 0);
            list_add(ir) = IR(IR_rval);
            list_add(ir) = IR(IR_pop);
            list_add(ir) = IR(IR_else);
            assemble(ir, ast->ternary.otherwise, variable_map, 0);
            list_add(ir) = IR(IR_rval);
            list_add(ir) = IR(IR_end);
            return true;
        case AST_Branch:
            list_add(ir) = IR(IR_if, INT(false));
            if (ast->ternary.when) assemble(ir, ast->ternary.when, variable_map, 0);
            else list_add(ir) = IR(IR_pushi, INT(0), INT(Type_Int32), INT(false));
            list_add(ir) = IR(IR_then);
            if (assemble(ir, ast->ternary.then, variable_map, 0)) list_add(ir) = IR(IR_pop);
            list_add(ir) = IR(IR_else);
            if (assemble(ir, ast->ternary.otherwise, variable_map, 0)) list_add(ir) = IR(IR_pop);
            list_add(ir) = IR(IR_end);
            return false;
        case AST_Scope:
        case AST_List:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                if (assemble(ir, list_get(ast->list.inner, i), variable_map, 0)) list_add(ir) = IR(IR_pop);
            }
            return false;
        case AST_Loop:
            list_add(ir) = IR(IR_if, INT(true));
            if (ast->loop.cond) assemble(ir, ast->loop.cond, variable_map, 0);
            else list_add(ir) = IR(IR_pushi, INT(1), INT(Type_Int32), INT(false));
            list_add(ir) = IR(IR_then);
            assemble(ir, ast->loop.body, variable_map, 0);
            list_add(ir) = IR(IR_goto_start);
            list_add(ir) = IR(IR_else);
            list_add(ir) = IR(IR_end);
            return false;
        case AST_Break:
            list_add(ir) = IR(IR_goto_end);
            return false;
        case AST_Continue:
            list_add(ir) = IR(IR_goto_start);
            return false;
        case AST_Return:
            if (ast->ret.expr) assemble(ir, ast->ret.expr, variable_map, 0);
            else list_add(ir) = IR(IR_pushi, INT(0), INT(Type_Int64), INT(true));
            list_add(ir) = IR(IR_ret);
            return false;
        case AST_Integer:
        case AST_StringLit:
            list_add(ir) = IR(IR_pushi, INT(ast->integer.value), type(ast->exprtype));
            return true;
        case AST_Floating:
            if (ast->floating.is_single_precision) list_add(ir) = IR(IR_pushf, FLT(ast->floating.value));
            else list_add(ir) = IR(IR_pushd, DBL(ast->floating.value));
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
            if (var->is_global) list_add(ir) = IR(IR_laddr, PTR(var->var.ptr), INT(kind), INT(is_unsigned));
            else list_add(ir) = IR(IR_lstack, INT(var->var.offset), INT(kind), INT(is_unsigned));
            if ((var->var.type->kind == Type_Array || var->var.type->kind == Type_Function) && !ast->variable.write_dest)
                list_add(ir) = IR(IR_addrof);
            return true;
        case AST_WalkStruct:
            assemble(ir, ast->walk_struct.struct_ptr, variable_map, 0);
            list_add(ir) = IR(IR_type, type(ast->exprtype));
            list_add(ir) = IR(IR_offset, INT(ast->walk_struct.offset));
            if (ast->exprtype->kind == Type_Array) list_add(ir) = IR(IR_addrof);
            return false;
        case AST_Initializer: {
            assemble(ir, ast->init.store_to, variable_map, 0);
            list_add(ir) = IR(IR_init, INT(ast->exprtype->size), INT(ast->exprtype->alignment));
            size_t curr_offset = 0;
            for (size_t i = 0; i < list_size(ast->init.items); i++) {
                size_t diff = list_get(ast->init.offsets, i) - curr_offset;
                list_add(ir) = IR(IR_offset, INT(diff));
                assemble(ir, list_get(ast->init.items, i), variable_map, 0);
                list_add(ir) = IR(IR_store);
                curr_offset += diff;
            }
            list_add(ir) = IR(IR_offset, INT(-curr_offset));
            if (ast->exprtype->kind == Type_Array) list_add(ir) = IR(IR_addrof);
        } return true;
        case AST_Goto: {
            list_add(ir) = IR(IR_goto, PTR(ast->label.name));
        } return false;
        case AST_Label: {
            list_add(ir) = IR(IR_label, PTR(ast->label.name));
        } return false;
        case AST_Interrupt: {
            list_add(ir) = IR(IR_int);
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
                    if (var->type->is_const || var->type->kind == Type_Function) break;
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
            smartptr(list(jitc_ir_t)) ir = list_new(jitc_ir_t);
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
            list_add(ir) = IR(IR_func, PTR(ast->func.variable), INT(get_stack_size(variable_map, ast->func.body, ast->func.variable)));
            for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
                jitc_ast_t* node = list_get(ast->func.body->list.inner, i);
                if (assemble(ir, node, variable_map, 0)) list_add(ir) = IR(IR_pop);
                is_return = node->node_type == AST_Return;
            }
            if (!is_return) {
                jitc_type_t* ret = ast->func.variable->func.ret;
                if (ret->kind != Type_Void) {
                    list_add(ir) = IR(IR_pushi, INT(0), type(ret));
                    list_add(ir) = IR(IR_ret);
                }
            }
            list_add(ir) = IR(IR_func_end);
            jitc_asm_emit(writer, ir);
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
