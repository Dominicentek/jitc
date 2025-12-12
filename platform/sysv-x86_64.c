#include "../jitc_internal.h"

#include <stdlib.h>

typedef enum: uint8_t {
    rax, rcx, rdx, rbx,
    rsp, rbp, rsi, rdi,
    r8,  r9,  r10, r11,
    r12, r13, r14, r15,

    xmm0=0,xmm1,  xmm2,  xmm3,
    xmm4,  xmm5,  xmm6,  xmm7,
    xmm8,  xmm9,  xmm10, xmm11,
    xmm12, xmm13, xmm14, xmm15,

    xmm_mask = (1 << 4)
} reg_t;

static reg_t stack_regs[] = { rbx, r12, r13, r14, r15, r10, r11 };
static reg_t stack_xmms[] = { xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14 };
static const char* reg_names[][16] = {
    [Type_Int8]    = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" },
    [Type_Int16]   = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" },
    [Type_Int32]   = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" },
    [Type_Int64]   = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" },
    [Type_Float32] = { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" },
    [Type_Float64] = { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" },
    [Type_Pointer] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" },
};

typedef enum: uint8_t {
    StackItem_literal,
    StackItem_rvalue,
    StackItem_lvalue,
    StackItem_lvalue_abs,
} stack_item_type_t;

typedef struct {
    stack_item_type_t type;
    jitc_type_kind_t kind;
    bool is_unsigned;
    union {
        uint64_t as_int;
        double as_float;
        void* as_ptr;
    } value;
} stack_item_t;

typedef struct {
    enum: uint8_t {
        OpType_imm,
        OpType_reg,
        OpType_ptr,
        OpType_ptrptr,
    } type;
    jitc_type_kind_t kind;
    bool is_unsigned;
    reg_t reg;
    uint64_t value;
} operand_t;

static int opstack_capacity = 0, opstack_size = 0;
static stack_item_t* opstack = NULL;

static int opstack_int_index = 0, opstack_float_index = 0, opstack_stack_index = 0;

static stack_item_t* push(stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned) {
    if (opstack_capacity == opstack_size) {
        if (opstack_capacity == 0) opstack_capacity = 4;
        else opstack_capacity *= 2;
        opstack = realloc(opstack, opstack_capacity * sizeof(stack_item_t));
    }
    stack_item_t* item = &opstack[opstack_size++];
    item->type = type;
    item->kind = kind;
    item->is_unsigned = is_unsigned;
    if (type == StackItem_rvalue || type == StackItem_lvalue_abs) {
        int* index = &opstack_int_index;
        if (type == StackItem_rvalue && (item->kind == Type_Float32 || item->kind == Type_Float64)) index = &opstack_float_index;
        if (*index == sizeof(stack_regs)) index = &opstack_stack_index;
        item->value.as_int = (*index)++ + (index == &opstack_stack_index ? sizeof(stack_regs) : 0);
        if (index == &opstack_stack_index) printf("sub rsp, 8\n");
    }
    return item;
}

static stack_item_t* pushi(stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned, uint64_t value) {
    stack_item_t* item = push(type, kind, is_unsigned);
    item->value.as_int = value;
    return item;
}

static stack_item_t* pushf(stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned, double value) {
    stack_item_t* item = push(type, kind, is_unsigned);
    item->value.as_float = value;
    return item;
}

static stack_item_t* peek(int offset) {
    return &opstack[opstack_size - 1 - offset];
}

static stack_item_t pop() {
    stack_item_t* item = peek(0);
    if (item->type == StackItem_rvalue || item->type == StackItem_lvalue_abs) {
        if (item->value.as_int >= sizeof(stack_regs)) {
            opstack_stack_index--;
            printf("add rsp, 8\n");
        }
        else if (item->kind == Type_Float32 || item->kind == Type_Float64) opstack_float_index--;
        else opstack_int_index--;
    }
    opstack_size--;
    return *item;
}

static operand_t reg(reg_t reg, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_reg, .kind = kind, .is_unsigned = is_unsigned, .reg = reg };
}

static operand_t op(stack_item_t* item) {
    switch (item->type) {
        case StackItem_literal: return (operand_t){
            .type = OpType_imm,
            .kind = item->kind,
            .is_unsigned = item->is_unsigned,
            .value = item->value.as_int
        };
        case StackItem_lvalue: return (operand_t){
            .type = OpType_ptr,
            .kind = item->kind,
            .is_unsigned = item->is_unsigned,
            .reg = rbp,
            .value = -item->value.as_int
        };
        case StackItem_lvalue_abs: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value.as_int >= sizeof(stack_regs)) {
                op.type = OpType_ptrptr;
                op.reg = rsp;
                op.value = (opstack_stack_index - item->value.as_int + sizeof(stack_regs)) * 8;
            }
            else {
                op.type = OpType_ptr;
                op.reg = stack_regs[item->value.as_int];
                op.value = 0;
            }
            return op;
        }
        case StackItem_rvalue: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value.as_int >= sizeof(stack_regs)) {
                op.type = OpType_ptr;
                op.reg = rsp;
                op.value = (opstack_stack_index - item->value.as_int + sizeof(stack_regs)) * 8;
            }
            else {
                op.type = OpType_reg;
                op.reg = (item->kind == Type_Float32 || item->kind == Type_Float64 ? stack_xmms : stack_regs)[item->value.as_int];
            }
            return op;
        }
    }
    return (operand_t){};
}

