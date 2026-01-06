#include "../jitc_internal.h"

#include "unix.c"

static void jitc_asm_pushi(bytewriter_t* writer, uint64_t value, jitc_type_kind_t kind, bool is_unsigned) { printf("pushi\n"); }
static void jitc_asm_pushf(bytewriter_t* writer, float value) { printf("pushf\n"); }
static void jitc_asm_pushd(bytewriter_t* writer, double value) { printf("pushd\n"); }
static void jitc_asm_pop(bytewriter_t* writer) { printf("pop\n"); }
static void jitc_asm_load(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) { printf("load\n"); }
static void jitc_asm_laddr(bytewriter_t* writer, jitc_variable_t* var, jitc_type_kind_t kind, bool is_unsigned) { printf("laddr\n"); }
static void jitc_asm_lstack(bytewriter_t* writer, int32_t offset, jitc_type_kind_t kind, bool is_unsigned) { printf("lstack\n"); }
static void jitc_asm_store(bytewriter_t* writer) { printf("store\n"); }
static void jitc_asm_copy(bytewriter_t* writer, uint64_t size, uint64_t alignment) { printf("copy\n"); }
static void jitc_asm_add(bytewriter_t* writer) { printf("add\n"); }
static void jitc_asm_sub(bytewriter_t* writer) { printf("sub\n"); }
static void jitc_asm_mul(bytewriter_t* writer) { printf("mul\n"); }
static void jitc_asm_div(bytewriter_t* writer) { printf("div\n"); }
static void jitc_asm_mod(bytewriter_t* writer) { printf("mod\n"); }
static void jitc_asm_and(bytewriter_t* writer) { printf("and\n"); }
static void jitc_asm_or(bytewriter_t* writer) { printf("or\n"); }
static void jitc_asm_xor(bytewriter_t* writer) { printf("xor\n"); }
static void jitc_asm_shl(bytewriter_t* writer) { printf("shl\n"); }
static void jitc_asm_shr(bytewriter_t* writer) { printf("shr\n"); }
static void jitc_asm_sadd(bytewriter_t* writer) { printf("sadd\n"); }
static void jitc_asm_ssub(bytewriter_t* writer) { printf("ssub\n"); }
static void jitc_asm_smul(bytewriter_t* writer) { printf("smul\n"); }
static void jitc_asm_sdiv(bytewriter_t* writer) { printf("sdiv\n"); }
static void jitc_asm_smod(bytewriter_t* writer) { printf("smod\n"); }
static void jitc_asm_sand(bytewriter_t* writer) { printf("sand\n"); }
static void jitc_asm_sor(bytewriter_t* writer) { printf("sor\n"); }
static void jitc_asm_sxor(bytewriter_t* writer) { printf("sxor\n"); }
static void jitc_asm_sshl(bytewriter_t* writer) { printf("sshl\n"); }
static void jitc_asm_sshr(bytewriter_t* writer) { printf("sshr\n"); }
static void jitc_asm_not(bytewriter_t* writer) { printf("not\n"); }
static void jitc_asm_neg(bytewriter_t* writer) { printf("neg\n"); }
static void jitc_asm_inc(bytewriter_t* writer, bool suffix, int32_t step) { printf("inc\n"); }
static void jitc_asm_zero(bytewriter_t* writer) { printf("zero\n"); }
static void jitc_asm_addrof(bytewriter_t* writer) { printf("addrof\n"); }
static void jitc_asm_eql(bytewriter_t* writer) { printf("eql\n"); }
static void jitc_asm_neq(bytewriter_t* writer) { printf("neq\n"); }
static void jitc_asm_lst(bytewriter_t* writer) { printf("lst\n"); }
static void jitc_asm_lte(bytewriter_t* writer) { printf("lte\n"); }
static void jitc_asm_grt(bytewriter_t* writer) { printf("grt\n"); }
static void jitc_asm_gte(bytewriter_t* writer) { printf("gte\n"); }
static void jitc_asm_swp(bytewriter_t* writer) { printf("swp\n"); }
static void jitc_asm_rval(bytewriter_t* writer) { printf("rval\n"); }
static void jitc_asm_sc_begin(bytewriter_t* writer) { printf("sc_begin\n"); }
static void jitc_asm_land(bytewriter_t* writer) { printf("land\n"); }
static void jitc_asm_lor(bytewriter_t* writer) { printf("lor\n"); }
static void jitc_asm_sc_end(bytewriter_t* writer) { printf("sc_end\n"); }
static void jitc_asm_cvt(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) { printf("cvt\n"); }
static void jitc_asm_type(bytewriter_t* writer, jitc_type_kind_t kind, bool is_unsigned) { printf("type\n"); }
static void jitc_asm_stackalloc(bytewriter_t* writer, uint32_t bytes) { printf("stackalloc\n"); }
static void jitc_asm_offset(bytewriter_t* writer, int32_t off) { printf("offset\n"); }
static void jitc_asm_normalize(bytewriter_t* writer, int32_t size) { printf("normalize\n"); }
static void jitc_asm_if(bytewriter_t* writer, bool loop) { printf("if\n"); }
static void jitc_asm_then(bytewriter_t* writer) { printf("then\n"); }
static void jitc_asm_else(bytewriter_t* writer) { printf("else\n"); }
static void jitc_asm_end(bytewriter_t* writer) { printf("end\n"); }
static void jitc_asm_goto_start(bytewriter_t* writer) { printf("goto_start\n"); }
static void jitc_asm_goto_end(bytewriter_t* writer) { printf("goto_end\n"); }
static void jitc_asm_call(bytewriter_t* writer, jitc_type_t* signature, jitc_type_t** arg_types, size_t num_args) { printf("call\n"); }
static void jitc_asm_func(bytewriter_t* writer, jitc_type_t* signature, size_t stack_size) { printf("func\n"); }
static void jitc_asm_ret(bytewriter_t* writer) { printf("ret\n"); }
static void jitc_asm_func_end(bytewriter_t* writer) { printf("func_end\n"); }
