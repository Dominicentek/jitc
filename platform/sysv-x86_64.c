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
static const char* ptr_prefixes[] = { "byte", "word", "dword", "qword", "", "", "qword" };

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
    uint64_t value;
    uint32_t extra_storage;
    int32_t offset;
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
        item->value = (*index)++ + (index == &opstack_stack_index ? sizeof(stack_regs) : 0);
        if (index == &opstack_stack_index) printf("sub rsp, 8\n");
    }
    return item;
}

static stack_item_t* pushi(stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned, uint64_t value) {
    stack_item_t* item = push(type, kind, is_unsigned);
    item->value = value;
    return item;
}

static stack_item_t* pushf(stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned, double value) {
    return pushi(type, kind, is_unsigned, *(uint64_t*)&value);
}

static stack_item_t* stackalloc(uint32_t bytes) {
    stack_item_t* item = push(StackItem_lvalue, Type_Pointer, true);
    item->extra_storage = bytes;
    printf("sub rsp, %u\n", bytes);
    return item;
}

static stack_item_t* peek(int offset) {
    return &opstack[opstack_size - 1 - offset];
}

static stack_item_t pop() {
    stack_item_t* item = peek(0);
    if (item->type == StackItem_rvalue || item->type == StackItem_lvalue_abs) {
        if (item->value >= sizeof(stack_regs)) {
            opstack_stack_index--;
            printf("add rsp, 8\n");
        }
        else if (item->kind == Type_Float32 || item->kind == Type_Float64) opstack_float_index--;
        else opstack_int_index--;
        if (item->extra_storage != 0) printf("add rsp, %u\n", item->extra_storage);
    }
    opstack_size--;
    return *item;
}

static operand_t reg(reg_t reg, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_reg, .kind = kind, .is_unsigned = is_unsigned, .reg = reg };
}

static operand_t imm(uint64_t value, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_imm, .kind = kind, .is_unsigned = is_unsigned, .value = value };
}

