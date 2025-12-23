#include "../jitc_internal.h"

#include <stdlib.h>
#include <string.h>

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
static const char* ptr_prefixes[] = { "byte", "word", "dword", "qword", "dword", "qword", "qword" };
static const char* opcode_suffix[][7] = {
    { "", "", "", "", "d", "q", "" },
    { "", "", "d", "q", "ss", "sd", "q" },
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

static int opstack_int_index = 0, opstack_float_index = 0;
static size_t stack_bytes = 0;

static bool isflt(jitc_type_kind_t kind) {
    return kind == Type_Float32 || kind == Type_Float64;
}

static void correct_kind(jitc_type_kind_t* kind, bool* is_unsigned) {
    if (*kind > Type_Pointer) *kind = Type_Pointer;
    if (*kind == Type_Pointer) *is_unsigned = true;
    if (isflt(*kind)) *is_unsigned = false;
}

static void stack_sub(size_t size) {
    printf("sub rsp, 0x%lx\n", size);
    stack_bytes += size;
}

static void stack_free(size_t size) {
    printf("add rsp, 0x%lx\n", size);
    stack_bytes -= size;
}

static stack_item_t* push(stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned) {
    if (kind == Type_Void) return NULL;
    if (opstack_capacity == opstack_size) {
        if (opstack_capacity == 0) opstack_capacity = 4;
        else opstack_capacity *= 2;
        opstack = realloc(opstack, opstack_capacity * sizeof(stack_item_t));
    }
    stack_item_t* item = &opstack[opstack_size++];
    correct_kind(&kind, &is_unsigned);
    item->type = type;
    item->kind = kind;
    item->is_unsigned = is_unsigned;
    item->value = 0;
    item->offset = 0;
    item->extra_storage = 0;
    int* index = &opstack_int_index;
    if (type == StackItem_rvalue && isflt(item->kind)) index = &opstack_float_index;
    if (type == StackItem_rvalue || type == StackItem_lvalue_abs) {
        if (*index < sizeof(stack_regs)) item->value = *index | (1L << 63);
        else {
            stack_sub(8);
            item->value = stack_bytes;
        }
    }
    (*index)++;
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

static stack_item_t* peek(int offset) {
    return &opstack[opstack_size - 1 - offset];
}

static stack_item_t pop() {
    stack_item_t* item = peek(0);
    if (isflt(item->kind)) opstack_float_index--;
    else opstack_int_index--;
    if (item->type == StackItem_rvalue || item->type == StackItem_lvalue_abs) {
        if (!(item->value & (1L << 63))) stack_free(8);
        if (item->extra_storage != 0) stack_free(item->extra_storage);
    }
    opstack_size--;
    return *item;
}

static operand_t reg(reg_t reg, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_reg, .kind = kind, .is_unsigned = is_unsigned, .reg = reg };
}

static operand_t ptr(reg_t reg, int32_t offset, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_ptr, .kind = kind, .is_unsigned = is_unsigned, .reg = reg, .value = offset };
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
            .value = -item->value + item->offset
        };
        case StackItem_lvalue_abs: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value & (1L << 63)) {
                op.type = OpType_ptr;
                op.reg = (isflt(item->kind) ? stack_xmms : stack_regs)[item->value & ~(1L << 63)];
                op.value = item->offset;
            }
            else {
                op.type = OpType_ptrptr;
                op.reg = rsp;
                op.value = stack_bytes - item->value + item->offset;
            }
            return op;
        }
        case StackItem_rvalue: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value & (1L << 63)) {
                op.type = OpType_reg;
                op.reg = (isflt(item->kind) ? stack_xmms : stack_regs)[item->value & ~(1L << 63)];
            }
            else {
                op.type = OpType_ptr;
                op.reg = rsp;
                op.value = stack_bytes - item->value;
            }
            return op;
        }
    }
    return (operand_t){};
}

static operand_t unptr(operand_t op1) {
    if (op1.type == OpType_ptr) {
        op1.type = OpType_reg;
        op1.kind = Type_Pointer;
        op1.value = 0;
    }
    else if (op1.type == OpType_ptrptr) op1.type = OpType_ptr;
    return op1;
}

static void print_mem(reg_t reg, int32_t disp) {
    printf("[%s", reg_names[Type_Int64][reg]);
    if (disp < 0) printf("%d", disp);
    if (disp > 0) printf("+%d", disp);
    printf("]");
}

