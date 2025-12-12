#include "../jitc_internal.h"

static const char* opcode_names[] = {
#define NAME(x, ...) #x,
OPCODES(NAME)
};

static void* jitc_assemble(list_t* list) {
    for (size_t i = 0; i < list_size(list); i++) {
        jitc_ir_t* ir = list_get_ptr(list, i);
        printf("%s", opcode_names[ir->opcode]);
        for (int i = 0; i < args[ir->opcode].num_args; i++) switch(args[ir->opcode].arg_type[i]) {
            case ArgType_Int: printf(" %lu", ir->params[i].as_integer); break;
            case ArgType_Float: printf(" %f", ir->params[i].as_float); break;
            case ArgType_Pointer: printf(" 0x%016lx", ir->params[i].as_integer); break;
        }
        printf("\n");
    }
}
