#include "x86_64.c"

#include <windows.h>

static void protect_rw(void* ptr, size_t size) {
    DWORD old;
    VirtualProtect(ptr, size, PAGE_READWRITE, &old);
}

static void protect_rx(void* ptr, size_t size) {
    DWORD old;
    VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old);
}

static size_t page_size() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

static void* alloc_page(size_t size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

static void free_page(void* ptr, size_t size) {
    VirtualFree(ptr, size, MEM_RELEASE);
}

static void jitc_asm_call(bytewriter_t* writer, jitc_type_t* signature, jitc_type_t** arg_types, size_t num_args) {
    stack_item_t func = pop(writer);

    // allocate stack
    int stack_size = 0;
    int stack_offset[num_args];
    if (signature->func.ret->size) stack_size += signature->func.ret->size;
    for (size_t i = 0; i < num_args; i++) {
        if (arg_types[i]->size <= 8) continue;
        int align = arg_types[i]->alignment;
        if (stack_size % align != 0) stack_size += align - (stack_size % align);
        stack_offset[i] = stack_size;
        stack_size += arg_types[i]->size;
    }
    int reg_args = signature->func.ret->size > 8 ? 3 : 4;
    if (num_args > reg_args) stack_size += (num_args - reg_args) * 8;
    if (stack_size % 16 != 0) stack_size += 16 - (stack_size % 16);
    if (stack_size != 0) stack_sub(writer, stack_size);

    // copy shit onto stack
    for (size_t i = 0; i < num_args; i++) {
        if (arg_types[i]->size <= 8) continue;
        stack_item_t* item = peek(i - 1);
        copy(writer, ptr(rsp, stack_offset[i] - stack_size, Type_Int64, true), op(item), arg_types[i]->size, arg_types[i]->alignment);
    }

    // copy args
    for (size_t i = 0; i < num_args; i++) {
        stack_item_t item = pop(writer);
        operand_t arg = op(&item);
        operand_t dst = i < 4
            ? reg((isflt(arg.kind)
                ? (reg_t[]){ xmm0, xmm1, xmm2, xmm3 }
                : (reg_t[]){ rcx, rdx, r8, r9 }
            )[i], arg.kind, arg.is_unsigned)
            : ptr(rsp, stack_size - stack_offset[i] - arg_types[i]->size, Type_Int64, true);
        emit(writer, arg_types[i]->size > 8 ? lea : mov, 2, dst, arg);
    }

    // call the function
    emit(writer, func.type == StackItem_lvalue_abs ? lea : mov, 2, reg(rax, Type_Int64, true), op(&func));
    emit(writer, call, 1, op(&func));
    if (stack_size != 0) stack_free(writer, stack_size);

    // return value
    stack_item_t* ret;
    jitc_type_t* ret_type = signature->func.ret;
    if (ret_type->kind == Type_Struct || ret_type->kind == Type_Union) {
        ret = jitc_asm_stackalloc(writer, ret_type->size);
        if (ret_type->size <= 8) emit(writer, mov, 2, op(ret), reg(rax, Type_Int64, true));
        else copy(writer, op(ret), ptr(rsp, -stack_size, Type_Int64, true), ret_type->size, ret_type->alignment);
    }
    else if (ret_type->kind != Type_Void) {
        ret = push(writer, StackItem_rvalue, signature->func.ret->kind, signature->func.ret->is_unsigned);
        if (isflt(ret->kind))
            emit(writer, mov, 2, op(ret), reg(xmm0, ret->kind, false));
        else
            emit(writer, mov, 2, op(ret), reg(rax, ret->kind, ret->is_unsigned));
    }
    else ret = pushi(writer, StackItem_literal, Type_Int32, false, 0);
}

static jitc_type_t* func_signature = NULL;

static void jitc_asm_func(bytewriter_t* writer, jitc_type_t* signature, size_t stack_size) {
    clear_labels();
    func_signature = signature;
    emit(writer, opc_push, 1, reg(rbx, Type_Int64, true));
    emit(writer, opc_push, 1, reg(r12, Type_Int64, true));
    emit(writer, opc_push, 1, reg(r13, Type_Int64, true));
    emit(writer, opc_push, 1, reg(r14, Type_Int64, true));
    emit(writer, opc_push, 1, reg(r15, Type_Int64, true));
    emit(writer, opc_push, 1, reg(rdi, Type_Int64, true));
    emit(writer, opc_push, 1, reg(rsi, Type_Int64, true));
    for (int i = 8; i <= 15; i++) emit(writer, opc_push, 1, reg(i, Type_Float64, true));
    emit(writer, opc_push, 1, reg(rbp, Type_Int64, true));
    emit(writer, mov, 2, reg(rbp, Type_Pointer, true), reg(rsp, Type_Pointer, true));
    stack_size += 8;
    if (stack_size % 16 != 0) stack_size += 16 - (stack_size % 16);
    stack_size -= 8;
    if (stack_size != 0) emit(writer, sub, 2, reg(rsp, Type_Pointer, true), imm(stack_size, Type_Int32, true));

    int offset = 0;
    for (size_t i = 0; i < signature->func.num_params; i++) {
        jitc_type_t* param = signature->func.params[i];
        if (offset % param->alignment != 0) offset += param->alignment - (offset % param->alignment);
        if (!param->name) continue;
        if (i < 4) emit(writer, mov, 2, ptr(rbp, -offset - param->size, param->kind, param->is_unsigned), (isflt(param->kind)
            ? (reg_t[]){ xmm0, xmm1, xmm2, xmm3 }
            : (reg_t[]){ rcx, rdx, r8, r9 }
        )[i], param->kind, param->is_unsigned);
        else emit(writer, mov, 2,
            ptr(rbp, -offset - param->size, param->kind, param->is_unsigned),
            ptr(rbp, (i + 11) * 8, param->kind, param->is_unsigned)
        );
        offset += param->size;
    }
}

static void jitc_asm_ret(bytewriter_t* writer) {
    if (isflt(func_signature->func.ret->kind))
        emit(writer, mov, 2, reg(xmm0, func_signature->func.ret->kind, false), op(peek(0)));
    else if (func_signature->func.ret->kind != Type_Void)
        emit(writer, mov, 2, reg(rax, func_signature->func.ret->kind, func_signature->func.ret->is_unsigned), op(peek(0)));
    pop(writer);
    emit(writer, jmp, 1, imm(0, Type_Int32, false));
    push_return(writer);
}

static void jitc_asm_func_end(bytewriter_t* writer) {
    pop_return(writer);
    emit(writer, leave, 0);
    for (int i = 15; i >= 8; i--) emit(writer, opc_pop, 1, reg(i, Type_Float64, true));
    emit(writer, opc_pop, 1, reg(r15, Type_Int64, true));
    emit(writer, opc_pop, 1, reg(r14, Type_Int64, true));
    emit(writer, opc_pop, 1, reg(r13, Type_Int64, true));
    emit(writer, opc_pop, 1, reg(r12, Type_Int64, true));
    emit(writer, opc_pop, 1, reg(rbx, Type_Int64, true));
    emit(writer, ret, 0);
}