static void instr2(const char* opcode, operand_t op1, operand_t op2) {
    // god fucking damnit SSE...
    bool use_tmp = false, tmpptr = false, store_float = false;
    reg_t tmpreg = isflt(op2.kind) ? xmm15 : rax;
    if (op2.type == OpType_ptr && (op1.type == OpType_ptr || op1.type == OpType_ptrptr)) {
        use_tmp = true;
        printf("mov%s %s, ", opcode_suffix[isflt(op2.kind)][op2.kind], reg_names[op2.kind][tmpreg]);
        print_mem(op2.reg, op2.value);
        printf("\n");
    }
    else if (op2.type == OpType_ptrptr) {
        use_tmp = true;
        printf("mov %s, ", reg_names[Type_Int64][rax]);
        print_mem(op2.reg, op2.value);
        printf("\n");
        if (op1.type == OpType_ptr || op1.type == OpType_ptrptr)
            printf("mov%s %s, [%s]\n", opcode_suffix[isflt(op2.kind)][op2.kind], reg_names[op2.kind][tmpreg], reg_names[Type_Int64][rax]);
        else tmpptr = true;
    }
    else if (op2.type == OpType_imm && op2.kind > Type_Int32) {
        use_tmp = true;
        jitc_type_kind_t itype = isflt(op2.kind) ? op2.kind - Type_Float32 + Type_Int32 : op2.kind;
        printf("mov %s, ", reg_names[itype][rax]);
        if (op2.kind == Type_Float32) {
            float x = *(double*)&op2.value;
            printf("0x%x\n", *(uint32_t*)&x);
        }
        else printf("0x%lx\n", op2.value);
        if (isflt(op2.kind)) printf("mov%s %s, %s\n", opcode_suffix[isflt(op2.kind)][itype], reg_names[op2.kind][tmpreg], reg_names[itype][rax]);
    }

    if (op1.type == OpType_ptrptr) {
        printf("mov %s, ", reg_names[Type_Int64][rcx]);
        print_mem(op1.reg, op1.value);
        if (isflt(op1.kind) && strcmp(opcode, "mov") != 0) {
            store_float = true;
            printf("mov%s %s, [%s]\n", opcode_suffix[isflt(op1.kind)][op1.kind], reg_names[op1.kind][xmm0], reg_names[Type_Int64][rcx]);
            printf("%s%s %s", opcode, opcode_suffix[isflt(op1.kind)][op2.kind], reg_names[op1.kind][xmm0]);
        }
        else {
            printf("\n%s%s ", opcode, opcode_suffix[isflt(op1.kind)][op2.kind]);
            if (op2.type == OpType_imm) printf("%s ptr ", ptr_prefixes[op2.kind]);
            printf("[%s]", reg_names[Type_Int64][rcx]);
        }
    }
    else if (op1.type == OpType_ptr && isflt(op1.kind) && strcmp(opcode, "mov") != 0) {
        store_float = true;
        printf("mov%s %s, ", opcode_suffix[isflt(op1.kind)][op1.kind], reg_names[op1.kind][xmm0]);
        print_mem(op1.reg, op1.value);
        printf("\n%s%s %s", opcode, opcode_suffix[isflt(op1.kind)][op2.kind], reg_names[op1.kind][xmm0]);
    }
    else if (op1.type == OpType_ptr) {
        printf("%s%s ", opcode, opcode_suffix[isflt(op1.kind)][op2.kind]);
        if (op2.type == OpType_imm) printf("%s ptr ", ptr_prefixes[op2.kind]);
        print_mem(op1.reg, op1.value);
    }
    else printf("%s%s %s", opcode, opcode_suffix[isflt(op1.kind)][op2.kind], reg_names[op1.kind][op1.reg]);
    printf(", ");

    if (use_tmp) printf(tmpptr ? "[%s]" : "%s", reg_names[op2.kind][tmpreg]);
    else if (op2.type == OpType_imm) {
        if (op2.kind == Type_Float32) {
            float x = *(double*)&op2.value;
            printf("0x%x", *(uint32_t*)&x);
        }
        else printf("0x%lx", op2.value);
    }
    else if (op2.type == OpType_reg) printf("%s", reg_names[op2.kind][op2.reg]);
    else if (op2.type == OpType_ptr) print_mem(op2.reg, op2.value);
    printf("\n");
    
    if (store_float) {
        printf("mov%s ", opcode_suffix[isflt(op1.kind)][op1.kind]);
        if (op1.type == OpType_ptrptr) printf("[%s]", reg_names[Type_Int64][rcx]);
        else print_mem(op1.reg, op1.value);
        printf(", %s\n", reg_names[op1.kind][xmm0]);
    }
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
    else if (op.type == OpType_imm) {
        printf("mov %s, 0x%lx\n", reg_names[op.kind][rcx], op.value);
        printf("%s %s", opcode, reg_names[op.kind][rcx]);
    }
    if (bitshift) printf(", cl\n");
    else printf("\n");
}

