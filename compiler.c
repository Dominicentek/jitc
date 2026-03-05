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
                size_t index;
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

static void append_to_var_tree(list_t* _list, map_t* _gotos, jitc_ast_t* node) {
    list(stackvar_t)* list = _list;
    map(char*, int)* gotos = _gotos;
    if (!node) return;
    switch (node->node_type) {
        case AST_Declaration: {
            stackvar_t* var = &list_add(list);
            var->is_leaf = true;
            var->is_global = false;
            var->var.type = node->decl.type;
            if (node->decl.decltype == Decltype_Extern) {
                var->is_global = true;
                var->var.ptr = node->decl.variable;
            }
        } break;
        case AST_List:
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_var_tree(list, gotos, list_get(node->list.inner, i));
            }
            break;
        case AST_Scope: {
            stackvar_t* var = &list_add(list);
            var->is_leaf = false;
            var->is_global = false;
            var->list = list_new(stackvar_t);
            for (size_t i = 0; i < list_size(node->list.inner); i++) {
                append_to_var_tree(var->list, gotos, list_get(node->list.inner, i));
            }
        } break;
        case AST_Ternary:
        case AST_Branch: {
            stackvar_t* var = &list_add(list);
            var->is_leaf = false;
            var->is_global = false;
            var->list = list_new(stackvar_t);
            append_to_var_tree(var->list, gotos, node->ternary.when);
            append_to_var_tree(var->list, gotos, node->ternary.then);
            append_to_var_tree(var->list, gotos, node->ternary.otherwise);
        } break;
        case AST_WhileLoop:
        case AST_DoWhileLoop:
        case AST_ForLoop: {
            stackvar_t* var = &list_add(list);
            var->is_leaf = false;
            var->is_global = false;
            var->list = list_new(stackvar_t);
            append_to_var_tree(var->list, gotos, node->loop.init);
            append_to_var_tree(var->list, gotos, node->loop.cond);
            append_to_var_tree(var->list, gotos, node->loop.iter);
            append_to_var_tree(var->list, gotos, node->loop.body);
        } break;
        case AST_Label: {
            map_add(gotos) = (void*)node->label.name;
            map_commit(gotos);
            map_get_value(gotos) = map_size(gotos);
        } break;
        default: break;
    }
}

static void process_stackvar_tree(map_t* _variable_map, stackvar_t* tree, size_t parent_index) {
    map(char*, stackvar_t)* variable_map = _variable_map;
    size_t curr_index = parent_index;
    for (size_t i = 0; i < list_size(tree->list); i++) {
        stackvar_t* node = &list_get(tree->list, i);
        if (!node->is_leaf) continue;
        if (!node->var.type->name) continue;
        if (!node->is_global) node->var.index = curr_index++;
        map_add(variable_map) = (char*)node->var.type->name;
        map_commit(variable_map);
        map_get_value(variable_map) = *node;
    }
    for (size_t i = 0; i < list_size(tree->list); i++) {
        stackvar_t* node = &list_get(tree->list, i);
        if (node->is_leaf) continue;
        process_stackvar_tree(variable_map, node, curr_index);
    }
    list_delete(tree->list);
}

static void scan_variables(map_t* variable_map, map_t* gotos, jitc_ast_t* ast, jitc_type_t* type) {
    stackvar_t root;
    root.is_leaf = false;
    root.list = list_new(stackvar_t);
    for (size_t i = 0; i < type->func.num_params; i++) {
        if (!type->func.params[i]->name) continue;
        stackvar_t* param = &list_add(root.list);
        param->is_leaf = true;
        param->is_global = false;
        param->var.type = type->func.params[i];
    }
    append_to_var_tree(root.list, gotos, ast);
    process_stackvar_tree(variable_map, &root, 0);
}

