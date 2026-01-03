#include "../jitc_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

//#define DEBUG

typedef enum: uint8_t {
    rax, rcx, rdx, rbx,
    rsp, rbp, rsi, rdi,
    r8,  r9,  r10, r11,
    r12, r13, r14, r15,

    xmm0=0,xmm1,  xmm2,  xmm3,
    xmm4,  xmm5,  xmm6,  xmm7,
    xmm8,  xmm9,  xmm10, xmm11,
    xmm12, xmm13, xmm14, xmm15,
} reg_t;

static reg_t stack_regs[] = { rbx, r12, r13, r14, r15, r10, r11 };
static reg_t stack_xmms[] = { xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14 };

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
    union {
        struct {
            reg_t reg;
            int32_t disp;
        };
        uint64_t value;
    };
} operand_t;

typedef enum: uint8_t {
    mov, movzx, movsx, lea,
    add, sub, imul, idiv, and, or, xor, cmp,
    shl, shr, sar, not, neg, jmp, jz, jnz, call, leave, ret,
    sete, setne, setl, setle, setg, setge,
    cbw, cwd, cdq, cqo, opc_push, opc_pop,
    rep_movsb, rep_movsw, rep_movsd, rep_movsq,
    cvtsi2ss, cvtsi2sd, cvttss2si, cvttsd2si, cvtss2sd, cvtsd2ss
} mnemonic_t;

typedef enum: uint16_t {
    force_rexw = (1 << 0),
    force_size = (1 << 1),
    has_modrm = (1 << 2),
    modrm_op2_mask = (1 << 3),
    modrm_opc = (1 << 4),
    prefix_f2 = (1 << 5),
    prefix_f3 = (1 << 6),
    twobyte = (1 << 7),
    flip_modrm = (1 << 8),
    no_rax = (1 << 9),

    modrm_op2 = modrm_op2_mask | has_modrm,
} instr_flags_t;

typedef enum: uint8_t {
    C_REG = (1 << 0), C_XMM = (1 << 1), C_IMM = (1 << 2), C_MEM = (1 << 3),
    C__S8  = (1 << 4), C_S16 = (1 << 5), C_S32 = (1 << 6), C_S64 = (1 << 7),

    C_NO8 = C_S16 | C_S32 | C_S64,
} instr_constraints_t;

typedef struct {
    mnemonic_t mnemonic;
    uint8_t opcode;
    instr_flags_t flags;
    instr_constraints_t constraints[2];
    uint8_t modrm_fixed_bits;
} instr_t;

typedef enum: uint8_t {
    Mode_Mem = 0b00,
    Mode_Disp8 = 0b01,
    Mode_Disp32 = 0b10,
    Mode_Reg = 0b11,
} opmode_t;

typedef enum: uint8_t {
    Legal_imm_reg,
    Legal_deref_reg,
    Legal_deref_xmm,
    Legal_deref_mem,
    Legal_to_reg,
    Legal_to_xmm,
    Legal_perform,
    Legal_writeback,
    Legal_next_op,
} legalization_step_t;

typedef struct {
    uint8_t cost;
    legalization_step_t steps[15];
} legalization_t;

