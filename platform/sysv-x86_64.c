#include "x86_64.c"

// holy FUCK this is some of the worst code ive ever written isftg
// prepare thyself if youre reading this (minos prime ultrakill reference)

typedef enum: uint8_t {
    ABIClass_INTEGER,
    ABIClass_FLOATING,
    ABIClass_MEMORY
} abi_class_t;

typedef struct {
    abi_class_t class, class_upper;
    bool is_big, is_128bit;
    size_t stack_offset;
    jitc_type_t* type;
} abi_arg_t;

typedef union {
    struct {
        jitc_type_kind_t kind;
        uint16_t size, offset;
    };
    uint64_t packed;
} abi_primitive_t;

static void append_primitives(list_t* list, jitc_type_t* type, size_t offset) {
    if (type->kind == Type_Struct || type->kind == Type_Union) for (size_t i = 0; i < type->str.num_fields; i++) {
        append_primitives(list, type->str.fields[i], offset + type->str.offsets[i]);
    }
    else list_add_int(list, (abi_primitive_t){
        .kind = type->kind, .size = type->size, .offset = offset
    }.packed);
}

static abi_arg_t classify(jitc_type_t* type, int* int_params, int* float_params, int* stack_params) {
    if (type->size > 16) {
        if (*int_params < 6) (*int_params)++;
        else (*stack_params)++;
        return (abi_arg_t){
            .class = ABIClass_MEMORY,
            .type = type,
            .is_big = true
        };
    }
    abi_arg_t arg = {
        .class = ABIClass_FLOATING, .class_upper = ABIClass_FLOATING,
        .type = type,
        .is_128bit = type->size > 8
    };
    smartptr(list_t) primitives = list_new();
    append_primitives(primitives, type, 0);
    for (size_t i = 0; i < list_size(primitives); i++) {
        abi_primitive_t primitive = (abi_primitive_t){ .packed = list_get_int(primitives, i) };
        if (!isflt(primitive.kind)) *(primitive.offset >= 8 ? &arg.class_upper : &arg.class) = ABIClass_INTEGER; 
    }
    int* counter = arg.class == ABIClass_FLOATING ? float_params : int_params;
    if (*counter >= (arg.class == ABIClass_FLOATING ? 8 : 6)) {
        arg.class = ABIClass_MEMORY;
        counter = stack_params;
    }
    (*counter)++;
    if (arg.is_128bit) {
        int* counter = arg.class_upper == ABIClass_FLOATING ? float_params : int_params;
        if (*counter >= (arg.class_upper == ABIClass_FLOATING ? 8 : 6)) {
            arg.class_upper = ABIClass_MEMORY;
            counter = stack_params;
        }
        (*counter)++;
    }
    if (arg.class_upper == ABIClass_MEMORY) {
        arg.class = ABIClass_MEMORY;
        (*counter)--; (*stack_params)++;
    }
    return arg;
}