static stack_item_t* stackalloc(uint32_t bytes) {
    stack_sub(bytes);
    size_t ptr = stack_bytes;
    stack_item_t* item = push(StackItem_rvalue, Type_Pointer, true);
    item->extra_storage = bytes;
    instr2("mov", op(item), reg(rsp, Type_Pointer, true));
    instr2("add", op(item), imm(8, Type_Int64, true));
    return item;
}

static void binaryop(const char* opcode) {
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    if (op1.type != StackItem_rvalue) instr2("mov", op(res), op(&op1));
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
    if (addr.type != StackItem_rvalue) instr2("mov", op(res), op(&addr));
    correct_kind(&kind, &is_unsigned);
    res->kind = kind;
    res->is_unsigned = is_unsigned;
    res->type = StackItem_lvalue_abs;
}

static void copy(uint64_t count, uint64_t alignment) {
    stack_item_t op2 = pop();
    stack_item_t* op1 = peek(0);
    instr2("lea", reg(rdi, Type_Int64, true), op(op1));
    instr2("lea", reg(rsi, Type_Int64, true), op(&op2));
    instr2("mov", reg(rcx, Type_Int32, true), imm(count, Type_Int32, true));
    printf("rep %s\n",
        alignment == 1 ? "movsb" :
        alignment == 2 ? "movsw" :
        alignment == 4 ? "movsd" :
        alignment == 8 ? "movsq" : "<UNK>"
    );
}

static void divide(reg_t outreg, bool store) {
    stack_item_t op1, op2 = pop();
    stack_item_t* res = NULL;
    if (store) op1 = *(res = peek(0));
    else {
        op1 = pop();
        res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    }
    if (isflt(op1.kind)) {
        if (op1.type != StackItem_rvalue) instr2("mov", op(res), op(&op1));
        instr2("div", op(res), op(&op2));
    }
    else {
        instr2("mov", reg(rax, op1.kind, op1.is_unsigned), op(&op1));
        if (op1.kind == Type_Int8) {
            if (op1.is_unsigned) printf("mov %s, 0x0\n", reg_names[Type_Int8][rax]);
            else printf("cbw\n");
        }
        else {
            if (op1.is_unsigned) printf("mov %s, 0x0\n", reg_names[op1.kind][rdx]);
            else printf("%s\n", (const char*[]){ "", "cwd", "cdq", "cqo", "", "", "cqo" }[op1.kind]);
        }
        instr1("idiv", op(&op2), false);
        instr2("mov", op(res), reg(outreg, op1.kind, op1.is_unsigned));
    }
}

static void negate() {
    stack_item_t* op1 = peek(0);
    if (isflt(op1->kind)) {
        instr2("mul", op(op1), imm(*(uint64_t*)&(double){-1}, op1->kind, op1->is_unsigned));
        pop();
    }
    else unaryop("neg", false);
}

static void increment(int32_t step, bool flip) {
    stack_item_t op1 = pop();
    stack_item_t* res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
    uint64_t data = 0;
    if (op1.kind == Type_Float32) data = *(uint32_t*)&(float){abs(step)};
    else if (op1.kind == Type_Float64) data = *(uint64_t*)&(double){abs(step)};
    else data = abs(step);
    if (flip) instr2("mov", op(res), op(&op1));
    if (step > 0) instr2("add", op(&op1), imm(data, op1.kind, op1.is_unsigned));
    if (step < 0) instr2("sub", op(&op1), imm(data, op1.kind, op1.is_unsigned));
    if (!flip) instr2("mov", op(res), op(&op1));
}

static void bitshift(bool store, bool is_right) {
    stack_item_t op2 = pop();
    stack_item_t* res = NULL;
    if (store) res = peek(0);
    else {
        stack_item_t op1 = pop();
        res = push(StackItem_rvalue, op1.kind, op1.is_unsigned);
        if (op1.type != StackItem_rvalue) instr2("mov", op(res), op(&op1));
    }
    instr2("mov", reg(rcx, op2.kind, op2.is_unsigned), op(&op2));
    instr1(is_right ? "shr" : "shl", op(res), true);
}