#define ir_opcode_t(ITEM) \
    ITEM(IR_nop)    /* nop */ \
    \
    ITEM(IR_load)   /* load from address */ \
    ITEM(IR_store)  /* store to address */ \
    ITEM(IR_addrof) /* address of a variable */ \
    ITEM(IR_mov)    /* move register */ \
    ITEM(IR_add)    /* add */ \
    ITEM(IR_sub)    /* subtract */ \
    ITEM(IR_mul)    /* multiply */ \
    ITEM(IR_div)    /* divide  */ \
    ITEM(IR_mod)    /* modulo */ \
    ITEM(IR_neg)    /* negate */ \
    ITEM(IR_shr)    /* bitshift right */ \
    ITEM(IR_shl)    /* bitshift left */ \
    ITEM(IR_and)    /* bitwise and */ \
    ITEM(IR_or)     /* bitwise or */ \
    ITEM(IR_xor)    /* bitwise xor */ \
    ITEM(IR_not)    /* bitwise not */ \
    ITEM(IR_lnot)   /* logical not */ \
    ITEM(IR_bool)   /* convert to bool */ \
    ITEM(IR_conv)   /* cast */ \
    ITEM(IR_eql)    /* equal */ \
    ITEM(IR_neq)    /* not equal */ \
    ITEM(IR_lst)    /* less than */ \
    ITEM(IR_lte)    /* less than or equal to */ \
    ITEM(IR_grt)    /* greater than */ \
    ITEM(IR_gte)    /* greater than or equal to */ \
    ITEM(IR_sufinc) /* suffix increment */ \
    ITEM(IR_sufdec) /* suffix decrement */ \
    ITEM(IR_preinc) /* prefix increment */ \
    ITEM(IR_predec) /* prefix decrement */ \
    ITEM(IR_label)  /* label */ \
    ITEM(IR_jmp)    /* unconditional jump */ \
    ITEM(IR_br)     /* conditional branch */ \
    ITEM(IR_ret)    /* function return */ \
    ITEM(IR_call)   /* function call */ \
    ITEM(IR_int)    /* interrupt */ \
    ITEM(IR_init)   /* zero-initialize */ \
    \
    ITEM(IR_reg)    /* single register */ \

ENUM(ir_opcode_t)

typedef enum {
    RegType_Void,
    RegType_Variable,
    RegType_Temporary,
    RegType_Argument,
    RegType_Return,
    RegType_Immediate,
    RegType_Global,
} ir_regtype_t;

typedef struct {
    ir_regtype_t kind;
    jitc_type_t* type;
    uint64_t value;
    int64_t offset;
} ir_register_t;

typedef struct {
    ir_opcode_t opcode;
    jitc_type_t* type;
    ir_register_t operands[3];
} ir_t;

typedef struct {
    list(ir_t)* ir;
    map(char*, stackvar_t)* variable_map;
    map(char*, int)* gotos;
    stack(int)* break_stack;
    stack(int)* continue_stack;
    int tmpreg_counter;
    int label_counter;
} ir_state_t;

typedef struct {
    jitc_binary_op_t curr_op;
    int dest_label;
    int dest_reg;
} shortcircuit_state_t;

#define IMM_T(x, t) (ir_t){ IR_reg, (t), { RegType_Immediate, (t), (x) }}
#define VAR(x, t) (ir_t){ IR_reg, (t), { RegType_Variable, (t), (x) }}
#define GBL(x, t) (ir_t){ IR_reg, (t), { RegType_Global, (t), (x) }}
#define IMM(x) IMM_T(x, INTEGER)

static ir_register_t get_register(ir_t instr, jitc_type_t* type, ir_state_t* state) {
    ir_register_t reg = { RegType_Temporary, type, state->tmpreg_counter };
    if (instr.opcode == IR_reg || instr.operands[0].kind != RegType_Void) reg = instr.operands[0];
    else {
        if (instr.operands[1].kind == RegType_Temporary) reg = instr.operands[1];
        else if (instr.operands[2].kind == RegType_Temporary) reg = instr.operands[2];
        else state->tmpreg_counter++;
        instr.operands[0] = reg;
        list_add(state->ir) = instr;
    }
    reg.type = type;
    return reg;
}

static ir_t instruction1(ir_opcode_t opcode, ir_t instr, jitc_type_t* type, ir_state_t* state) {
    return (ir_t){ opcode, type, {{}, get_register(instr, type, state) }};
}

static ir_t instruction2(ir_opcode_t opcode, ir_t instr1, ir_t instr2, jitc_type_t* type, ir_state_t* state) {
    return (ir_t){ opcode, type, {{}, get_register(instr1, type, state), get_register(instr2, type, state) }};
}

static ir_t instruction3(ir_opcode_t opcode, ir_t instr1, ir_t instr2, ir_t instr3, jitc_type_t* type, ir_state_t* state) {
    ir_t instr = (ir_t){ opcode, type, { get_register(instr1, type, state), get_register(instr2, type, state), get_register(instr3, type, state) }};
    list_add(state->ir) = instr;
    return instr;
}

