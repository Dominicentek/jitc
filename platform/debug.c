#include "../arch.h"

static const char* str_kind[] = { "int8", "int16", "int32", "int64", "float32", "float64", "ptr" };
static const char* str_bool[] = { "", "u" };

void jitc_asm_pushi(bytewriter_t* writer, uint64_t value, jitc_type_kind_t kind, bool is_unsigned) {
    printf(is_unsigned ? "pushi %lu %s%s\n" : "pushi %ld %s%s\n", value, str_bool[is_unsigned], str_kind[kind]);
}

void jitc_asm_pushf(bytewriter_t* writer, float value) {
    printf("pushf %f\n", value);
}

void jitc_asm_pushd(bytewriter_t* writer, double value) {
    printf("pushd %f\n", value);
}

void jitc_asm_pop(bytewriter_t* writer) {
    printf("pop\n");
}

void jitc_asm_dup(bytewriter_t* writer) {
    printf("dup\n");
}

void jitc_asm_load(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) {
    printf("load %s%s\n", str_bool[is_unsigned], str_kind[kind]);
}

void jitc_asm_lstack(bytewriter_t* writer, int32_t offset, jitc_type_kind_t kind, bool is_unsigned) {
    printf("lstack %d %s%s\n", offset, str_bool[is_unsigned], str_kind[kind]);
}

void jitc_asm_store(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_add(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_sub(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_mul(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_div(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_mod(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_shl(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_shr(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_and(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_or(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_xor(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_not(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_neg(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_inc(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_dec(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_zero(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_addrof(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_eql(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_neq(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_lst(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_lte(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_grt(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_gte(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_swp(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_cvt(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) {
    printf("cvt %s%s\n", str_bool[is_unsigned], str_kind[kind]);
}

void jitc_asm_if(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_then(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_else(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_end(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_goto_start(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_goto_end(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }

void jitc_asm_call(bytewriter_t* writer, jitc_type_t* signature)  { printf("%s\n", __FUNCTION__ + 9); }
void jitc_asm_ret(bytewriter_t* writer) { printf("%s\n", __FUNCTION__ + 9); }

void jitc_asm_func(bytewriter_t* writer, jitc_type_t* signature, size_t stack_size) { printf("%s %lu\n", __FUNCTION__ + 9, stack_size); }
void jitc_asm_func_end(bytewriter_t* writer)  { printf("%s\n", __FUNCTION__ + 9); }