static operand_t op(stack_item_t* item) {
    switch (item->type) {
        case StackItem_literal: return (operand_t){
            .type = OpType_imm,
            .kind = item->kind,
            .is_unsigned = item->is_unsigned,
            .value = item->value
        };
        case StackItem_lvalue: return (operand_t){
            .type = OpType_ptr,
            .kind = item->kind,
            .is_unsigned = item->is_unsigned,
            .reg = rbp,
            .value = -item->value - (int[]){ 1, 2, 4, 8, 4, 8, 8 }[item->kind] + item->offset
        };
        case StackItem_lvalue_abs: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value >= sizeof(stack_regs)) {
                op.type = OpType_ptrptr;
                op.reg = rsp;
                op.value = (opstack_stack_index - item->value + sizeof(stack_regs)) * 8 + item->offset;
            }
            else {
                op.type = OpType_ptr;
                op.reg = stack_regs[item->value];
                op.value = item->offset;
            }
            return op;
        }
        case StackItem_rvalue: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value >= sizeof(stack_regs)) {
                op.type = OpType_ptr;
                op.reg = rsp;
                op.value = (opstack_stack_index - item->value + sizeof(stack_regs)) * 8;
            }
            else {
                op.type = OpType_reg;
                op.reg = (item->kind == Type_Float32 || item->kind == Type_Float64 ? stack_xmms : stack_regs)[item->value];
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

static void instr1(const char* opcode, operand_t op, bool bitshift) {
    if (op.type == OpType_ptrptr) {
        printf("mov %s, ", reg_names[Type_Int64][rax]);
        print_mem(op.reg, op.value);
        printf("\n%s %s ptr [%s]", opcode, ptr_prefixes[op.kind], reg_names[Type_Int64][rax]);
    }
    else if (op.type == OpType_ptr) {
        printf("%s %s ptr ", opcode, ptr_prefixes[op.kind]);
        print_mem(op.reg, op.value);
    }
    else if (op.type == OpType_reg) printf("%s %s", opcode, reg_names[op.kind][op.reg]);
    if (bitshift) printf(", cl\n");
    else printf("\n");
}

static void binaryop(const char* opcode) {
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    instr2("mov",  op(res), op(&op1));
    instr2(opcode, op(res), op(&op2));
}

static void unaryop(const char* opcode, bool flip) {
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    if (flip) instr2("mov",  op(res), op(&op1));
    instr1(opcode, op(&op1), false);
    if (!flip) instr2("mov",  op(res), op(&op1));
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
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    instr2("mov", reg(rax, op1.kind, op1.is_unsigned), op(&op1));
    if (op1.kind == Type_Int8) {
        if (op1.is_unsigned) printf("mov ah, 0x0\n");
        else printf("cbw\n");
    }
    else {
        if (op1.is_unsigned) printf("mov %s, 0x0\n", reg_names[op1.kind][rdx]);
        else printf("%s\n", (const char*[]){ "", "cwd", "cdq", "cqo", "", "", "cqo" }[op1.kind]);
    }
    instr1("idiv", op(&op2), false);
    instr2("mov", op(res), reg(outreg, op1.kind, op1.is_unsigned));
}

static void bitshift(bool is_right) {
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    instr2("mov", op(res), op(&op1));
    instr2("mov", reg(rcx, op2.kind, op2.is_unsigned), op(&op2));
    instr1(is_right ? "shr" : "shl", op(res), true);
}

static void compare(const char* opcode) {
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    operand_t res = op(push(StackItem_rvalue, Type_Int8, true));
    instr2("cmp", op(&op1), op(&op2));
    instr1(opcode, res, false);
}

static void compare_against(const char* opcode, operand_t op2) {
    stack_item_t op1 = pop();
    operand_t res = op(push(StackItem_rvalue, Type_Int8, true));
    instr2("cmp", op(&op1), op2);
    instr1(opcode, res, false);
}

static void addrof() {
    stack_item_t item = pop();
    operand_t op1 = op(&item);
    operand_t res = op(push(StackItem_rvalue, Type_Pointer, true));
    op1.kind = Type_Pointer;
    op1.is_unsigned = true;
    if (op1.type == OpType_ptr) {
        op1.type = OpType_reg;
        instr2("mov", res, op1);
        int32_t offset = op1.value;
        if (offset < 0) instr2("sub", res, imm(-offset, Type_Int32, true));
        if (offset > 0) instr2("add", res, imm( offset, Type_Int32, true));
    }
    if (op1.type == OpType_ptrptr) {
        op1.type = OpType_ptr;
        instr2("mov", res, op1);
    }
}

static void convert(jitc_type_kind_t kind, bool is_unsigned) {
    stack_item_t item = pop();
    operand_t op1 = op(&item);
    operand_t res = op(push(StackItem_rvalue, kind, is_unsigned));
    if (op1.kind == Type_Pointer) op1.kind = Type_Int64;
    if (res.kind == Type_Pointer) res.kind = Type_Int64;
    if (op1.kind > res.kind) {
        op1.kind = res.kind;
        instr2("mov", res, op1);
    }
    if (op1.kind < res.kind) instr2(op1.is_unsigned ? "movzx" : "movsx", res, op1);
}

static void swap() {
    stack_item_t tmp = *peek(0);
    *peek(0) = *peek(1);
    *peek(1) = tmp;
}

static void offset(int32_t off) {
    peek(0)->offset += off;
}

static int branch_id = 0;

static void push_branch() {
    printf("_L%d:\n", branch_id * 3 + 0);
    branch_id++;
}

static void branch_then() {
    stack_item_t item = pop();
    instr2("cmp", op(&item), op(&item));
    printf("jz _L%d\n", (branch_id - 1) * 3 + 1);
}

static void branch_else() {
    printf("jmp _L%d\n", (branch_id - 1) * 3 + 2);
    printf("_L%d:\n", (branch_id - 1) * 3 + 1);
}

static void pop_branch() {
    branch_id--;
    printf("_L%d:\n", branch_id * 3 + 2);
}

static void goto_start() {
    printf("jmp _L%d\n", (branch_id - 1) * 3 + 0);
}

static void goto_end() {
    printf("jmp _L%d\n", (branch_id - 1) * 3 + 2);
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
            case IROpCode_not: unaryop("not", false); break;
            case IROpCode_neg: unaryop("neg", false); break;
            case IROpCode_inc: unaryop("inc", ir->params[0].as_integer); break;
            case IROpCode_dec: unaryop("dec", ir->params[0].as_integer); break;
            case IROpCode_addrof: addrof(); break;
            case IROpCode_zero: compare_against("sete", imm(0, peek(0)->kind, peek(0)->is_unsigned)); break;
            case IROpCode_eql: compare("sete"); break;
            case IROpCode_neq: compare("setne"); break;
            case IROpCode_lst: compare("setl"); break;
            case IROpCode_lte: compare("setle"); break;
            case IROpCode_grt: compare("setg"); break;
            case IROpCode_gte: compare("setge"); break;
            case IROpCode_swp: swap(); break;
            case IROpCode_cvt: convert(ir->params[0].as_integer, ir->params[1].as_integer); break;
            case IROpCode_offset: offset(ir->params[0].as_integer); break;
            case IROpCode_stackalloc: stackalloc(ir->params[0].as_integer); break;
            case IROpCode_if: push_branch(); break; // todo: remove redundant branching
            case IROpCode_then: branch_then(); break;
            case IROpCode_else: branch_else(); break;
            case IROpCode_end: pop_branch(); break;
            case IROpCode_goto_start: goto_start(); break;
            case IROpCode_goto_end: goto_end(); break;
            case IROpCode_call: break;
            case IROpCode_ret: break;
            case IROpCode_func: break;
            case IROpCode_func_end: break;
        }
        free(ir);
    }
    return NULL;
}