static ir_t store_shift(ir_t instr, ir_state_t* state) {
    instr.operands[0] = instr.operands[1];
    instr.operands[1] = instr.operands[2];
    list_add(state->ir) = instr;
    return instr;
}

static ir_t store_copy(ir_t instr, ir_state_t* state) {
    instr.operands[0] = instr.operands[1];
    list_add(state->ir) = instr;
    return instr;
}

static ir_t emit_ir(ir_t instr, ir_state_t* state) {
    if (instr.opcode == IR_nop || instr.opcode == IR_reg) return instr;
    if (instr.operands[0].kind != RegType_Void) return instr;
    list_add(state->ir) = instr;
    return instr;
}

static ir_t assign(ir_t instr, ir_register_t reg, ir_state_t* state) {
    if (instr.opcode == IR_reg) return store_shift(instruction2(IR_mov, (ir_t){ IR_reg, instr.type, { reg }}, instr, instr.type, state), state);
    instr.operands[0] = reg;
    list_add(state->ir) = instr;
    return instr;
}

static ir_t assemble(jitc_context_t* context, jitc_ast_t* ast, ir_state_t* state, shortcircuit_state_t sc) {
    static jitc_type_t* INTEGER;
    if (!INTEGER) INTEGER = jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int64));

    switch (ast->node_type) {
        case AST_Unary: switch (ast->unary.operation) {
            case Unary_ArithPlus:          return assemble(context, ast->unary.inner, state, (shortcircuit_state_t){});
            case Unary_ArithNegate:        return instruction1(IR_neg, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_LogicNegate:        return instruction1(IR_lnot, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_BinaryNegate:       return instruction1(IR_not, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_AddressOf:          return instruction1(IR_addrof, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_Dereference:        return instruction1(IR_load, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_PrefixIncrement:
            case Unary_PtrPrefixIncrement: return instruction1(IR_preinc, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_PrefixDecrement:
            case Unary_PtrPrefixDecrement: return instruction1(IR_predec, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_SuffixIncrement:
            case Unary_PtrSuffixIncrement: return instruction1(IR_sufinc, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            case Unary_SuffixDecrement:
            case Unary_PtrSuffixDecrement: return instruction1(IR_sufdec, assemble(context, ast->unary.inner, state, (shortcircuit_state_t){}), ast->exprtype, state);
            default: break;
        } break;
        case AST_Binary: {
            if (ast->binary.operation == Binary_Cast) {
                ir_t dest = (ir_t){ IR_reg, ast->exprtype, { RegType_Temporary, ast->exprtype, state->label_counter++ }};
                jitc_type_t* target = ast->binary.right->type.type->kind == Type_Void
                    ? ast->binary.left->exprtype
                    : ast->binary.right->type.type;
                return store_shift(instruction2(IR_conv, dest, assemble(context, ast->binary.left, state, (shortcircuit_state_t){}), target, state), state);
            }
            else if (ast->binary.operation == Binary_FunctionCall) {
                size_t num_args = list_size(ast->binary.right->list.inner);
                jitc_type_t* signature = ast->binary.left->exprtype;
                if (signature->kind == Type_Pointer) signature = signature->ptr.base;
                for (size_t i = 0; i < num_args; i++) {
                    jitc_ast_t* arg = list_get(ast->binary.right->list.inner, i);
                    assign(assemble(context, arg, state, (shortcircuit_state_t){}), (ir_register_t){ RegType_Argument, arg->exprtype, i }, state);
                }
                store_shift(instruction1(IR_call, assemble(context, ast->binary.left, state, (shortcircuit_state_t){}), ast->exprtype, state), state);
                return (ir_t){ IR_reg, ast->exprtype, { RegType_Return, ast->exprtype, 0 }};
            }
            else if (ast->binary.operation == Binary_Comma) {
                emit_ir(assemble(context, ast->binary.left, state, (shortcircuit_state_t){}), state);
                return assemble(context, ast->binary.right, state, (shortcircuit_state_t){});
            }
            else {
                ir_t left, right;
                if (ast->binary.operation != Binary_LogicAnd && ast->binary.operation != Binary_LogicOr) {
                    if (get_su_number(ast->binary.left) < get_su_number(ast->binary.right) && ast->binary.operation < Binary_Assignment) {
                        right = assemble(context, ast->binary.right, state, (shortcircuit_state_t){});
                        left  = assemble(context, ast->binary.left,  state, (shortcircuit_state_t){});
                    }
                    else {
                        left  = assemble(context, ast->binary.left,  state, (shortcircuit_state_t){});
                        right = assemble(context, ast->binary.right, state, (shortcircuit_state_t){});
                    }
                }
                switch (ast->binary.operation) {
                    case Binary_Assignment:
                    case Binary_AssignConst:          return store_shift(instruction2(IR_store, left, right, ast->exprtype, state), state);
                    case Binary_PtrAddition:
                    case Binary_Addition:             return instruction2(IR_add, left, right, ast->exprtype, state);
                    case Binary_PtrSubtraction:
                    case Binary_PtrDiff:
                    case Binary_Subtraction:          return instruction2(IR_sub, left, right, ast->exprtype, state);
                    case Binary_Multiplication:       return instruction2(IR_mul, left, right, ast->exprtype, state);
                    case Binary_Division:             return instruction2(IR_div, left, right, ast->exprtype, state);
                    case Binary_Modulo:               return instruction2(IR_mod, left, right, ast->exprtype, state);
                    case Binary_BitshiftLeft:         return instruction2(IR_shl, left, right, ast->exprtype, state);
                    case Binary_BitshiftRight:        return instruction2(IR_shr, left, right, ast->exprtype, state);
                    case Binary_And:                  return instruction2(IR_and, left, right, ast->exprtype, state);
                    case Binary_Or:                   return instruction2(IR_or,  left, right, ast->exprtype, state);
                    case Binary_Xor:                  return instruction2(IR_xor, left, right, ast->exprtype, state);
                    case Binary_Equals:               return instruction2(IR_eql, left, right, ast->exprtype, state);
                    case Binary_NotEquals:            return instruction2(IR_neq, left, right, ast->exprtype, state);
                    case Binary_LessThan:             return instruction2(IR_lst, left, right, ast->exprtype, state);
                    case Binary_LessThanOrEqualTo:    return instruction2(IR_lte, left, right, ast->exprtype, state);
                    case Binary_GreaterThan:          return instruction2(IR_grt, left, right, ast->exprtype, state);
                    case Binary_GreaterThanOrEqualTo: return instruction2(IR_gte, left, right, ast->exprtype, state);
                    case Binary_AssignPtrAddition:
                    case Binary_AssignAddition:       return store_copy(instruction2(IR_add, left, right, ast->exprtype, state), state);
                    case Binary_AssignPtrSubtraction:
                    case Binary_AssignPtrDiff:
                    case Binary_AssignSubtraction:    return store_copy(instruction2(IR_sub, left, right, ast->exprtype, state), state);
                    case Binary_AssignMultiplication: return store_copy(instruction2(IR_mul, left, right, ast->exprtype, state), state);
                    case Binary_AssignDivision:       return store_copy(instruction2(IR_div, left, right, ast->exprtype, state), state);
                    case Binary_AssignModulo:         return store_copy(instruction2(IR_mod, left, right, ast->exprtype, state), state);
                    case Binary_AssignBitshiftLeft:   return store_copy(instruction2(IR_shl, left, right, ast->exprtype, state), state);
                    case Binary_AssignBitshiftRight:  return store_copy(instruction2(IR_shr, left, right, ast->exprtype, state), state);
                    case Binary_AssignAnd:            return store_copy(instruction2(IR_and, left, right, ast->exprtype, state), state);
                    case Binary_AssignOr:             return store_copy(instruction2(IR_or,  left, right, ast->exprtype, state), state);
                    case Binary_AssignXor:            return store_copy(instruction2(IR_xor, left, right, ast->exprtype, state), state);
                    case Binary_LogicAnd:
                    case Binary_LogicOr: {
                        int prev_label = sc.dest_label;
                        sc.dest_label = sc.curr_op == ast->binary.operation ? sc.dest_label : state->label_counter++;
                        sc.dest_reg = sc.dest_label == prev_label ? sc.dest_reg : state->tmpreg_counter++;
                        sc.curr_op = ast->binary.operation;
                        ir_register_t reg = { RegType_Temporary, ast->exprtype, sc.dest_reg };
                        ir_t instr = assemble(context, ast->binary.left, state, sc);
                        assign(instruction1(IR_bool, instr, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state), reg, state);
                        instruction3(IR_br, (ir_t){ IR_reg, ast->exprtype, reg },
                            ast->binary.operation == Binary_LogicAnd ? IMM(0) : IMM(sc.dest_label),
                            ast->binary.operation == Binary_LogicOr  ? IMM(0) : IMM(sc.dest_label),
                        ast->exprtype, state);
                        instr = assemble(context, ast->binary.right, state, sc);
                        assign(instruction1(IR_bool, instr, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state), reg, state);
                        if (sc.dest_label != prev_label) store_shift(instruction1(IR_label, IMM(sc.dest_label), INTEGER, state), state);
                        return (ir_t){ IR_reg, ast->exprtype, reg };
                    } break;
                    default: break;
                }
            }
        } break;
        case AST_Ternary: {
            ir_t reg = (ir_t){ IR_reg, ast->exprtype, { RegType_Temporary, ast->exprtype, state->tmpreg_counter++ }};
            int tmpreg = state->tmpreg_counter;
            ir_t instr = assemble(context, ast->ternary.when, state, (shortcircuit_state_t){});
            instr = instruction1(IR_bool, instr, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state);
            int otherwise = state->label_counter++; int end = state->label_counter++;
            instruction3(IR_br, instr, IMM(0), IMM(otherwise), INTEGER, state);
            store_shift(instruction2(IR_mov, reg, assemble(context, ast->ternary.then, state, (shortcircuit_state_t){}), ast->exprtype, state), state);
            store_shift(instruction1(IR_jmp, IMM(end), INTEGER, state), state);
            state->tmpreg_counter = tmpreg;
            store_shift(instruction1(IR_label, IMM(otherwise), INTEGER, state), state);
            store_shift(instruction2(IR_mov, reg, assemble(context, ast->ternary.otherwise, state, (shortcircuit_state_t){}), ast->exprtype, state), state);
            state->tmpreg_counter = tmpreg;
            store_shift(instruction1(IR_label, IMM(end), INTEGER, state), state);
            return reg;
        } break;
        case AST_Branch: {
            ir_t instr = assemble(context, ast->ternary.when, state, (shortcircuit_state_t){});
            instr = instruction1(IR_bool, instr, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state);
            int then = 0, otherwise = 0, end = 0;
            if (ast->ternary.then && ast->ternary.otherwise) {
                otherwise = state->label_counter++;
                end = state->label_counter++;
            }
            else if (ast->ternary.then) otherwise = state->label_counter++;
            else if (ast->ternary.otherwise) then = state->label_counter++;
            instruction3(IR_br, instr, IMM(then), IMM(otherwise), INTEGER, state);
            if (ast->ternary.then) {
                emit_ir(assemble(context, ast->ternary.then, state, (shortcircuit_state_t){}), state);
                if (ast->ternary.otherwise) {
                    store_shift(instruction1(IR_jmp, IMM(end), INTEGER, state), state);
                    store_shift(instruction1(IR_label, IMM(otherwise), INTEGER, state), state);
                }
            }
            if (ast->ternary.otherwise)
                emit_ir(assemble(context, ast->ternary.otherwise, state, (shortcircuit_state_t){}), state);
            store_shift(instruction1(IR_label, IMM(ast->ternary.then && ast->ternary.otherwise ? end : ast->ternary.then ? otherwise : then), INTEGER, state), state);
        } break;
        case AST_List:
        case AST_Scope:
            for (size_t i = 0; i < list_size(ast->list.inner); i++) {
                emit_ir(assemble(context, list_get(ast->list.inner, i), state, (shortcircuit_state_t){}), state);
                state->tmpreg_counter = 0;
            }
            break;
        case AST_WhileLoop: {
            int label_break = stack_push(state->break_stack) = state->label_counter++;
            int label_continue = stack_push(state->continue_stack) = state->label_counter++;
            store_shift(instruction1(IR_label, IMM(label_continue), INTEGER, state), state);
            ir_t cond = assemble(context, ast->loop.cond, state, (shortcircuit_state_t){});
            cond = instruction1(IR_bool, cond, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state);
            instruction3(IR_br, cond, IMM(0), IMM(label_break), INTEGER, state);
            emit_ir(assemble(context, ast->loop.body, state, (shortcircuit_state_t){}), state);
            store_shift(instruction1(IR_label, IMM(label_break), INTEGER, state), state);
            stack_pop(state->break_stack); stack_pop(state->continue_stack);
        } break;
        case AST_DoWhileLoop: {
            int label_break = stack_push(state->break_stack) = state->label_counter++;
            int label_continue = stack_push(state->continue_stack) = state->label_counter++;
            store_shift(instruction1(IR_label, IMM(label_continue), INTEGER, state), state);
            emit_ir(assemble(context, ast->loop.body, state, (shortcircuit_state_t){}), state);
            ir_t cond = assemble(context, ast->loop.cond, state, (shortcircuit_state_t){});
            cond = instruction1(IR_bool, cond, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state);
            instruction3(IR_br, cond, IMM(label_continue), IMM(0), INTEGER, state);
            store_shift(instruction1(IR_label, IMM(label_break), INTEGER, state), state);
            stack_pop(state->break_stack); stack_pop(state->continue_stack);
        } break;
        case AST_ForLoop: {
            int label_start = state->label_counter++;
            int label_break = stack_push(state->break_stack) = state->label_counter++;
            int label_continue = stack_push(state->continue_stack) = state->label_counter++;
            emit_ir(assemble(context, ast->loop.init, state, (shortcircuit_state_t){}), state);
            store_shift(instruction1(IR_label, IMM(label_start), INTEGER, state), state);
            ir_t cond = assemble(context, ast->loop.cond, state, (shortcircuit_state_t){});
            cond = instruction1(IR_bool, cond, jitc_typecache_unsigned(context, jitc_typecache_primitive(context, Type_Int8)), state);
            instruction3(IR_br, cond, IMM(label_break), IMM(0), INTEGER, state);
            emit_ir(assemble(context, ast->loop.body, state, (shortcircuit_state_t){}), state);
            store_shift(instruction1(IR_label, IMM(label_continue), INTEGER, state), state);
            emit_ir(assemble(context, ast->loop.iter, state, (shortcircuit_state_t){}), state);
            store_shift(instruction1(IR_jmp, IMM(label_start), INTEGER, state), state);
            store_shift(instruction1(IR_label, IMM(label_break), INTEGER, state), state);
            stack_pop(state->break_stack); stack_pop(state->continue_stack);
        } break;
        case AST_Break:
            return store_shift(instruction1(IR_jmp, IMM(stack_peek(state->break_stack)), INTEGER, state), state);
        case AST_Continue:
            return store_shift(instruction1(IR_jmp, IMM(stack_peek(state->continue_stack)), INTEGER, state), state);
        case AST_Return:
            assign(assemble(context, ast->ret.expr, state, (shortcircuit_state_t){}), (ir_register_t){ RegType_Return, ast->ret.expr->exprtype, 0 }, state);
            return (ir_t){ IR_ret };
        case AST_Integer:
        case AST_StringLit:
            return IMM_T(ast->integer.value, ast->exprtype);
        case AST_Floating:
            if (ast->exprtype->kind == Type_Float32) return IMM_T(*(uint32_t*)&ast->floating.value, ast->exprtype);
            if (ast->exprtype->kind == Type_Float64) return IMM_T(*(uint64_t*)&ast->floating.value, ast->exprtype);
            break;
        case AST_Variable: {
            map_find(state->variable_map, &ast->variable.name);
            stackvar_t* var = &map_get_value(state->variable_map);
            if (var->is_global) return GBL((uint64_t)var->var.ptr, ast->exprtype);
            else return VAR(var->var.index, ast->exprtype);
        } break;
        case AST_WalkStruct: {
            ir_t ir = emit_ir(assemble(context, ast->walk_struct.struct_ptr, state, (shortcircuit_state_t){}), state);
            ir_register_t reg = ir.operands[0];
            reg.offset += ast->walk_struct.offset;
            return (ir_t){ IR_reg, ast->exprtype, { reg }};
        } break;
        case AST_Initializer: {
            ir_t instr = store_shift(instruction1(IR_init, assemble(context, ast->init.store_to, state, (shortcircuit_state_t){}), ast->init.type, state), state);
            ir_register_t reg = instr.operands[0];
            uint64_t base_offset = reg.offset;
            for (size_t i = 0; i < list_size(ast->init.items); i++) {
                reg.offset = base_offset + list_get(ast->init.offsets, i);
                assign(assemble(context, list_get(ast->init.items, i), state, (shortcircuit_state_t){}), reg, state);
            }
            if (ast->exprtype->kind == Type_Array) return instruction1(IR_addrof, instr, ast->exprtype, state);
            else return instr;
        } break;
        case AST_Goto: {
            map_find(state->gotos, &ast->label.name);
            int id = map_get_value(state->gotos);
            return store_shift(instruction1(IR_jmp, IMM(id), INTEGER, state), state);
        } break;
        case AST_Label: {
            map_find(state->gotos, &ast->label.name);
            int id = map_get_value(state->gotos);
            return store_shift(instruction1(IR_label, IMM(id), INTEGER, state), state);
        } break;
        case AST_Interrupt:
            return (ir_t){ IR_int };
        default: break;
    }
    return (ir_t){};
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
            smartptr(map(char*, int)) gotos = map_new(compare_string, char*, int);
            smartptr(stack(int)) break_stack = stack_new(int);
            smartptr(stack(int)) continue_stack = stack_new(int);
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
            scan_variables(variable_map, gotos, ast->func.body, ast->func.variable);
            /*jitc_asm_func(writer, ast->func.variable, get_stack_size(variable_map, ast->func.body, ast->func.variable));
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
            }*/
            
            ir_state_t state;
            state.ir = list_new(ir_t);
            state.tmpreg_counter = 0;
            state.label_counter = 1;
            state.variable_map = (void*)variable_map;
            state.gotos = (void*)gotos;
            state.break_stack = (void*)break_stack;
            state.continue_stack = (void*)continue_stack;
            for (size_t i = 0; i < list_size(ast->func.body->list.inner); i++) {
                jitc_ast_t* node = list_get(ast->func.body->list.inner, i);
                emit_ir(assemble(context, node, &state, (shortcircuit_state_t){}), &state);
                state.tmpreg_counter = 0;
            }
            
            for (size_t i = 0; i < list_size(state.ir); i++) {
                ir_t* ir = &list_get(state.ir, i);
                printf("%s", ir_opcode_t_names[ir->opcode] + 3);
                for (int j = 0; j < (int[]){
                    [IR_nop]    = 0,
                    [IR_load]   = 2,
                    [IR_store]  = 2,
                    [IR_addrof] = 2,
                    [IR_mov]    = 2,
                    [IR_add]    = 3,
                    [IR_sub]    = 3,
                    [IR_mul]    = 3,
                    [IR_div]    = 3,
                    [IR_mod]    = 3,
                    [IR_neg]    = 2,
                    [IR_shr]    = 3,
                    [IR_shl]    = 3,
                    [IR_and]    = 3,
                    [IR_or]     = 3,
                    [IR_xor]    = 3,
                    [IR_not]    = 2,
                    [IR_lnot]   = 2,
                    [IR_bool]   = 2,
                    [IR_conv]   = 2,
                    [IR_eql]    = 3,
                    [IR_neq]    = 3,
                    [IR_lst]    = 3,
                    [IR_lte]    = 3,
                    [IR_grt]    = 3,
                    [IR_gte]    = 3,
                    [IR_sufinc] = 2,
                    [IR_sufdec] = 2,
                    [IR_preinc] = 2,
                    [IR_predec] = 2,
                    [IR_label]  = 1,
                    [IR_jmp]    = 1,
                    [IR_br]     = 3,
                    [IR_ret]    = 0,
                    [IR_call]   = 1,
                    [IR_int]    = 0,
                    [IR_init]   = 1,
                    [IR_reg]    = 0,
                }[ir->opcode]; j++) {
                    printf(" ");
                    switch (ir->operands[j].kind) {
                        case RegType_Void: printf("void"); break;
                        case RegType_Variable: printf("v%lu", ir->operands[j].value); break;
                        case RegType_Temporary: printf("t%lu", ir->operands[j].value); break;
                        case RegType_Argument: printf("a%lu", ir->operands[j].value); break;
                        case RegType_Return: printf("ret"); break;
                        case RegType_Global: printf("%%0x%lx", ir->operands[j].value); break;
                        case RegType_Immediate: printf("#%lu", ir->operands[j].value); break;
                        default: printf("invalid:%d", ir->operands[j].kind);
                    }
                    if (ir->operands[j].offset > 0) printf("+%ld", ir->operands[j].offset);
                    if (ir->operands[j].offset < 0) printf( "%ld", ir->operands[j].offset);
                }
                printf("\n");
            }
            
            list_delete(state.ir);
        } break;
        default: break;
    }
    jitc_link(context);
}
