#include "arch.h"

#include "../jitc_internal.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// TODO: floating point

typedef enum: uint8_t {
    REX = 0b01000000,
    REX_W = REX | (1 << 3),
    REX_R = REX | (1 << 2),
    REX_X = REX | (1 << 1),
    REX_B = REX | (1 << 0),
} rex_t;

typedef enum: uint8_t {
    rax, rcx, rdx, rbx,
    rsp, rbp, rsi, rdi,
    r8,  r9,  r10, r11,
    r12, r13, r14, r15
} reg_t;

typedef enum: uint16_t {
    add = 0x03,
    sub = 0x2B,
    and = 0x23,
    or = 0x0B,
    xor = 0x33,
    cmp = 0x3B
} alu_t;

typedef enum: uint8_t {
    jz = 0x84,
    jnz = 0x85,
    jg = 0x8F,
    jge = 0x8D
} jcond_t;

typedef enum: uint8_t {
    movtype_no_offset = 0b00,
    movtype_offset8 = 0b01,
    movtype_offset32 = 0b10,
    movtype_copy = 0b11,

    movtype_load = 0b100
} movtype_t;

static void header(bytewriter_t* writer, jitc_width_t width, reg_t dst, reg_t src) {
    uint8_t rex = 0;
    if (dst & 0b1000) rex |= REX_B;
    if (src & 0b1000) rex |= REX_R;
    if (width == W64) rex |= REX_W;
    if (width == W16) bytewriter_int8(writer, 0x66);
    if (rex != 0) bytewriter_int8(writer, rex);
}

static void modrm(bytewriter_t* writer, movtype_t movtype, reg_t a, reg_t b) {
    bytewriter_int8(writer, ((movtype & 0b11) << 6) | ((a & 0b111) << 3) | (b & 0b111));
}

static void mov_imm(bytewriter_t* writer, reg_t reg, uint64_t imm, jitc_width_t width) {
    header(writer, width, reg, 0);
    bytewriter_int8(writer, (width == W8 ? 0xB0 : 0xB8) | (reg & 0b111));
    if (width == W8)  bytewriter_int8 (writer, imm);
    if (width == W16) bytewriter_int16(writer, imm);
    if (width == W32) bytewriter_int32(writer, imm);
    if (width == W64) bytewriter_int64(writer, imm);
}

static void mov_reg(bytewriter_t* writer, reg_t from, reg_t to, movtype_t movtype, int32_t offset, jitc_width_t width) {
    if (((movtype & movtype_load ? from : to) & 0b111) == 0b101) movtype |= movtype_offset8;
    header(writer, width, to, from);
    bytewriter_int8(writer, (width == W8 ? 0x8A : 0x8B) | (movtype & movtype_load ? 0b10 : 0));
    modrm(writer, movtype, from, to);
    if ((movtype & 0b11) == movtype_offset8) bytewriter_int8(writer, offset);
    if ((movtype & 0b11) == movtype_offset32) bytewriter_int32(writer, offset);
}

static void movex(bytewriter_t* writer, reg_t reg, jitc_width_t from, jitc_width_t to, bool signedness) {
    if (from == W32 && to == W64 && !signedness) {
        mov_reg(writer, reg, reg, movtype_copy, 0, W32);
        return;
    }
    header(writer, to, reg, reg);
    if (from == W32 && to == W64) bytewriter_int8(writer, 0x63);
    else {
        bytewriter_int8(writer, 0x0F);
        bytewriter_int8(writer, 0xB6 | (signedness << 3) | (from == W8 ? 0 : 1));
    }
    modrm(writer, movtype_copy, reg, reg);
}

static void alu(bytewriter_t* writer, reg_t result, reg_t value, alu_t opcode, jitc_width_t width) {
    header(writer, width, result, value);
    bytewriter_int8(writer, opcode & (width == W8 ? 0xFE : 0xFF));
    modrm(writer, movtype_copy, result, value);
}

static void imul8(bytewriter_t* writer, reg_t reg) {
    header(writer, W8, reg, 0);
    bytewriter_int8(writer, 0xF6);
    modrm(writer, movtype_copy, 0b101, reg);
}

static void imul(bytewriter_t* writer, reg_t result, reg_t value, jitc_width_t width) {
    header(writer, width, result, value);
    bytewriter_int8(writer, 0x0F); bytewriter_int8(writer, 0xAF);
    modrm(writer, movtype_copy, result, value);
}

static void unary(bytewriter_t* writer, reg_t reg, uint8_t opcode, jitc_width_t width) {
    header(writer, width, reg, 0);
    bytewriter_int8(writer, opcode & (width == W8 ? 0xFE : 0xFF));
}

