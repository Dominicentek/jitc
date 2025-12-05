#ifdef _WIN32
#include "arch/win-x86_64.c"
#elif __aarch64__
#include "arch/sysv-aarch64.c"
#else
#include "arch/sysv-x86_64.c"
#endif

bytewriter_t* jitc_generate_function(jitc_context_t* context, jitc_ast_t* ast) {
    smartptr(bytewriter_t) writer = bytewriter_new();

    jitc_asm_func(writer, 0);
    jitc_asm_imm(writer, R0, 1, W32);
    jitc_asm_imm(writer, R1, 2, W32);
    jitc_asm_add(writer, R0, R1, W32);
    jitc_asm_ret(writer, R0);

    return move(writer);
}