static void jitc_asm_call(bytewriter_t* writer, jitc_type_t* signature) {
    stack_item_t func = pop();

    // classify
    int int_params = 0, float_params = 0, stack_params = 0, stack_size = 0;
    abi_arg_t args[signature->func.num_params + 1];
    args[0] = classify(signature->func.ret, &int_params, &float_params, &stack_params);
    int_params = float_params = stack_params = 0;
    if (args[0].is_big) int_params = 1;
    for (size_t i = 0; i < signature->func.num_params; i++) {
        args[i + 1] = classify(signature->func.params[i], &int_params, &float_params, &stack_params);
    }
    
    // allocate stack
    if (signature->func.params[signature->func.num_params - 1]->kind == Type_Varargs) stack_size += 8;
    for (size_t i = 0; i < signature->func.num_params + 1; i++) {
        if (!args[i].is_big) continue;
        if (stack_size % args[i].type->alignment) stack_size += args[i].type->alignment - (stack_size % args[i].type->alignment);
        args[i].stack_offset = stack_size;
        stack_size += args[i].type->size;
    }
    stack_size += stack_params * 8;
    if ((stack_size + stack_bytes) % 16 != 0) stack_size += 16 - ((stack_size + stack_bytes) % 16);
    if (stack_size != 0) stack_sub(stack_size);
    
    // copy shit onto stack
    for (size_t i = 1; i < signature->func.num_params + 1; i++) {
        if (!args[i].is_big) continue;
        stack_item_t* item = peek(i - 1);
        copy(ptr(rsp, stack_size - args[i].stack_offset - args[i].type->size, Type_Int64, true), op(item), args[i].type->size, args[i].type->alignment);
    }
    
    // copy args
    int_params = float_params = 0;
    if (args[0].is_big) {
        instr2("lea", reg(rdi, Type_Int64, true), ptr(rsp, stack_size - args[0].stack_offset - args[0].type->size, Type_Int64, true));
        int_params = 1;
    }
    for (size_t i = 1; i < signature->func.num_params + 1; i++) {
        stack_item_t item = pop();
        for (size_t j = 0; j <= args[i].is_128bit; j++) {
            abi_class_t class = (&args[i].class)[j];
            if (j != 0) item.offset += 8;
            operand_t op1 = args[i].is_big ? ptr(rsp, stack_size - args[i].stack_offset - args[i].type->size, Type_Int64, true) : op(&item);
            const char* opcode = args[i].is_big ? "lea" : "mov";
            if (class == ABIClass_FLOATING && float_params < 8) instr2(opcode, reg((reg_t[]){
                xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8
            }[float_params++], item.kind, item.is_unsigned), op1);
            else if (int_params < 6) instr2(opcode, reg((reg_t[]){
                rdi, rsi, rdx, rcx, r8, r9
            }[int_params++], item.kind, item.is_unsigned), op1);
            else instr2(opcode, ptr(rsp, --stack_params * 8, item.kind, item.is_unsigned), op1);
        }
    }

    // call the function
    operand_t func_op = reg(rax, Type_Pointer, true);
    if (signature->func.params[signature->func.num_params - 1]->kind == Type_Varargs)
        func_op = ptr(rsp, stack_size - 8, Type_Pointer, true);
    instr2("lea", func_op, op(&func));
    instr1("call", func_op, false);
    if (stack_size != 0) stack_free(stack_size);

    // return value
    stack_item_t* ret;
    if (args[0].type->kind == Type_Struct || args[0].type->kind == Type_Union) {
        ret = jitc_asm_stackalloc(args[0].type->size);
        if (!args[0].is_big) {
            instr2("mov", op(ret), reg(rax, args[0].class == ABIClass_FLOATING ? Type_Float64 : Type_Int64, true));
            if (args[0].is_128bit) {
                reg_t dst = args[0].class == args[0].class_upper
                    ? args[0].class_upper == ABIClass_FLOATING
                        ? xmm1 : rdx
                    : args[0].class_upper == ABIClass_FLOATING
                        ? xmm0 : rax;
                ret->offset += 8;
                instr2("mov", op(ret), reg(dst, args[0].class_upper == ABIClass_FLOATING ? Type_Float64 : Type_Int64, true));
                ret->offset -= 8;
            }
        }
    }
    else if (args[0].type->kind != Type_Void) {
        ret = push(StackItem_rvalue, signature->func.ret->kind, signature->func.ret->is_unsigned);
        if (isflt(ret->kind))
            instr2("mov", op(ret), reg(xmm0, ret->kind, false));
        else
            instr2("mov", op(ret), reg(rax, ret->kind, ret->is_unsigned));
    }
}

static jitc_type_t* func_signature = NULL;

static void jitc_asm_func(bytewriter_t* writer, jitc_type_t* signature, size_t stack_size) {
    func_signature = signature;
    printf("push rbp\n"); // todo: preserve rbx and r12..r15
    printf("mov rbp, rsp\n");
    if (stack_size % 16 != 0) stack_size += 16 - (stack_size % 16);
    if (stack_size != 0) printf("sub rsp, 0x%lx\n", stack_size);
}

static void jitc_asm_ret(bytewriter_t* writer) {
    if (isflt(func_signature->func.ret->kind))
        instr2("mov", reg(xmm0, func_signature->func.ret->kind, false), op(peek(0)));
    else if (func_signature->func.ret->kind != Type_Void)
        instr2("mov", reg(rax, func_signature->func.ret->kind, func_signature->func.ret->is_unsigned), op(peek(0)));
    pop();
    printf("jmp _ret\n");
}

static void jitc_asm_func_end(bytewriter_t* writer) {
    printf("_ret:\n");
    printf("leave\n");
    printf("ret\n");
}