static void idiv(bytewriter_t* writer, reg_t reg, jitc_width_t width) {
    unary(writer, reg, 0xF7, width);
    modrm(writer, movtype_copy, 0b111, reg);
}

static void inc(bytewriter_t* writer, reg_t reg, bool decrement, jitc_width_t width) {
    unary(writer, reg, 0xFF, width);
    modrm(writer, movtype_copy, decrement, reg);
}

static void not(bytewriter_t* writer, reg_t reg, jitc_width_t width) {
    unary(writer, reg, 0xF7, width);
    modrm(writer, movtype_copy, 0b010, reg);
}

static void shift(bytewriter_t* writer, reg_t reg, bool right, jitc_width_t width) {
    unary(writer, reg, 0xD1, width);
    modrm(writer, movtype_copy, 0b100 | right, reg);
}

static void signextend(bytewriter_t* writer, jitc_width_t width) {
    if (width <= W16) bytewriter_int8(writer, 0x66);
    if (width == W64) bytewriter_int8(writer, 0x48);
    bytewriter_int8(writer, width == W8 ? 0x98 : 0x99);
}

static void setne(bytewriter_t* writer, reg_t reg) {
    header(writer, W8, reg, 0);
    bytewriter_int8(writer, 0x0F);
    bytewriter_int8(writer, 0x95);
    modrm(writer, movtype_copy, 0, reg);
}

static void push(bytewriter_t* writer, reg_t reg) {
    header(writer, W32, reg, 0);
    bytewriter_int8(writer, 0x50 | (reg & 0b111));
}

static void pop(bytewriter_t* writer, reg_t reg) {
    header(writer, W32, reg, 0);
    bytewriter_int8(writer, 0x58 | (reg & 0b111));
}

static void jmp(bytewriter_t* writer, int32_t offset) {
    bytewriter_int8(writer, 0xE9);
    bytewriter_int32(writer, offset);
}

static void jcond(bytewriter_t* writer, jcond_t cond, int32_t offset) {
    bytewriter_int8(writer, 0x0F);
    bytewriter_int8(writer, cond);
    bytewriter_int32(writer, offset);
}

static void call(bytewriter_t* writer, reg_t reg) {
    header(writer, W32, reg, 0);
    bytewriter_int8(writer, 0xFF);
    modrm(writer, movtype_copy, 0b010, reg);
}

static void ret(bytewriter_t* writer) {
    bytewriter_int8(writer, 0xC3);
}

static void stackalloc(bytewriter_t* writer, size_t bytes, bool alloc) {
    header(writer, W64, rsp, 0);
    bytewriter_int8(writer, 0x81);
    modrm(writer, movtype_copy, 0b100 | alloc, rsp);
    bytewriter_int32(writer, bytes);
}

static uint8_t reg(jitc_reg_t reg) {
    switch (reg) {
        case R0: return r12;
        case R1: return r13;
        case R2: return r14;
    }
    return 0;
}

void jitc_asm_imm(bytewriter_t* writer, jitc_reg_t to, uint64_t from, jitc_width_t width) {
    mov_imm(writer, reg(to), from, width);
}

void jitc_asm_reg(bytewriter_t* writer, jitc_reg_t to, jitc_reg_t from, jitc_width_t to_width, jitc_width_t from_width, bool signedness) {
    mov_reg(writer, reg(from), reg(to), movtype_copy, 0, min(from_width, to_width));
    if (from_width < to_width) movex(writer, reg(to), from_width, to_width, signedness);
}

void jitc_asm_ld(bytewriter_t* writer, jitc_reg_t to, jitc_reg_t from, jitc_width_t width) {
    mov_reg(writer, reg(from), reg(to), movtype_no_offset | movtype_load, 0, width);
}

void jitc_asm_st(bytewriter_t* writer, jitc_reg_t to, jitc_reg_t from, jitc_width_t width) {
    mov_reg(writer, reg(from), reg(to), movtype_no_offset, 0, width);
}

void jitc_asm_ls(bytewriter_t* writer, jitc_reg_t to, int32_t offset, jitc_width_t width) {
    mov_reg(writer, rbp, reg(to), movtype_offset32 | movtype_load, offset, width);
}

void jitc_asm_ss(bytewriter_t* writer, int32_t offset, jitc_reg_t from, jitc_width_t width) {
    mov_reg(writer, reg(from), rbp, movtype_offset32, offset, width);
}

void jitc_asm_add(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    alu(writer, reg(result), reg(value), add, width);
}

void jitc_asm_sub(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    alu(writer, reg(result), reg(value), sub, width);
}