static void print_mem(reg_t reg, int32_t disp) {
    printf("[%s", reg_names[Type_Int64][reg]);
    if (disp < 0) printf("%d", disp);
    if (disp > 0) printf("+%d", disp);
    printf("]");
}

static void instr2(const char* opcode, operand_t op1, operand_t op2) {
    bool use_rcx = false, rcxptr = false;
    if (op2.type == OpType_ptr && (op1.type == OpType_ptr || op1.type == OpType_ptrptr)) {
        use_rcx = true;
        printf("mov %s, ", reg_names[op2.kind][rcx]);
        print_mem(op2.reg, op2.value);
        printf("\n");
    }
    else if (op2.type == OpType_ptrptr) {
        use_rcx = true;
        printf("mov %s, ", reg_names[Type_Int64][rcx]);
        print_mem(op2.reg, op2.value);
        printf("\n");
        if (op1.type == OpType_ptr || op1.type == OpType_ptrptr)
            printf("mov %s, [%s]\n", reg_names[op2.kind][rcx], reg_names[Type_Int64][rcx]);
        else rcxptr = true;
    }

    if (op1.type == OpType_ptrptr) {
        printf("mov %s, ", reg_names[Type_Int64][rax]);
        print_mem(op1.reg, op1.value);
        printf("\n%s ", opcode);
        if (op2.type == OpType_imm) printf("%s ptr ", (const char*[]) {
            "byte", "word", "dword", "qword", "", "", "qword"
        }[op2.kind]);
        printf("[%s]", reg_names[Type_Int64][rax]);
    }
    else if (op1.type == OpType_ptr) {
        printf("%s ", opcode);
        if (op2.type == OpType_imm) printf("%s ptr ", (const char*[]) {
            "byte", "word", "dword", "qword", "", "", "qword"
        }[op2.kind]);
        print_mem(op1.reg, op1.value);
    }
    else printf("%s %s", opcode, reg_names[op1.kind][op1.reg]);
    printf(", ");

    if (use_rcx) printf(rcxptr ? "[%s]" : "%s", reg_names[op2.kind][rcx]);
    else if (op2.type == OpType_imm) printf("0x%lx", op2.value);
    else if (op2.type == OpType_reg) printf("%s", reg_names[op2.kind][op2.reg]);
    else if (op2.type == OpType_ptr) print_mem(op2.reg, op2.value);
    printf("\n");
}

