#include "dynamics.h"
#include "jitc_internal.h"
#include "lexer.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define FORMAT(fmt) ({ \
    va_list args1, args2; \
    va_start(args1, fmt); \
    va_copy(args2, args1); \
    char* __str = malloc(vsnprintf(NULL, 0, fmt, args1) + 1); \
    va_end(args1); \
    vsprintf(__str, fmt, args2); \
    va_end(args2); \
    __str; \
})

static uint64_t hash_mix(uint64_t a, uint64_t b) {
    return a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2));
}

static uint64_t hash_str(const char* str) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    while (str && *str) hash = (hash ^ (uint8_t)*str++) * 0x100000001B3ULL;
    return hash;
}

static uint64_t hash_ptr(void* ptr) {
    return (uintptr_t)ptr;
}

static uint64_t hash_int(uint64_t ptr) {
    return ptr;
}

static uint64_t hash_type(jitc_type_t* type) {
    if (type->hash != 0) return type->hash;
    uint64_t hash = 0xCBF29CE484222325ULL;
    hash = hash_mix(hash, hash_int(type->kind));
    hash = hash_mix(hash, hash_int(type->size));
    hash = hash_mix(hash, hash_int(type->alignment));
    hash = hash_mix(hash, hash_int(type->is_const));
    hash = hash_mix(hash, hash_int(type->is_unsigned));
    hash = hash_mix(hash, hash_str(type->name));
    switch (type->kind) {
        case Type_Pointer:
        case Type_Enum:
            hash = hash_mix(hash, hash_ptr(type->ptr.base));
            break;
        case Type_Array:
            hash = hash_mix(hash, hash_ptr(type->arr.base));
            hash = hash_mix(hash, hash_int(type->arr.size));
            break;
        case Type_Function:
            hash = hash_mix(hash, hash_ptr(type->func.ret));
            for (size_t i = 0; i < list_size(type->func.params); i++)
                hash = hash_mix(hash, hash_ptr(list_get_ptr(type->func.params, i)));
            break;
        case Type_Struct:
        case Type_Union:
            for (size_t i = 0; i < list_size(type->str.fields); i++)
                hash = hash_mix(hash, hash_ptr(list_get_ptr(type->str.fields, i)));
            break;
        case Type_StructRef:
        case Type_UnionRef:
        case Type_EnumRef:
            hash = hash_mix(hash, hash_str(type->ref.name));
            break;
        default: break;
    }
    return type->hash = hash;
}

jitc_type_t* jitc_register_type(jitc_context_t* context, jitc_type_t* type) {
    uint64_t hash = hash_type(type);
    jitc_type_t** t = map_get_int(context->typecache, hash);
    if (!*t) {
        jitc_type_t* copy = malloc(sizeof(jitc_type_t));
        memcpy(copy, type, sizeof(jitc_type_t));
        *t = copy;
    }
    else {
        if (type->kind == Type_Struct || type->kind == Type_Union) list_delete(type->str.fields);
        if (type->kind == Type_Function) list_delete(type->func.params);
    }
    return *t;
}

jitc_type_t* jitc_typecache_primitive(jitc_context_t* context, jitc_type_kind_t kind) {
    jitc_type_t type = {};
    type.kind = kind;
    if      (kind == Type_Int8)    type.size = 1;
    else if (kind == Type_Int16)   type.size = 2;
    else if (kind == Type_Int32)   type.size = 4;
    else if (kind == Type_Float32) type.size = 4;
    else type.size = 8;
    type.alignment = type.size;
    return jitc_register_type(context, &type);
}

jitc_type_t* jitc_typecache_unsigned(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t type = *base;
    type.is_unsigned = true;
    type.hash = 0;
    return jitc_register_type(context, &type);
}

jitc_type_t* jitc_typecache_const(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t type = *base;
    type.is_const = true;
    type.hash = 0;
    return jitc_register_type(context, &type);
}

jitc_type_t* jitc_typecache_align(jitc_context_t* context, jitc_type_t* base, uint64_t new_align) {
    jitc_type_t type = *base;
    type.alignment = new_align;
    type.hash = 0;
    return jitc_register_type(context, &type);
}

jitc_type_t* jitc_typecache_pointer(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t ptr = {};
    ptr.kind = Type_Pointer;
    ptr.ptr.base = base;
    ptr.size = ptr.alignment = 8;
    return jitc_register_type(context, &ptr);
}

jitc_type_t* jitc_typecache_array(jitc_context_t* context, jitc_type_t* base, size_t size) {
    jitc_type_t arr = {};
    arr.kind = Type_Array;
    arr.arr.base = base;
    arr.arr.size = size;
    arr.size = size == -1 ? size : base->size * size;
    arr.alignment = base->alignment;
    return jitc_register_type(context, &arr);
}

jitc_type_t* jitc_typecache_function(jitc_context_t* context, jitc_type_t* ret, list_t* params) {
    jitc_type_t func = {};
    list_t* copy = list_new();
    for (size_t i = 0; i < list_size(params); i++) list_add_ptr(copy, list_get_ptr(params, i));
    func.kind = Type_Function;
    func.func.params = copy;
    func.func.ret = ret;
    func.size = func.alignment = 8;
    return jitc_register_type(context, &func);
}