static void compare(const char* opcode) {
    stack_item_t op2 = pop();
    stack_item_t op1 = pop();
    operand_t res = op(push(StackItem_rvalue, Type_Int8, true));
    instr2(isflt(op2.kind) ? "ucomi" : "cmp", op(&op1), op(&op2));
    instr1(opcode, res, false);
}

static void compare_against(const char* opcode, operand_t op2) {
    stack_item_t op1 = pop();
    operand_t res = op(push(StackItem_rvalue, Type_Int8, true));
    instr2(isflt(op1.kind) ? "ucomi" : "cmp", op(&op1), op2);
    instr1(opcode, res, false);
}

static void addrof() {
    stack_item_t item = pop();
    operand_t op1 = op(&item);
    operand_t res = op(push(StackItem_rvalue, Type_Pointer, true));
    op1.kind = Type_Pointer;
    op1.is_unsigned = true;
    if (item.type != StackItem_rvalue) instr2("lea", res, op1);
}

static void convert(jitc_type_kind_t kind, bool is_unsigned) {
    stack_item_t item = pop();
    operand_t op1 = op(&item);
    operand_t res = op(push(StackItem_rvalue, kind, is_unsigned));
    if (op1.kind == Type_Pointer) op1.kind = Type_Int64;
    if (res.kind == Type_Pointer) res.kind = Type_Int64;
    if (res.kind == Type_Float32) {
        if (op1.kind == Type_Float32) instr2("mov", res, op1);
        else if (op1.kind == Type_Float64) instr2("cvtsd2ss", res, op1);
        else if (op1.kind == Type_Int64 && op1.is_unsigned) {
            operand_t itmp = reg(rax, Type_Int64, true);
            operand_t ftmp = reg(xmm15, Type_Float64, true);
            instr2("mov", itmp, op1);
            instr2("cvtsi2ss", res, itmp);
            instr2("sar", itmp, imm(63, Type_Int64, true));
            instr2("and", itmp, imm(0x5F800000, Type_Int64, true));
            instr2("mov", ftmp, itmp);
            instr2("add", res, ftmp);
        }
        else {
            operand_t newop = op1;
            newop.kind = Type_Int32;
            if (op1.kind < Type_Int32) instr2(op1.is_unsigned ? "movzx" : "movsx", newop, op1);
            instr2("mov", res, newop);
        }
    }
    else if (res.kind == Type_Float64) {
        if (op1.kind == Type_Float32) instr2("cvtss2sd", res, op1);
        else if (op1.kind == Type_Float64) instr2("mov", res, op1);
        else if (op1.kind == Type_Int64 && op1.is_unsigned) {
            operand_t itmp = reg(rax, Type_Int64, true);
            operand_t ftmp = reg(xmm15, Type_Float64, true);
            instr2("mov", itmp, op1);
            instr2("cvtsi2sd", res, itmp);
            instr2("sar", itmp, imm(63, Type_Int64, true));
            instr2("and", itmp, imm(0x43f0000000000000, Type_Int64, true));
            instr2("mov", ftmp, itmp);
            instr2("add", res, ftmp);
        }
        else {
            operand_t newop = op1;
            newop.kind = Type_Int64;
            if (op1.kind < Type_Int64) instr2(op1.is_unsigned ? "movzx" : "movsx", newop, op1);
            instr2("mov", res, newop);
        }
    }
    else {
        if (op1.kind == Type_Float32) {
            if (res.kind < Type_Int32) res.kind = Type_Int32;
            instr2("cvttss2si", res, op1);
        }
        else if (op1.kind == Type_Float64) {
            if (res.kind < Type_Int64) res.kind = Type_Int64;
            instr2("cvttsd2si", res, op1);
        }
        else {
            if (op1.kind > res.kind) {
                op1.kind = res.kind;
                instr2("mov", res, op1);
            }
            if (op1.kind < res.kind) instr2(op1.is_unsigned ? "movzx" : "movsx", res, op1);
        }
    }
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
    instr2(isflt(item.kind) ? "ucomi" : "cmp", op(&item), op(&item));
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

static void call(jitc_type_t* signature) {
    stack_item_t func = pop();
    stack_item_t* ret = push(StackItem_rvalue, signature->func.ret->kind, signature->func.ret->is_unsigned);
    int int_params = 0, float_params = 0, stack_params = 0;
    for (size_t i = 0; i < signature->func.num_params; i++) {
        stack_item_t* item = peek(i);
        if (isflt(signature->func.params[i]->kind) && float_params < 8) float_params++;
        else if (int_params < 6) int_params++;
        else stack_params++;
    }
    int_params = 0, float_params = 0;
    int temp_stack = stack_params * 8;
    if (temp_stack != 0) stack_sub(temp_stack);
    for (size_t i = 0; i < signature->func.num_params; i++) {
        stack_item_t* item = peek(i);
        if (isflt(signature->func.params[i]->kind) && float_params < 8) instr2("mov", reg((reg_t[]){
            xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8
        }[float_params++], item->kind, item->is_unsigned), op(item));
        else if (int_params < 6) instr2("mov", reg((reg_t[]){
            rdi, rsi, rdx, rcx, r8, r9
        }[int_params++], item->kind, item->is_unsigned), op(item));
        else instr2("mov", ptr(rsp, --stack_params * 8, item->kind, item->is_unsigned), op(item));
    }
    instr2("lea", reg(rax, Type_Pointer, true), op(&func));
    instr1("call", reg(rax, Type_Pointer, true), false);
    if (temp_stack != 0) stack_free(temp_stack);
    if (ret) {
        if (isflt(ret->kind))
            instr2("mov", op(ret), reg(xmm0, ret->kind, false));
        else
            instr2("mov", op(ret), reg(rax, ret->kind, ret->is_unsigned));
    }
}

static jitc_type_t* func_signature = NULL;

static void func_begin(jitc_type_t* signature, size_t stack_size) {
    func_signature = signature;
    printf("push rbp\n"); // todo: preserve rbx and r12..r15
    printf("mov rbp, rsp\n");
    if (stack_size != 0) printf("sub rsp, 0x%lx\n", stack_size);
}

static void ret() {
    if (isflt(func_signature->func.ret->kind))
        instr2("mov", reg(xmm0, func_signature->func.ret->kind, false), op(peek(0)));
    else if (func_signature->func.ret->kind != Type_Void)
        instr2("mov", reg(rax, func_signature->func.ret->kind, func_signature->func.ret->is_unsigned), op(peek(0)));
    pop();
    printf("jmp _ret\n");
}

static void func_end() {
    printf("_ret:\n");
    printf("leave\n");
    printf("ret\n");
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
            case IROpCode_laddr: instr2("mov",
                unptr(op(push(StackItem_lvalue_abs, ir->params[1].as_integer, ir->params[2].as_integer))),
                imm(ir->params[0].as_integer, Type_Pointer, true)
            ); break;
            case IROpCode_lstack:
                pushi(StackItem_lvalue, ir->params[1].as_integer, ir->params[2].as_integer, ir->params[0].as_integer);
                break;
            case IROpCode_pop: pop(); break;
            case IROpCode_load: load(ir->params[0].as_integer, ir->params[1].as_integer); break;
            case IROpCode_store: instr2("mov", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_copy: copy(ir->params[0].as_integer, ir->params[1].as_integer); break;
            case IROpCode_add: binaryop("add"); break;
            case IROpCode_sub: binaryop("sub"); break;
            case IROpCode_mul: binaryop(isflt(peek(1)->kind) ? "mul" : "imul"); break;
            case IROpCode_div: divide(rax, false); break;
            case IROpCode_mod: divide(rdx, false); break;
            case IROpCode_and: binaryop("and"); break;
            case IROpCode_or:  binaryop("or");  break;
            case IROpCode_xor: binaryop("xor"); break;
            case IROpCode_shl: bitshift(false, false); break;
            case IROpCode_shr: bitshift(false, true); break;
            case IROpCode_sadd: instr2("add", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_ssub: instr2("sub", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_smul: instr2(isflt(peek(1)->kind) ? "mul" : "imul", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_sdiv: divide(rax, true); break;
            case IROpCode_smod: divide(rdx, true); break;
            case IROpCode_sand: instr2("and", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_sor: instr2("or", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_sxor: instr2("xor", op(peek(1)), op(peek(0))); pop(); break;
            case IROpCode_sshl: bitshift(true, false); break;
            case IROpCode_sshr: bitshift(true, true); break;
            case IROpCode_not: unaryop("not", false); break;
            case IROpCode_neg: negate(); break;
            case IROpCode_inc: increment(ir->params[1].as_integer, ir->params[0].as_integer); break;
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
            case IROpCode_call: call(ir->params[0].as_pointer); break;
            case IROpCode_ret: ret(); break;
            case IROpCode_func: func_begin(ir->params[0].as_pointer, ir->params[1].as_integer); break;
            case IROpCode_func_end: func_end(); break;
        }
        free(ir);
    }
    return NULL;
}