static void instr1(const char* opcode, operand_t op) {
    if (op.type == OpType_ptrptr) {
        printf("mov %s, ", reg_names[Type_Int64][rax]);
        print_mem(op.reg, op.value);
        printf("\n%s%c [%s]\n", opcode, "bwdq"[op.kind], reg_names[Type_Int64][rax]);
    }
    else if (op.type == OpType_ptr) {
        printf("\n%s%c ", opcode, "bwdq"[op.kind]);
        print_mem(op.reg, op.value);
        printf("\n");
    }
    else if (op.type == OpType_reg) printf("%s %s\n", opcode, reg_names[op.kind][op.reg]);
}

static void binaryop(const char* opcode) {
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    instr2("mov",  op(res), op(&op1));
    instr2(opcode, op(res), op(&op2));
}

static void unaryop(const char* opcode) {
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    instr2("mov",  op(res), op(&op1));
    instr1(opcode, op(res));
}

static void load(jitc_type_kind_t kind, bool is_unsigned) {
    stack_item_t addr = pop();
    stack_item_t* res = push(StackItem_rvalue, Type_Pointer, true);
    instr2("mov", op(res), op(&addr));
    res->kind = kind;
    res->is_unsigned = is_unsigned;
    res->type = StackItem_lvalue_abs;
}

static void divide(reg_t outreg) {

}

static void bitshift(bool is_right) {

}

static void* jitc_assemble(list_t* list) {
    stack_t* stack = stack_new();
    for (size_t i = 0; i < list_size(list); i++) {
        jitc_ir_t* ir = list_get_ptr(list, i);
        switch (ir->opcode) {
            case IROpCode_pushi:
                pushi(StackItem_literal, ir->params[1].as_integer, ir->params[2].as_integer, ir->params[0].as_integer);
                break;
            case IROpCode_pushf:
            case IROpCode_pushd:
                pushf(StackItem_literal, ir->opcode == IROpCode_pushf ? Type_Float32 : Type_Float64, false, ir->params[0].as_float);
                break;
            case IROpCode_lstack:
                pushi(StackItem_lvalue, ir->params[1].as_integer, ir->params[2].as_integer, ir->params[0].as_integer);
                break;
            case IROpCode_pop: pop(); break;
            case IROpCode_load: load(ir->params[0].as_integer, ir->params[1].as_integer); break;
            case IROpCode_store: instr2("mov", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_add: binaryop("add"); break;
            case IROpCode_sub: binaryop("sub"); break;
            case IROpCode_mul: binaryop("imul"); break;
            case IROpCode_div: divide(rax); break;
            case IROpCode_mod: divide(rdx); break;
            case IROpCode_and: binaryop("and"); break;
            case IROpCode_or:  binaryop("or");  break;
            case IROpCode_xor: binaryop("xor"); break;
            case IROpCode_shl: bitshift(false); break;
            case IROpCode_shr: bitshift(true); break;
            case IROpCode_not: unaryop("not"); break;
            case IROpCode_neg: unaryop("neg"); break;
            case IROpCode_inc: unaryop("inc"); break;
            case IROpCode_dec: unaryop("dec"); break;
            case IROpCode_addrof: break;
            case IROpCode_zero: break;
            case IROpCode_eql: break;
            case IROpCode_neq: break;
            case IROpCode_lst: break;
            case IROpCode_lte: break;
            case IROpCode_grt: break;
            case IROpCode_gte: break;
            case IROpCode_swp: break;
            case IROpCode_cvt: break;
            case IROpCode_if: break;
            case IROpCode_then: break;
            case IROpCode_else: break;
            case IROpCode_end: break;
            case IROpCode_goto_start: break;
            case IROpCode_goto_end: break;
            case IROpCode_call: break;
            case IROpCode_ret: break;
            case IROpCode_func: break;
            case IROpCode_func_end: break;
        }
        free(ir);
    }
    return NULL;
}