jitc_type_t* jitc_typecache_struct(jitc_context_t* context, list_t* fields) {
    jitc_type_t str = {};
    list_t* copy = list_new();
    for (size_t i = 0; i < list_size(fields); i++) list_add_ptr(copy, list_get_ptr(fields, i));
    str.kind = Type_Struct;
    str.str.fields = copy;
    size_t max_alignment = 1;
    size_t ptr = 0;
    for (int i = 0; i < list_size(fields); i++) {
        jitc_type_t* field = list_get_ptr(fields, i);
        if (ptr % field->alignment != 0) ptr += field->alignment - ptr % field->alignment;
        if (max_alignment < field->alignment) max_alignment = field->alignment;
        ptr += field->size;
    }
    if (ptr % max_alignment == 0) ptr += max_alignment - ptr % max_alignment;
    if (ptr == 0) ptr++;
    str.alignment = max_alignment;
    str.size = ptr;
    return jitc_register_type(context, &str);
}

jitc_type_t* jitc_typecache_union(jitc_context_t* context, list_t* fields) {
    jitc_type_t str = {};
    list_t* copy = list_new();
    for (size_t i = 0; i < list_size(fields); i++) list_add_ptr(copy, list_get_ptr(fields, i));
    str.kind = Type_Union;
    str.str.fields = copy;
    size_t max_alignment = 1;
    size_t max_size = 0;
    for (int i = 0; i < list_size(fields); i++) {
        jitc_type_t* field = list_get_ptr(fields, i);
        if (max_alignment < field->alignment) max_alignment = field->alignment;
        if (max_size < field->size) max_size = field->size;
    }
    if (max_size % max_alignment == 0) max_size += max_alignment - max_size % max_alignment;
    if (max_size == 0) max_size++;
    str.alignment = max_alignment;
    str.size = max_size;
    return jitc_register_type(context, &str);
}

jitc_type_t* jitc_typecache_enum(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t enm = {};
    enm.kind = Type_Enum;
    enm.ptr.base = base;
    return jitc_register_type(context, &enm);
}

jitc_type_t* jitc_typecache_structref(jitc_context_t* context, const char* name) {
    jitc_type_t ref = {};
    ref.kind = Type_StructRef;
    ref.ref.name = name;
    return jitc_register_type(context, &ref);
}

jitc_type_t* jitc_typecache_unionref(jitc_context_t* context, const char* name) {
    jitc_type_t ref = {};
    ref.kind = Type_EnumRef;
    ref.ref.name = name;
    return jitc_register_type(context, &ref);
}

jitc_type_t* jitc_typecache_enumref(jitc_context_t* context, const char* name) {
    jitc_type_t ref = {};
    ref.kind = Type_EnumRef;
    ref.ref.name = name;
    return jitc_register_type(context, &ref);
}

jitc_type_t* jitc_typecache_named(jitc_context_t* context, jitc_type_t* base, const char* name) {
    jitc_type_t type = *base;
    type.name = name;
    type.hash = 0;
    return jitc_register_type(context, &type);
}

bool jitc_declare_variable(jitc_context_t* context, jitc_type_t* type, jitc_decltype_t decltype) {
    return true;
}

bool jitc_declare_tagged_type(jitc_context_t* context, jitc_type_t* type) {
    return true;
}

bool jitc_declare_enum_item(jitc_context_t* context, jitc_type_t* type, const char* name, uint64_t value) {
    return true;
}

void jitc_push_function_scope(jitc_context_t* context) {

}

void jitc_push_scope(jitc_context_t* context) {

}

void jitc_pop_scope(jitc_context_t* context) {

}

jitc_error_t* jitc_error_syntax(const char* filename, int row, int col, const char* str, ...) {
    jitc_error_t* error = malloc(sizeof(jitc_error_t));
    error->msg = FORMAT(str);
    error->file = filename;
    error->row = row;
    error->col = col;
    return error;
}

jitc_error_t* jitc_error_parser(jitc_token_t* token, const char* str, ...) {
    jitc_error_t* error = malloc(sizeof(jitc_error_t));
    error->msg = FORMAT(str);
    error->file = token->filename;
    error->row = token->row;
    error->col = token->col;
    return error;
}

void jitc_error_set(jitc_context_t* context, jitc_error_t* error) {
    free(context->error);
    context->error = error;
}

void jitc_report_error(jitc_error_t* error, FILE* file) {
    fprintf(file, "Error: %s (in %s at %d:%d)\n", error->msg, error->file, error->row, error->col);
}

bool jitc_validate_type(jitc_type_t* type, jitc_type_policy_t policy) {
    if ((policy & TypePolicy_NoArray) && type->kind == Type_Array) return false;
    if ((policy & TypePolicy_NoFunction) && type->kind == Type_Function) return false;
    if ((policy & TypePolicy_NoVoid) && type->kind == Type_Void) return false;
    if ((policy & TypePolicy_NoUnkArrSize) && (type->kind == Type_Array && type->arr.size == -1)) return false;
    if ((policy & TypePolicy_NoUndefTags)) {
        // todo
        return true;
    }
    return true;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

static int compare_int64(const void* a, const void* b) {
    return *(uint64_t**)b - *(uint64_t**)a;
}

jitc_context_t* jitc_create_context() {
    jitc_context_t* context = malloc(sizeof(jitc_context_t));
    context->strings = set_new(compare_string);
    context->symbols = map_new(compare_string);
    context->typecache = map_new(compare_int64);
    context->error = NULL;
    return context;
}