static instr_t instructions[] = {
    { mov, 0x88, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { mov, 0x89, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { mov, 0x8A, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { mov, 0x8B, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { mov, 0xB0, modrm_opc, { C_REG | C__S8, C_IMM | C__S8 }},
    { mov, 0xB8, modrm_opc, { C_REG | C_NO8, C_IMM | C_NO8 }},
    { mov, 0xC6, has_modrm, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }},
    { mov, 0xC7, has_modrm, { C_REG | C_MEM | C_S16 | C_S32, C_IMM | C_S16 | C_S32 }},
    { movzx, 0xB6, has_modrm | twobyte | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C__S8 }},
    { movzx, 0xB7, has_modrm | twobyte | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_S16 }},
    { movzx, 0x8B, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_S32 }}, // this is actually mov in disguise
    { movsx, 0xBE, has_modrm | twobyte | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C__S8 }},
    { movsx, 0xBF, has_modrm | twobyte | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_S16 }},
    { movsx, 0x63, has_modrm | force_rexw | flip_modrm, { C_REG | C_S64, C_REG | C_MEM | C_S32 }},
    { mov, 0x6E, force_size | has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_REG | C_MEM | C_S32 }},
    { mov, 0x7E, force_size | has_modrm | twobyte, { C_REG | C_MEM | C_S32, C_XMM | C_S32 }},
    { mov, 0x6E, force_size | has_modrm | twobyte | force_rexw | flip_modrm, { C_XMM | C_S64, C_REG | C_MEM | C_S64 }},
    { mov, 0x7E, force_size | has_modrm | twobyte | force_rexw, { C_REG | C_MEM | C_S64, C_XMM | C_S64 }},
    { mov, 0x10, prefix_f3 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S32 }},
    { mov, 0x10, prefix_f2 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S64 }},
    { lea, 0x8D, has_modrm | force_rexw | flip_modrm, { C_REG | C_S64, C_MEM | C__S8 | C_NO8 }},
    { add, 0x00, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { add, 0x01, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { add, 0x02, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { add, 0x03, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { add, 0x80, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b000 },
    { add, 0x83, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b000 },
    { add, 0x81, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C_S16 | C_S32 }, 0b000 },
    { add, 0x58, prefix_f3 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S32 }},
    { add, 0x58, prefix_f2 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S64 }},
    { sub, 0x28, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { sub, 0x29, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { sub, 0x2A, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { sub, 0x2B, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { sub, 0x80, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b101 },
    { sub, 0x83, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b101 },
    { sub, 0x81, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C_S16 | C_S32 }, 0b101 },
    { sub, 0x5C, prefix_f3 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S32 }},
    { sub, 0x5C, prefix_f2 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S64 }},
    { imul, 0xF6, modrm_op2 | no_rax, { C_REG | C_MEM | C__S8 }, 0b101 },
    { imul, 0xF7, modrm_op2 | no_rax, { C_REG | C_MEM | C_NO8 }, 0b101 },
    { imul, 0x59, prefix_f3 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S32 }},
    { imul, 0x59, prefix_f2 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S64 }},
    { idiv, 0xF6, modrm_op2 | no_rax, { C_REG | C_MEM | C__S8 }, 0b111 },
    { idiv, 0xF7, modrm_op2 | no_rax, { C_REG | C_MEM | C_NO8 }, 0b111 },
    { idiv, 0x5E, prefix_f3 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S32 }},
    { idiv, 0x5E, prefix_f2 | has_modrm | twobyte | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S64 }},
    { and, 0x20, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { and, 0x21, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { and, 0x22, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { and, 0x23, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { and, 0x80, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b100 },
    { and, 0x83, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b100 },
    { and, 0x81, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C_S16 | C_S32 }, 0b100 },
    { or, 0x08, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { or, 0x09, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { or, 0x0A, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { or, 0x0B, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { or, 0x80, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b001 },
    { or, 0x83, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b001 },
    { or, 0x81, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C_S16 | C_S32 }, 0b001 },
    { xor, 0x30, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { xor, 0x31, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { xor, 0x32, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { xor, 0x33, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { xor, 0x80, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b110 },
    { xor, 0x83, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b110 },
    { xor, 0x81, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C_S16 | C_S32 }, 0b110 },
    { cmp, 0x38, has_modrm, { C_REG | C_MEM | C__S8, C_REG | C__S8 }},
    { cmp, 0x39, has_modrm, { C_REG | C_MEM | C_NO8, C_REG | C_NO8 }},
    { cmp, 0x3A, has_modrm | flip_modrm, { C_REG | C__S8, C_REG | C_MEM | C__S8 }},
    { cmp, 0x3B, has_modrm | flip_modrm, { C_REG | C_NO8, C_REG | C_MEM | C_NO8 }},
    { cmp, 0x80, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b111 },
    { cmp, 0x83, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b111 },
    { cmp, 0x81, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C_S16 | C_S32 }, 0b111 },
    { cmp, 0x2E, has_modrm | twobyte | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S32 }},
    { cmp, 0x2E, has_modrm | twobyte | force_size | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S64 }},
    { shl, 0xD2, modrm_op2, { C_REG | C_MEM | C__S8 }, 0b100 },
    { shl, 0xD3, modrm_op2, { C_REG | C_MEM | C_NO8 }, 0b100 },
    { shl, 0xC0, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b100 },
    { shl, 0xC1, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b100 },
    { shr, 0xD2, modrm_op2, { C_REG | C_MEM | C__S8 }, 0b101 },
    { shr, 0xD3, modrm_op2, { C_REG | C_MEM | C_NO8 }, 0b101 },
    { shr, 0xC0, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b101 },
    { shr, 0xC1, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b101 },
    { sar, 0xC0, modrm_op2, { C_REG | C_MEM | C__S8, C_IMM | C__S8 }, 0b111 },
    { sar, 0xC1, modrm_op2, { C_REG | C_MEM | C_NO8, C_IMM | C__S8 }, 0b111 },
    { not, 0xF6, modrm_op2, { C_REG | C_MEM | C__S8 }, 0b010 },
    { not, 0xF7, modrm_op2, { C_REG | C_MEM | C_NO8 }, 0b010 },
    { neg, 0xF6, modrm_op2, { C_REG | C_MEM | C__S8 }, 0b011 },
    { neg, 0xF7, modrm_op2, { C_REG | C_MEM | C_NO8 }, 0b011 },
    { sete,  0x94, modrm_op2 | twobyte, { C_REG | C_MEM | C__S8 }, 0b000 },
    { setne, 0x95, modrm_op2 | twobyte, { C_REG | C_MEM | C__S8 }, 0b000 },
    { setl,  0x9C, modrm_op2 | twobyte, { C_REG | C_MEM | C__S8 }, 0b000 },
    { setle, 0x9E, modrm_op2 | twobyte, { C_REG | C_MEM | C__S8 }, 0b000 },
    { setg,  0x9F, modrm_op2 | twobyte, { C_REG | C_MEM | C__S8 }, 0b000 },
    { setge, 0x9D, modrm_op2 | twobyte, { C_REG | C_MEM | C__S8 }, 0b000 },
    { jmp, 0xE9, 0, { C_IMM | C_S32 }},
    { jz, 0x84, twobyte, { C_IMM | C_S32 }},
    { jnz, 0x85, twobyte, { C_IMM | C_S32 }},
    { opc_push, 0x50, modrm_opc, { C_REG | C_S64 }},
    { opc_pop, 0x58, modrm_opc, { C_REG | C_S64 }},
    { call, 0xFF, modrm_op2, { C_REG | C_MEM | C_S64 }, 0b010 },
    { leave, 0xC9 },
    { ret, 0xC3 },
    { cbw, 0x98 },
    { cwd, 0x99, force_size },
    { cdq, 0x99 },
    { cqo, 0x99, force_rexw },
    { cvtsi2ss, 0x2A, twobyte | has_modrm | prefix_f3 | flip_modrm, { C_XMM | C_S32, C_REG | C_MEM | C_S32 }},
    { cvtsi2sd, 0x2A, twobyte | has_modrm | prefix_f2 | force_rexw | flip_modrm, { C_XMM | C_S64, C_REG | C_MEM | C_S64 }},
    { cvttss2si, 0x2C, twobyte | has_modrm | prefix_f3 | flip_modrm, { C_REG | C_S32, C_XMM | C_MEM | C_S32 }},
    { cvttsd2si, 0x2C, twobyte | has_modrm | prefix_f2 | force_rexw | flip_modrm, { C_REG | C_S64, C_XMM | C_MEM | C_S64 }},
    { cvtss2sd, 0x5A, twobyte | has_modrm | prefix_f3 | flip_modrm, { C_XMM | C_S64, C_XMM | C_MEM | C_S32 }},
    { cvtsd2ss, 0x5A, twobyte | has_modrm | prefix_f2 | flip_modrm, { C_XMM | C_S32, C_XMM | C_MEM | C_S64 }},
    { rep_movsb, 0xA4, prefix_f3 },
    { rep_movsw, 0xA5, prefix_f3 | force_size },
    { rep_movsd, 0xA5, prefix_f3 },
    { rep_movsq, 0xA5, prefix_f3 | force_rexw },
};

static int opstack_capacity = 0, opstack_size = 0;
static stack_item_t* opstack = NULL;

static int opstack_int_index = 0, opstack_float_index = 0;
static size_t stack_bytes = 0;

static bool isflt(jitc_type_kind_t kind) {
    return kind == Type_Float32 || kind == Type_Float64;
}

static void correct_kind(jitc_type_kind_t* kind, bool* is_unsigned) {
    if (*kind > Type_Pointer && *kind != Type_Struct && *kind != Type_Union) *kind = Type_Pointer;
    if (*kind == Type_Pointer) *is_unsigned = true;
    if (isflt(*kind)) *is_unsigned = false;
}

static void stack_sub(bytewriter_t* writer, size_t size);
static void stack_free(bytewriter_t* writer, size_t size);

static stack_item_t* push(bytewriter_t* writer, stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned) {
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
    if (type == StackItem_rvalue || type == StackItem_lvalue_abs) {
        int* index = &opstack_int_index;
        if (type == StackItem_rvalue && isflt(item->kind)) index = &opstack_float_index;
        if (*index < sizeof(stack_regs)) item->value = *index | (1L << 63);
        else {
            stack_sub(writer, 8);
            item->value = stack_bytes;
        }
        (*index)++;
    }
#ifdef DEBUG
    printf("PUSH %s %d (%d %d)\n", (const char*[]){"literal", "rvalue", "lvalue", "lvalue_abs"}[type], opstack_size, opstack_int_index, opstack_float_index);
#endif
    return item;
}

static stack_item_t* pushi(bytewriter_t* writer, stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned, uint64_t value) {
    stack_item_t* item = push(writer, type, kind, is_unsigned);
    item->value = value;
    return item;
}

static stack_item_t* pushf(bytewriter_t* writer, stack_item_type_t type, jitc_type_kind_t kind, bool is_unsigned, double value) {
    return pushi(writer, type, kind, is_unsigned, *(uint64_t*)&value);
}

static stack_item_t* peek(int offset) {
    return &opstack[opstack_size - 1 - offset];
}

static stack_item_t pop(bytewriter_t* writer) {
    stack_item_t* item = peek(0);
    if (item->type == StackItem_rvalue || item->type == StackItem_lvalue_abs) {
        if (item->type == StackItem_rvalue && isflt(item->kind)) opstack_float_index--;
        else opstack_int_index--;
        if (!(item->value & (1L << 63))) stack_free(writer, 8);
        if (item->extra_storage != 0) stack_free(writer, item->extra_storage);
    }
    opstack_size--;
#ifdef DEBUG
    printf("POP  %s %d (%d %d)\n", (const char*[]){"literal", "rvalue", "lvalue", "lvalue_abs"}[item->type], opstack_size, opstack_int_index, opstack_float_index);
#endif
    return *item;
}

static operand_t reg(reg_t reg, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_reg, .kind = kind, .is_unsigned = is_unsigned, .reg = reg };
}

static operand_t ptr(reg_t reg, int32_t offset, jitc_type_kind_t kind, bool is_unsigned) {
    return (operand_t){ .type = OpType_ptr, .kind = kind, .is_unsigned = is_unsigned, .reg = reg, .disp = offset };
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
            .disp = item->offset
        };
        case StackItem_lvalue_abs: {
            operand_t op = (operand_t){ .kind = item->kind, .is_unsigned = item->is_unsigned };
            if (item->value & (1L << 63)) {
                op.type = OpType_ptr;
                op.reg = (isflt(item->kind) ? stack_xmms : stack_regs)[item->value & ~(1L << 63)];
                op.disp = item->offset;
            }
            else {
                op.type = OpType_ptrptr;
                op.reg = rsp;
                op.disp = stack_bytes - item->value + item->offset;
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
                op.disp = stack_bytes - item->value;
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
        op1.disp = 0;
    }
    else if (op1.type == OpType_ptrptr) op1.type = OpType_ptr;
    return op1;
}

static instr_flags_t get_extra_flags(instr_constraints_t constraints, jitc_type_kind_t type, bool allow_rex_float) {
    if (type == Type_Int16) return force_size;
    if (type == Type_Int64 || type == Type_Pointer || (allow_rex_float && type == Type_Float64)) {
        if ((constraints & C_S64) && !(constraints & (C__S8 | C_S16 | C_S32))) return 0;
        return force_rexw;
    }
    return 0;
}

static void encode_instruction(bytewriter_t* writer, uint8_t opcode, reg_t reg1, reg_t reg2, opmode_t mode, uint8_t modrm_bits, instr_flags_t flags) {
    uint8_t rex = 0;
    reg_t op1 = flags & flip_modrm ? reg2 : reg1;
    reg_t op2 = flags & flip_modrm ? reg1 : reg2;
    if (op1 >= 8) rex |= 0x40 | 0b0001;
    if (op2 >= 8) rex |= 0x40 | 0b0100;
    if (flags & force_rexw) rex |= 0x48;
    if (flags & force_size) bytewriter_int8(writer, 0x66);
    if (flags & prefix_f3) bytewriter_int8(writer, 0xF3);
    if (flags & prefix_f2) bytewriter_int8(writer, 0xF2);
    if (rex != 0) bytewriter_int8(writer, rex);
    if (flags & twobyte) bytewriter_int8(writer, 0x0F);
    bytewriter_int8(writer, opcode | (flags & modrm_opc ? reg1 & 0b111 : 0));
    if (flags & has_modrm) {
        bool emit_zero = false;
        if (mode == Mode_Mem && (op1 & 0b111) == 0b101) {
            mode = Mode_Disp8;
            emit_zero = true;
        }
        uint8_t modrm = (mode & 0b11) << 6;
        if (flags & modrm_op2_mask) modrm |= (modrm_bits & 0b111) << 3;
        else modrm |= (op2 & 0b111) << 3;
        modrm |= op1 & 0b111;
        bytewriter_int8(writer, modrm);
        if ((mode & 0b11) != Mode_Reg && (op1 & 0b111) == 0b100)
            bytewriter_int8(writer, 0x24); // SID byte
        if (emit_zero) bytewriter_int8(writer, 0x00);
    }
}

static void emit_instruction(bytewriter_t* writer, instr_t* instr, reg_t reg1, reg_t reg2, opmode_t mode, instr_flags_t extra_flags) {
    encode_instruction(writer, instr->opcode, reg1, reg2, mode, instr->modrm_fixed_bits, instr->flags | extra_flags);
}

static bool legalize(legalization_t* legalization, operand_t op, instr_constraints_t constraints, bool writeback) {
#define step() legalization->steps[legalization->cost++]
    instr_constraints_t size_mask = (instr_constraints_t[]){ C__S8, C_S16, C_S32, C_S64, C_S32, C_S64, C_S64 }[op.kind];
    if (!(constraints & size_mask)) return legalization->cost = 0;
    if (op.type == OpType_imm) {
        if (constraints & C_IMM) (void)0;
        else if (constraints & C_REG) step() = Legal_imm_reg;
        else if (constraints & C_XMM) {
            step() = Legal_imm_reg;
            step() = Legal_to_xmm;
        }
        else return legalization->cost = 0;
    }
    if (op.type == OpType_reg) {
        if (isflt(op.kind) && (constraints & C_REG) && !(constraints && C_XMM)) step() = Legal_to_reg;
        else if (!isflt(op.kind) && (constraints & C_XMM) && !(constraints & C_REG)) step() = Legal_to_xmm;
        else if (!(constraints & (isflt(op.kind) ? C_XMM : C_REG))) return legalization->cost = 0;
    }
    if (op.type == OpType_ptrptr) step() = Legal_deref_mem;
    if (op.type == OpType_ptrptr || op.type == OpType_ptr) {
        if (constraints & C_MEM) (void)0;
        else if (constraints & C_REG) step() = Legal_deref_reg;
        else if (constraints & C_XMM) step() = Legal_deref_xmm;
        else return legalization->cost = 0;
    }
    step() = Legal_perform;
    if (writeback && (op.type == OpType_ptr || op.type == OpType_ptrptr) && !(constraints & C_MEM))
        step() = Legal_writeback;
    return true;
#undef step
}

static void emit_instructions(bytewriter_t* writer, instr_t* instr, legalization_t* legalization, operand_t* ops, int num_ops) {
    int curr_op = num_ops - 1;
    reg_t int_tmp = instr->flags & no_rax ? rcx : rax;
    reg_t flt_tmp = xmm15;
    operand_t writeback;
    bool tmp_used = false;
    for (uint8_t i = 0; i < legalization->cost; i++) switch (legalization->steps[i]) {
        case Legal_imm_reg:
            encode_instruction(writer, ops[curr_op].kind == Type_Int8 ? 0xB0 : 0xB8, int_tmp, 0, Mode_Reg, 0, modrm_opc | get_extra_flags(0, ops[curr_op].kind, true));
            if (ops[curr_op].kind == Type_Int8) bytewriter_int8(writer, ops[curr_op].value);
            else if (ops[curr_op].kind == Type_Int16) bytewriter_int16(writer, ops[curr_op].value);
            else if (ops[curr_op].kind == Type_Int32) bytewriter_int32(writer, ops[curr_op].value);
            else if (ops[curr_op].kind == Type_Float32) {
                float f = *(double*)&ops[curr_op].value;
                bytewriter_int32(writer, *(uint32_t*)&f);
            }
            else bytewriter_int64(writer, ops[curr_op].value);
            ops[curr_op] = reg(int_tmp, ops[curr_op].kind, ops[curr_op].is_unsigned);
            tmp_used = true;
            break;
        case Legal_to_reg: {
            jitc_type_kind_t next_kind = ops[curr_op].kind == Type_Float32 ? Type_Int32 : Type_Int64;
            encode_instruction(writer, 0x7E, int_tmp, ops[curr_op].reg, Mode_Reg, 0, force_size | has_modrm | twobyte | flip_modrm | get_extra_flags(0, next_kind, false));
            ops[curr_op] = reg(int_tmp, next_kind, ops[curr_op].is_unsigned);
            tmp_used = true;
        } break;
        case Legal_to_xmm: {
            jitc_type_kind_t next_kind = ops[curr_op].kind == Type_Int32 ? Type_Float32 : Type_Float64;
            encode_instruction(writer, 0x6E, flt_tmp, ops[curr_op].reg, Mode_Reg, 0, force_size | has_modrm | twobyte | flip_modrm | get_extra_flags(0, ops[curr_op].kind, true));
            ops[curr_op] = reg(flt_tmp, next_kind, ops[curr_op].is_unsigned);
        } break;
        case Legal_deref_reg:
        case Legal_deref_xmm:
        case Legal_deref_mem: {
            opmode_t mode;
            if (ops[curr_op].disp == 0) mode = Mode_Mem;
            else if (ops[curr_op].disp >= INT8_MIN && ops[curr_op].disp <= INT8_MAX) mode = Mode_Disp8;
            else mode = Mode_Disp32;
            if (legalization->steps[i] == Legal_deref_xmm)
                encode_instruction(writer, 0x10, flt_tmp, ops[curr_op].reg, mode, 0, (ops[curr_op].kind == Type_Float32 ? prefix_f3 : prefix_f2) | has_modrm | twobyte | flip_modrm | get_extra_flags(0, ops[curr_op].kind, false));
            else
                encode_instruction(writer, ops[curr_op].kind == Type_Int8 ? 0x8A : 0x8B, int_tmp, ops[curr_op].reg, mode, 0, has_modrm | flip_modrm | get_extra_flags(0, ops[curr_op].kind, false));
            if (mode == Mode_Disp8)  bytewriter_int8 (writer, ops[curr_op].disp);
            if (mode == Mode_Disp32) bytewriter_int32(writer, ops[curr_op].disp);
            writeback = ops[curr_op];
            ops[curr_op] = reg(legalization->steps[i] == Legal_deref_xmm ? flt_tmp : int_tmp, ops[curr_op].kind, ops[curr_op].is_unsigned);
            ops[curr_op].disp = 0;
            if (legalization->steps[i] == Legal_deref_mem) ops[curr_op].type = OpType_ptr;
            tmp_used = true;
        } break;
        case Legal_perform: {
            opmode_t mode = Mode_Reg;
            operand_t* op1 = &ops[curr_op];
            operand_t* op2 = num_ops > 1 ? &ops[curr_op + 1] : op1;
            operand_t* mem = NULL;
            // dont mind me, just abusing short circuiting here
            if (op1->type == OpType_ptr && (mem = op1) || op2->type == OpType_ptr && (mem = op2)) {
                if (mem->disp == 0) mode = Mode_Mem;
                else if (mem->disp >= INT8_MIN && mem->disp <= INT8_MAX) mode = Mode_Disp8;
                else mode = Mode_Disp32;
            }
            emit_instruction(writer, instr, op1->reg, op1 == op2 || op2->type == OpType_imm ? rax : op2->reg, mode, get_extra_flags(instr->constraints[0], op1->kind, false));
            if (mode == Mode_Disp8) bytewriter_int8(writer, op1->disp == 0 ? op2->disp : op1->disp);
            if (mode == Mode_Disp32) bytewriter_int32(writer, op1->disp == 0 ? op2->disp : op1->disp);
            if (op2->type == OpType_imm) {
                jitc_type_kind_t kind = op1->kind < op2->kind ? op1->kind : op2->kind;
                if (kind == Type_Int8) bytewriter_int8(writer, op2->value);
                else if (kind == Type_Int16) bytewriter_int16(writer, op2->value);
                else if (kind == Type_Int32) bytewriter_int32(writer, op2->value);
                else if (kind == Type_Float32) {
                    float f = *(double*)&op2->value;
                    bytewriter_int32(writer, *(uint32_t*)&f);
                }
                else bytewriter_int64(writer, op2->value);
            }
        } break;
        case Legal_writeback: {
            opmode_t mode;
            if (writeback.disp == 0) mode = Mode_Mem;
            else if (writeback.disp >= INT8_MIN && writeback.disp <= INT8_MAX) mode = Mode_Disp8;
            else mode = Mode_Disp32;
            if (isflt(writeback.kind))
                encode_instruction(writer, 0x7E, writeback.reg, ops[curr_op].reg, mode, 0, force_size | has_modrm | twobyte | get_extra_flags(0, writeback.kind, true));
            else
                encode_instruction(writer, writeback.kind == Type_Int8 ? 0x88 : 0x89, writeback.reg, ops[curr_op].reg, mode, 0, has_modrm | get_extra_flags(0, writeback.kind, false));
            if (mode == Mode_Disp8) bytewriter_int8(writer, writeback.disp);
            if (mode == Mode_Disp32) bytewriter_int32(writer, writeback.disp);
        } break;
        case Legal_next_op:
            curr_op--;
            if (tmp_used) int_tmp++;
            break;
    }
}

static void emit(bytewriter_t* writer, mnemonic_t mnemonic, int num_ops, ...) {
#ifdef DEBUG
    printf("emitting %s\n", (const char*[]){
        "mov", "movzx", "movsx", "lea",
        "add", "sub", "imul", "idiv", "and", "or", "xor", "cmp",
        "shl", "shr", "sar", "not", "neg", "jmp", "jz", "call", "leave", "ret",
        "sete", "setne", "setl", "setle", "setg", "setge",
        "cbw", "cwd", "cdq", "cqo", "opc_push", "opc_pop",
        "rep_movsb", "rep_movsw", "rep_movsd", "rep_movsq",
        "cvtsi2ss", "cvtsi2sd", "cvttss2si", "cvttsd2si", "cvtss2sd", "cvtsd2ss"
    }[mnemonic]);
#endif
    legalization_t candidates[sizeof(instructions) / sizeof(instr_t)] = {};
    operand_t ops[num_ops];
    int min_cost = 12;
    int min_cost_index = -1;
    va_list list;
    va_start(list, num_ops);
    for (int i = 0; i < num_ops; i++) ops[i] = va_arg(list, operand_t);
    for (size_t i = 0; i < sizeof(instructions) / sizeof(instr_t); i++) {
        if (instructions[i].mnemonic != mnemonic) continue;
        // ok me using strlen is lowkey stupid here, but it counts bytes until 0,
        // and since "no operand" is just no constraints at all,
        // this effectively counts the number of operands
        if (strnlen((char*)instructions[i].constraints, 2) != num_ops) continue;
        for (int j = num_ops - 1; j >= 0; j--) {
            if (!legalize(&candidates[i], ops[j], instructions[i].constraints[j], j == 0)) break;
            if (j != 0) candidates[i].steps[candidates[i].cost - 1] = Legal_next_op;
        }
        if (candidates[i].cost == 0) {
            if (num_ops == 0) {
                min_cost_index = i;
                break;
            }
            continue;
        }
        if (min_cost > candidates[i].cost) {
            min_cost = candidates[i].cost;
            min_cost_index = i;
        }
    }
    if (min_cost_index == -1) {
        printf("[JITC] !! Unable to find suitable instruction (THIS IS AN INTERNAL BUG)\n");
        abort();
    }
    if (num_ops == 0) emit_instruction(writer, &instructions[min_cost_index], rax, rax, Mode_Reg, 0);
    else emit_instructions(writer, &instructions[min_cost_index], &candidates[min_cost_index], ops, num_ops);
}

static void stack_sub(bytewriter_t* writer, size_t size) {
    emit(writer, sub, 2, reg(rsp, Type_Int64, true), imm(size, Type_Int32, true));
    stack_bytes += size;
}

static void stack_free(bytewriter_t* writer, size_t size) {
    emit(writer, add, 2, reg(rsp, Type_Int64, true), imm(size, Type_Int32, true));
    stack_bytes -= size;
}

static void binaryop(bytewriter_t* writer, mnemonic_t mnemonic) {
    stack_item_t op2 = pop(writer);
    stack_item_t op1 = pop(writer);
    stack_item_t* res = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
    stack_item_t* writeback = NULL;
    if (op1.type != StackItem_rvalue) {
        if (op2.type == StackItem_rvalue || op2.type == StackItem_lvalue_abs) {
            writeback = res;
            push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
            res = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
        }
        emit(writer, mov, 2, op(res), op(&op1));
    }
    emit(writer, mnemonic, 2, op(res), op(&op2));
    if (writeback) {
        emit(writer, mov, 2, op(writeback), op(res));
        pop(writer);
        pop(writer);
    }
}

static void unaryop(bytewriter_t* writer, mnemonic_t mnemonic, bool flip) {
    stack_item_t op1 = pop(writer);
    stack_item_t* res = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
    if (flip) emit(writer, mov, 2, op(res), op(&op1));
    emit(writer, mnemonic, 1, op(&op1));
    if (!flip) emit(writer, mov, 2, op(res), op(&op1));
}

static void copy(bytewriter_t* writer, operand_t dst, operand_t src, uint64_t size, uint64_t alignment) {
    emit(writer, lea, 2, reg(rdi, Type_Int64, true), dst);
    emit(writer, lea, 2, reg(rsi, Type_Int64, true), src);
    emit(writer, mov, 2, reg(rcx, Type_Int32, true), imm(size / alignment, Type_Int32, true));
    emit(writer,
        alignment == 1 ? rep_movsb :
        alignment == 2 ? rep_movsw :
        alignment == 4 ? rep_movsd :
        alignment == 8 ? rep_movsq : 0, 0
    );
}

static void arithcomplex(bytewriter_t* writer, mnemonic_t mnemonic, reg_t outreg, bool store) {
    stack_item_t op1, op2 = pop(writer);
    stack_item_t* res = NULL;
    if (store) op1 = *(res = peek(0));
    else {
        op1 = pop(writer);
        res = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
    }
    if (op1.kind == Type_Int8 && op1.is_unsigned)
        emit(writer, mov, 2, reg(rax, Type_Int16, op1.is_unsigned), imm(0, Type_Int16, op1.is_unsigned));
    emit(writer, mov, 2, reg(rax, op1.kind, op1.is_unsigned), op(&op1));
    if (op1.kind == Type_Int8) {
        if (!op1.is_unsigned) emit(writer, cbw, 0);
    }
    else {
        if (op1.is_unsigned) emit(writer, mov, 2, reg(rdx, op1.kind, op1.is_unsigned), imm(0, op1.kind, op1.is_unsigned));
        else emit(writer, (mnemonic_t[]){ 0, cwd, cdq, cqo, 0, 0, cqo }[op1.kind], 0);
    }
    emit(writer, mnemonic, 1, op(&op2));
    emit(writer, mov, 2, op(res), reg(outreg, op1.kind, op1.is_unsigned));
}

static void increment(bytewriter_t* writer, int32_t step, bool flip) {
    stack_item_t op1 = pop(writer);
    stack_item_t* res = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
    uint64_t data = 0;
    if (op1.kind == Type_Float32) data = *(uint32_t*)&(float){abs(step)};
    else if (op1.kind == Type_Float64) data = *(uint64_t*)&(double){abs(step)};
    else data = abs(step);
    if (flip) emit(writer, mov, 2, op(res), op(&op1));
    if (step > 0) emit(writer, add, 2, op(&op1), imm(data, op1.kind, op1.is_unsigned));
    if (step < 0) emit(writer, sub, 2, op(&op1), imm(data, op1.kind, op1.is_unsigned));
    if (!flip) emit(writer, mov, 2, op(res), op(&op1));
}

static void bitshift(bytewriter_t* writer, bool store, bool is_right) {
    stack_item_t op2 = pop(writer);
    stack_item_t* res = NULL;
    if (store) res = peek(0);
    else {
        stack_item_t op1 = pop(writer);
        res = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
        if (op1.type != StackItem_rvalue) emit(writer, mov, 2, op(res), op(&op1));
    }
    emit(writer, mov, 2, reg(rcx, op2.kind, op2.is_unsigned), op(&op2));
    emit(writer, is_right ? shr : shl, 1, op(res));
}

static void compare(bytewriter_t* writer, mnemonic_t mnemonic) {
    stack_item_t op2 = pop(writer);
    stack_item_t op1 = pop(writer);
    operand_t res = op(push(writer, StackItem_rvalue, Type_Int8, true));
    emit(writer, cmp, 2, op(&op1), op(&op2));
    emit(writer, mnemonic, 1, res);
}

static void compare_against(bytewriter_t* writer, mnemonic_t mnemonic, operand_t op2) {
    stack_item_t op1 = pop(writer);
    operand_t res = op(push(writer, StackItem_rvalue, Type_Int8, true));
    emit(writer, cmp, 2, op(&op1), op2);
    emit(writer, mnemonic, 1, res);
}

static stack_t* shortcircuits;

static void push_shortcircuit(bytewriter_t* writer) {
    if (!shortcircuits) shortcircuits = stack_new();
    stack_push_ptr(shortcircuits, stack_new());
}

static void push_shortcircuit_jump(bytewriter_t* writer) {
    stack_push_int(stack_peek_ptr(shortcircuits), bytewriter_size(writer));
}

static void pop_shortcircuit(bytewriter_t* writer) {
    stack_t* stack = stack_pop_ptr(shortcircuits);
    while (stack_size(stack) > 0) {
        int* ptr = (int*)(bytewriter_data(writer) + stack_peek_int(stack));
        ptr[-1] = bytewriter_size(writer) - stack_peek_int(stack);
        stack_pop(stack);
    }
    stack_delete(stack);
}

static stack_t* returns;
static stack_t* branches;

typedef struct {
    size_t branch_start;
    size_t curr_jump;
    stack_t* end_stack;
    bool is_loop;
} branch_t;

static void push_return(bytewriter_t* writer) {
    if (!returns) returns = stack_new();
    stack_push_int(returns, bytewriter_size(writer));
}

static void pop_return(bytewriter_t* writer) {
    while (stack_size(returns) > 0) {
        int* ptr = (int*)(bytewriter_data(writer) + stack_peek_int(returns));
        ptr[-1] = bytewriter_size(writer) - stack_peek_int(returns);
        stack_pop(returns);
    }
}

static void push_branch(bytewriter_t* writer, bool loop) {
    if (!branches) branches = stack_new();
    branch_t* branch = malloc(sizeof(branch_t));
    branch->end_stack = stack_new();
    branch->is_loop = loop;
    if (loop) branch->branch_start = bytewriter_size(writer);
    else branch->branch_start = stack_size(branches) == 0 ? 0 : ((branch_t*)stack_peek_ptr(branches))->branch_start;
    stack_push_ptr(branches, branch);
}

static void pop_branch(bytewriter_t* writer) {
    branch_t* branch = stack_pop_ptr(branches);
    branch_t* parent = stack_peek_ptr(branches);
    while (stack_size(branch->end_stack) > 0) {
        if (!branch->is_loop) {
            stack_push_int(parent->end_stack, stack_pop_int(branch->end_stack));
            continue;
        }
        int* ptr = (int*)(bytewriter_data(writer) + stack_peek_int(branch->end_stack));
        ptr[-1] = bytewriter_size(writer) - stack_peek_int(branch->end_stack);
        stack_pop(branch->end_stack);
    }
    stack_delete(branch->end_stack);
    free(branch);
}

static void set_jump(bytewriter_t* writer) {
    ((branch_t*)stack_peek_ptr(branches))->curr_jump = bytewriter_size(writer);
}

static void write_jump(bytewriter_t* writer) {
    size_t offset = ((branch_t*)stack_peek_ptr(branches))->curr_jump;
    int* ptr = (int*)(bytewriter_data(writer) + offset);
    ptr[-1] = bytewriter_size(writer) - offset;
}

static void jitc_asm_pushi(bytewriter_t* writer, uint64_t value, jitc_type_kind_t kind, bool is_unsigned) {
    pushi(writer, StackItem_literal, kind, is_unsigned, value);
}

static void jitc_asm_pushf(bytewriter_t* writer, float value) {
    pushf(writer, StackItem_literal, Type_Float32, false, value);
}

static void jitc_asm_pushd(bytewriter_t* writer, double value) {
    pushf(writer, StackItem_literal, Type_Float64, false, value);
}

static void jitc_asm_pop(bytewriter_t* writer) {
    pop(writer);
}

static void jitc_asm_load(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) {
    stack_item_t addr = pop(writer);
    stack_item_t* res = push(writer, StackItem_rvalue, Type_Pointer, true);
    if (addr.type != StackItem_rvalue) emit(writer, mov, 2, op(res), op(&addr));
    correct_kind(&kind, &is_unsigned);
    res->kind = kind;
    res->is_unsigned = is_unsigned;
    res->type = StackItem_lvalue_abs;
}

static void jitc_asm_laddr(bytewriter_t* writer, void* ptr, jitc_type_kind_t kind, bool is_unsigned) {
    emit(writer, mov, 2,
        unptr(op(push(writer, StackItem_lvalue_abs, kind, is_unsigned))),
        imm((uint64_t)ptr, Type_Pointer, true)
    );
}

static void jitc_asm_lstack(bytewriter_t* writer, int32_t offset, jitc_type_kind_t kind, bool is_unsigned) {
    stack_item_t* item = push(writer, StackItem_lvalue, kind, is_unsigned);
    item->offset = -offset;
}

static void jitc_asm_store(bytewriter_t* writer) {
    emit(writer, mov, 2, op(peek(1)), op(peek(0)));
    pop(writer);
}

static void jitc_asm_copy(bytewriter_t* writer, uint64_t size, uint64_t alignment) {
    stack_item_t src = pop(writer);
    stack_item_t* dst = peek(0);
    copy(writer, op(dst), op(&src), size, alignment);
}

static void jitc_asm_add(bytewriter_t* writer) {
    binaryop(writer, add);
}

static void jitc_asm_sub(bytewriter_t* writer) {
    binaryop(writer, sub);
}

static void jitc_asm_mul(bytewriter_t* writer) {
    if (isflt(peek(1)->kind)) binaryop(writer, imul);
    else arithcomplex(writer, imul, rax, false);
}

static void jitc_asm_div(bytewriter_t* writer) {
    if (isflt(peek(1)->kind)) binaryop(writer, idiv);
    else arithcomplex(writer, idiv, rax, false);
}

static void jitc_asm_mod(bytewriter_t* writer) {
    if (isflt(peek(1)->kind)) binaryop(writer, idiv);
    else arithcomplex(writer, idiv, rdx, false);
}

static void jitc_asm_and(bytewriter_t* writer) {
    binaryop(writer, and);
}

static void jitc_asm_or(bytewriter_t* writer) {
    binaryop(writer, or);
}

static void jitc_asm_xor(bytewriter_t* writer) {
    binaryop(writer, xor);
}

static void jitc_asm_shl(bytewriter_t* writer) {
    bitshift(writer, false, false);
}

static void jitc_asm_shr(bytewriter_t* writer) {
    bitshift(writer, false, true);
}

static void jitc_asm_sadd(bytewriter_t* writer) {
    emit(writer, add, 2, op(peek(1)), op(peek(0)));
    pop(writer);
}

static void jitc_asm_ssub(bytewriter_t* writer) {
    emit(writer, sub, 2, op(peek(1)), op(peek(0)));
    pop(writer);
}

static void jitc_asm_smul(bytewriter_t* writer) {
    if (isflt(peek(1)->kind)) {
        emit(writer, imul, 2, op(peek(1)), op(peek(0)));
        pop(writer);
    }
    arithcomplex(writer, imul, rax, true);
}

static void jitc_asm_sdiv(bytewriter_t* writer) {
    if (isflt(peek(1)->kind)) {
        emit(writer, idiv, 2, op(peek(1)), op(peek(0)));
        pop(writer);
    }
    arithcomplex(writer, idiv, rax, true);
}

static void jitc_asm_smod(bytewriter_t* writer) {
    if (isflt(peek(1)->kind)) {
        emit(writer, idiv, 2, op(peek(1)), op(peek(0)));
        pop(writer);
    }
    arithcomplex(writer, idiv, rdx, true);
}

static void jitc_asm_sand(bytewriter_t* writer) {
    emit(writer, and, 2, op(peek(1)), op(peek(0)));
    pop(writer);
}

static void jitc_asm_sor(bytewriter_t* writer) {
    emit(writer, or, 2, op(peek(1)), op(peek(0)));
    pop(writer);
}

static void jitc_asm_sxor(bytewriter_t* writer) {
    emit(writer, xor, 2, op(peek(1)), op(peek(0)));
    pop(writer);
}

static void jitc_asm_sshl(bytewriter_t* writer) {
    bitshift(writer, true, false);
}

static void jitc_asm_sshr(bytewriter_t* writer) {
    bitshift(writer, true, true);
}

static void jitc_asm_not(bytewriter_t* writer) {
    unaryop(writer, not, false);
}

static void jitc_asm_neg(bytewriter_t* writer) {
    stack_item_t* op1 = peek(0);
    if (isflt(op1->kind)) {
        emit(writer, imul, 2, op(op1), imm(*(uint64_t*)&(double){-1}, op1->kind, op1->is_unsigned));
        pop(writer);
    }
    else unaryop(writer, neg, false);
}

static void jitc_asm_inc(bytewriter_t* writer, bool suffix, int32_t step) {
    increment(writer, step, suffix);
}

static void jitc_asm_zero(bytewriter_t* writer) {
    compare_against(writer, sete, imm(0, peek(0)->kind, peek(0)->is_unsigned));
}

static void jitc_asm_addrof(bytewriter_t* writer) {
    stack_item_t item = pop(writer);
    operand_t op1 = op(&item);
    operand_t res = op(push(writer, StackItem_rvalue, Type_Pointer, true));
    op1.kind = Type_Pointer;
    op1.is_unsigned = true;
    if (item.type != StackItem_rvalue) emit(writer, lea, 2, res, op1);
}

static void jitc_asm_eql(bytewriter_t* writer) {
    compare(writer, sete);
}

static void jitc_asm_neq(bytewriter_t* writer) {
    compare(writer, setne);
}

static void jitc_asm_lst(bytewriter_t* writer) {
    compare(writer, setl);
}

static void jitc_asm_lte(bytewriter_t* writer) {
    compare(writer, setle);
}

static void jitc_asm_grt(bytewriter_t* writer) {
    compare(writer, setg);
}

static void jitc_asm_gte(bytewriter_t* writer) {
    compare(writer, setge);
}

static void jitc_asm_swp(bytewriter_t* writer) {
    stack_item_t tmp = *peek(0);
    *peek(0) = *peek(1);
    *peek(1) = tmp;
}

static void jitc_asm_rval(bytewriter_t* writer) {
    stack_item_t* item = peek(0);
    if (item->type != StackItem_rvalue) {
        pop(writer);
        operand_t op1 = op(item);
        item = push(writer, StackItem_rvalue, op1.kind, op1.is_unsigned);
        emit(writer, mov, 2, op(item), op1);
    }
}

static void jitc_asm_sc_begin(bytewriter_t* writer) {
    push_shortcircuit(writer);
}

static void jitc_asm_land(bytewriter_t* writer) {
    jitc_asm_rval(writer);
    stack_item_t item = pop(writer);
    emit(writer, cmp, 2, op(&item), imm(0, item.kind, item.is_unsigned));
    emit(writer, jz, 1, imm(0, Type_Int32, false));
    push_shortcircuit_jump(writer);
}

static void jitc_asm_lor(bytewriter_t* writer) {
    jitc_asm_rval(writer);
    stack_item_t item = pop(writer);
    emit(writer, cmp, 2, op(&item), imm(0, item.kind, item.is_unsigned));
    emit(writer, jnz, 1, imm(0, Type_Int32, false));
    push_shortcircuit_jump(writer);
}

static void jitc_asm_sc_end(bytewriter_t* writer) {
    pop_shortcircuit(writer);
    jitc_asm_rval(writer);
    stack_item_t item = pop(writer);
    stack_item_t* res = push(writer, StackItem_rvalue, Type_Int8, true);
    emit(writer, cmp, 2, op(&item), imm(0, item.kind, item.is_unsigned));
    emit(writer, setne, 1, op(res));
}

static void jitc_asm_cvt(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) {
    stack_item_t item = pop(writer);
    operand_t op1 = op(&item);
    operand_t res = op(push(writer, StackItem_rvalue, kind, is_unsigned));
    if (op1.kind == Type_Pointer) op1.kind = Type_Int64;
    if (res.kind == Type_Pointer) res.kind = Type_Int64;
    if (res.kind == Type_Float32) {
        if (op1.kind == Type_Float32) emit(writer, mov, 2, res, op1);
        else if (op1.kind == Type_Float64) emit(writer, cvtsd2ss, 2, res, op1);
        else if (op1.kind == Type_Int64 && op1.is_unsigned) {
            operand_t itmp = reg(rax, Type_Int64, true);
            operand_t ftmp = reg(xmm15, Type_Float64, true);
            emit(writer, mov, 2, itmp, op1);
            emit(writer, cvtsi2ss, 2, res, itmp);
            emit(writer, sar, 2, itmp, imm(63, Type_Int8, true));
            emit(writer, and, 2, itmp, imm(0x5F800000, Type_Float32, true));
            emit(writer, mov, 2, ftmp, itmp);
            emit(writer, add, 2, res, ftmp);
        }
        else {
            operand_t newop = op(push(writer, StackItem_rvalue, Type_Int32, false));
            if (op1.kind < Type_Int32) emit(writer, op1.is_unsigned ? movzx : movsx, 2, newop, op1);
            else emit(writer, mov, 2, newop, op1);
            emit(writer, cvtsi2ss, 2, res, newop);
            pop(writer);
        }
    }
    else if (res.kind == Type_Float64) {
        if (op1.kind == Type_Float32) emit(writer, cvtss2sd, 2, res, op1);
        else if (op1.kind == Type_Float64) emit(writer, mov, 2, res, op1);
        else if (op1.kind == Type_Int64 && op1.is_unsigned) {
            operand_t itmp = reg(rax, Type_Int64, true);
            operand_t ftmp = reg(xmm15, Type_Float64, true);
            emit(writer, mov, 2, itmp, op1);
            emit(writer, cvtsi2sd, 2, res, itmp);
            emit(writer, sar, 2, itmp, imm(63, Type_Int8, true));
            emit(writer, and, 2, itmp, imm(0x43F0000000000000, Type_Float64, true));
            emit(writer, mov, 2, ftmp, itmp);
            emit(writer, add, 2, res, ftmp);
        }
        else {
            operand_t newop = op(push(writer, StackItem_rvalue, Type_Int64, false));
            if (op1.kind < Type_Int64) emit(writer, op1.is_unsigned ? movzx : movsx, 2, newop, op1);
            else emit(writer, mov, 2, newop, op1);
            emit(writer, cvtsi2sd, 2, res, newop);
            pop(writer);
        }
    }
    else {
        if (op1.kind == Type_Float32) {
            if (res.kind < Type_Int32) res.kind = Type_Int32;
            emit(writer, cvttss2si, 2, res, op1);
        }
        else if (op1.kind == Type_Float64) {
            if (res.kind < Type_Int64) res.kind = Type_Int64;
            emit(writer, cvttsd2si, 2, res, op1);
        }
        else {
            if (op1.kind > res.kind) {
                op1.kind = res.kind;
                emit(writer, mov, 2, res, op1);
            }
            if (op1.kind < res.kind) emit(writer, op1.is_unsigned ? movzx : movsx, 2, res, op1);
        }
    }
}

static void jitc_asm_type(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) {
    correct_kind(&kind, &is_unsigned);
    peek(0)->kind = kind;
    peek(0)->is_unsigned = is_unsigned;
}

static stack_item_t* jitc_asm_stackalloc(bytewriter_t* writer, uint32_t bytes) {
    if (bytes % 8 != 0) bytes += 8 - (bytes % 8);
    stack_sub(writer, bytes);
    stack_item_t* item = push(writer, StackItem_rvalue, Type_Pointer, true);
    item->extra_storage = bytes;
    operand_t op1 = op(item);
    emit(writer, lea, 2, op1, ptr(rsp, op1.type == OpType_reg ? 0 : 8, Type_Pointer, true));
    item->type = StackItem_lvalue_abs;
    return item;
}

static void jitc_asm_offset(bytewriter_t* writer, int32_t off) {
    peek(0)->offset += off;
}

static void jitc_asm_normalize(bytewriter_t* writer, int32_t size) {
    if (peek(0)->type == StackItem_literal || peek(0)->type == StackItem_lvalue) jitc_asm_rval(writer);
    if (size == 0) return;
    mnemonic_t mnemonic = size < 0 ? shr : shl;
    size = abs(size);
    int amount = 0;
    while (size >>= 1) amount++;
    emit(writer, mnemonic, 2, op(peek(0)), imm(amount, Type_Int8, true));
}

static void jitc_asm_if(bytewriter_t* writer, bool loop) {
    push_branch(writer, loop);
}

static void jitc_asm_then(bytewriter_t* writer) {
    stack_item_t item = pop(writer);
    emit(writer, cmp, 2, op(&item), imm(0, item.kind, item.is_unsigned));
    emit(writer, jz, 1, imm(0, Type_Int32, true));
    set_jump(writer);
}

static void jitc_asm_else(bytewriter_t* writer) {
    emit(writer, jmp, 1, imm(0, Type_Int32, true));
    write_jump(writer);
    set_jump(writer);
}

static void jitc_asm_end(bytewriter_t* writer) {
    write_jump(writer);
    pop_branch(writer);
}

static void jitc_asm_goto_start(bytewriter_t* writer) {
    branch_t* branch = stack_peek_ptr(branches);
    emit(writer, jmp, 1, imm(0, Type_Int32, false));
    ((int32_t*)(bytewriter_data(writer) + bytewriter_size(writer)))[-1] = branch->branch_start - bytewriter_size(writer);
}

static void jitc_asm_goto_end(bytewriter_t* writer) {
    emit(writer, jmp, 1, imm(0, Type_Int32, false));
    branch_t* branch = stack_peek_ptr(branches);
    stack_push_int(branch->end_stack, bytewriter_size(writer));
}