void jitc_asm_mul(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    if (width == W8) {
        mov_reg(writer, reg(result), rax, movtype_copy, 0, W8);
        imul8(writer, reg(value));
        mov_reg(writer, rax, reg(result), movtype_copy, 0, W8);
    }
    else imul(writer, reg(result), reg(value), width);
}

void jitc_asm_div(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    mov_reg(writer, reg(result), rax, movtype_copy, 0, width);
    if (width != W64) movex(writer, rax, width, W64, false);
    signextend(writer, width);
    idiv(writer, reg(value), width);
    mov_reg(writer, rax, reg(result), movtype_copy, 0, width);
}

void jitc_asm_mod(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    mov_reg(writer, reg(result), rax, movtype_copy, 0, width);
    if (width != W64) movex(writer, rax, width, W64, false);
    signextend(writer, width);
    idiv(writer, reg(value), width);
    mov_reg(writer, rdx, reg(result), movtype_copy, 0, width);
}

void jitc_asm_shl(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    mov_reg(writer, reg(value), rcx, movtype_copy, 0, width);
    shift(writer, reg(result), false, width);
}

void jitc_asm_shr(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    mov_reg(writer, reg(value), rcx, movtype_copy, 0, width);
    shift(writer, reg(result), false, width);
}

void jitc_asm_and(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    alu(writer, reg(result), reg(value), and, width);
}

void jitc_asm_or(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    alu(writer, reg(result), reg(value), or, width);
}

void jitc_asm_xor(bytewriter_t* writer, jitc_reg_t result, jitc_reg_t value, jitc_width_t width) {
    alu(writer, reg(result), reg(value), xor, width);
}

void jitc_asm_not(bytewriter_t* writer, jitc_reg_t value, jitc_width_t width) {
    not(writer, reg(value), width);
}

void jitc_asm_inc(bytewriter_t* writer, jitc_reg_t value, jitc_width_t width) {
    inc(writer, reg(value), false, width);
}

void jitc_asm_dec(bytewriter_t* writer, jitc_reg_t value, jitc_width_t width) {
    inc(writer, reg(value), true, width);
}

void jitc_asm_nzero(bytewriter_t* writer, jitc_reg_t value, jitc_width_t width) {
    alu(writer, reg(value), reg(value), cmp, width);
    setne(writer, reg(value));
    movex(writer, reg(value), W8, width, false);
}

void jitc_asm_cmp(bytewriter_t* writer, jitc_reg_t a, jitc_reg_t b, jitc_width_t width) {
    alu(writer, reg(a), reg(b), cmp, width);
}

void jitc_asm_jmp(bytewriter_t* writer, int32_t offset) {
    jmp(writer, offset);
}

void jitc_asm_jz(bytewriter_t* writer, int32_t offset) {
    jcond(writer, jz, offset);
}

void jitc_asm_jnz(bytewriter_t* writer, int32_t offset) {
    jcond(writer, jnz, offset);
}

void jitc_asm_jpos(bytewriter_t* writer, int32_t offset) {
    jcond(writer, jg, offset);
}

void jitc_asm_jzpos(bytewriter_t* writer, int32_t offset) {
    jcond(writer, jge, offset);
}

void jitc_asm_func(bytewriter_t* writer, size_t stack_size) {
    push(writer, r12);
    push(writer, r13);
    push(writer, r14);
    push(writer, rbp);
    mov_reg(writer, rbp, rsp, movtype_copy, 0, W64);
    if (stack_size % 16 != 0) stack_size += 16 - (stack_size % 16);
    stackalloc(writer, stack_size, true);
}

void jitc_asm_ret(bytewriter_t* writer, jitc_reg_t retvalue) {
    mov_reg(writer, rax, reg(retvalue), movtype_copy, 0, W64);
    mov_reg(writer, rsp, rbp, movtype_copy, 0, W64);
    pop(writer, rbp);
    pop(writer, r14);
    pop(writer, r13);
    pop(writer, r12);
    ret(writer);
}

void jitc_asm_arg(bytewriter_t* writer, jitc_reg_t value, jitc_width_t width, bool signedness) {
    if (width != W64) movex(writer, reg(value), width, W64, signedness);
    push(writer, reg(value));
}

void jitc_asm_call(bytewriter_t* writer, jitc_reg_t ret, jitc_reg_t addr, jitc_type_t* signature) {
    // TODO: implement struct-by-value and floating point
    for (size_t i = 0; i < signature->func.num_params; i++) {
        if (i < 6) pop(writer, (reg_t[]){ rdi, rsi, rdx, rcx, r8, r9 }[i]);
    }
    call(writer, reg(addr));
    mov_reg(writer, reg(ret), rax, movtype_copy, 0, W64);
    if (signature->func.num_params > 6) stackalloc(writer, (signature->func.num_params - 6) * 8, false);
}
