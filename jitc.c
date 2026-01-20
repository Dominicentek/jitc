#include "jitc.h"
#include "dynamics.h"
#include "jitc_internal.h"
#include "cleanups.h"
#include "compares.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
            hash = hash_mix(hash, hash_int(type->ptr.prev));
            hash = hash_mix(hash, hash_int(type->ptr.arr_size));
        case Type_Enum:
            hash = hash_mix(hash, hash_ptr(type->ptr.base));
            break;
        case Type_Array:
            hash = hash_mix(hash, hash_ptr(type->arr.base));
            hash = hash_mix(hash, hash_int(type->arr.size));
            break;
        case Type_Function:
            hash = hash_mix(hash, hash_ptr(type->func.ret));
            for (size_t i = 0; i < type->func.num_params; i++)
                hash = hash_mix(hash, hash_ptr(type->func.params[i]));
            break;
        case Type_Struct:
        case Type_Union:
            hash = hash_mix(hash, hash_ptr(type->str.source_token));
            for (size_t i = 0; i < type->str.num_fields; i++)
                hash = hash_mix(hash, hash_ptr(type->str.fields[i]));
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

static jitc_type_t* jitc_register_type(jitc_context_t* context, jitc_type_t* type, bool free_extras) {
    uint64_t hash = hash_type(type);
    if (!map_find(context->typecache, &hash)) {
        jitc_type_t* copy = malloc(sizeof(jitc_type_t));
        memcpy(copy, type, sizeof(jitc_type_t));
        map_add(context->typecache) = hash;
        map_commit(context->typecache);
        map_get_value(context->typecache) = copy;
    }
    else if (free_extras) {
        if (type->kind == Type_Struct || type->kind == Type_Union) {
            free(type->str.fields);
            free(type->str.offsets);
        }
        if (type->kind == Type_Function) free(type->func.params);
    }
    return map_get_value(context->typecache);
}

static jitc_type_t jitc_copy_type(jitc_type_t* type) {
    jitc_type_t copy = *type;
    if (copy.kind == Type_Struct || copy.kind == Type_Union) {
        copy.str.fields = malloc(sizeof(jitc_type_t*) * copy.str.num_fields);
        copy.str.offsets = malloc(sizeof(size_t) * copy.str.num_fields);
        memcpy(copy.str.fields, type->str.fields, sizeof(jitc_type_t*) * copy.str.num_fields);
        memcpy(copy.str.offsets, type->str.offsets, sizeof(size_t) * copy.str.num_fields);
    }
    if (copy.kind == Type_Function) {
        copy.func.params = malloc(sizeof(jitc_type_t*) * copy.func.num_params);
        memcpy(copy.func.params, type->func.params, sizeof(jitc_type_t*) * copy.func.num_params);
    }
    return copy;
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
    return jitc_register_type(context, &type, false);
}

jitc_type_t* jitc_typecache_unsigned(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t type = jitc_copy_type(base);
    type.is_unsigned = true;
    type.hash = 0;
    return jitc_register_type(context, &type, false);
}

jitc_type_t* jitc_typecache_const(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t type = jitc_copy_type(base);
    type.is_const = true;
    type.hash = 0;
    return jitc_register_type(context, &type, false);
}

jitc_type_t* jitc_typecache_align(jitc_context_t* context, jitc_type_t* base, uint64_t new_align) {
    jitc_type_t type = jitc_copy_type(base);
    type.alignment = new_align;
    type.hash = 0;
    return jitc_register_type(context, &type, false);
}

jitc_type_t* jitc_typecache_pointer(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t ptr = {};
    if (base->kind == Type_Pointer && base->ptr.prev != Type_Pointer) {
        ptr = jitc_copy_type(base);
        ptr.ptr.prev = Type_Pointer;
        ptr.hash = 0;
        return jitc_register_type(context, &ptr, false);
    }
    ptr.kind = Type_Pointer;
    ptr.ptr.base = base;
    ptr.ptr.prev = Type_Pointer;
    ptr.size = ptr.alignment = 8;
    return jitc_register_type(context, &ptr, false);
}

jitc_type_t* jitc_typecache_array(jitc_context_t* context, jitc_type_t* base, size_t size) {
    jitc_type_t arr = {};
    arr.kind = Type_Array;
    arr.arr.base = base;
    arr.arr.size = size;
    arr.size = size == -1 ? size : base->size * size;
    arr.alignment = base->alignment;
    return jitc_register_type(context, &arr, false);
}

jitc_type_t* jitc_typecache_function(jitc_context_t* context, jitc_type_t* ret, list_t* _params) {
    list(jitc_type_t*)* params = _params;
    jitc_type_t func = {};
    func.kind = Type_Function;
    func.func.num_params = list_size(params);
    func.func.params = malloc(sizeof(jitc_type_t*) * list_size(params));
    func.func.ret = ret;
    func.size = func.alignment = 8;
    for (size_t i = 0; i < list_size(params); i++) func.func.params[i] = list_get(params, i);
    return jitc_register_type(context, &func, true);
}

jitc_type_t* jitc_typecache_struct(jitc_context_t* context, list_t* _fields, jitc_token_t* source) {
    list(jitc_type_t*)* fields = _fields;
    jitc_type_t str = {};
    str.kind = Type_Struct;
    str.str.num_fields = list_size(fields);
    str.str.fields = malloc(sizeof(jitc_type_t*) * list_size(fields));
    str.str.offsets = malloc(sizeof(size_t) * list_size(fields));
    str.str.source_token = source;
    for (size_t i = 0; i < list_size(fields); i++) str.str.fields[i] = list_get(fields, i);
    size_t max_alignment = 1;
    size_t ptr = 0;
    for (int i = 0; i < list_size(fields); i++) {
        jitc_type_t* field = list_get(fields, i);
        if (ptr % field->alignment != 0) ptr += field->alignment - ptr % field->alignment;
        if (max_alignment < field->alignment) max_alignment = field->alignment;
        str.str.offsets[i] = ptr;
        ptr += field->size;
    }
    if (ptr % max_alignment != 0) ptr += max_alignment - ptr % max_alignment;
    if (ptr == 0) ptr++;
    str.alignment = max_alignment;
    str.size = ptr;
    return jitc_register_type(context, &str, true);
}

jitc_type_t* jitc_typecache_union(jitc_context_t* context, list_t* _fields, jitc_token_t* source) {
    list(jitc_type_t*)* fields = _fields;
    jitc_type_t str = {};
    str.kind = Type_Union;
    str.str.num_fields = list_size(fields);
    str.str.fields = malloc(sizeof(jitc_type_t*) * list_size(fields));
    str.str.offsets = malloc(sizeof(size_t) * list_size(fields));
    str.str.source_token = source;
    for (size_t i = 0; i < list_size(fields); i++) str.str.fields[i] = list_get(fields, i);
    size_t max_alignment = 1;
    size_t max_size = 0;
    for (int i = 0; i < list_size(fields); i++) {
        jitc_type_t* field = list_get(fields, i);
        if (max_alignment < field->alignment) max_alignment = field->alignment;
        if (max_size < field->size) max_size = field->size;
        str.str.offsets[i] = 0;
    }
    if (max_size % max_alignment != 0) max_size += max_alignment - max_size % max_alignment;
    if (max_size == 0) max_size++;
    str.alignment = max_alignment;
    str.size = max_size;
    return jitc_register_type(context, &str, true);
}

jitc_type_t* jitc_typecache_enum(jitc_context_t* context, jitc_type_t* base) {
    jitc_type_t enm = {};
    enm.kind = Type_Enum;
    enm.ptr.base = base;
    enm.size = enm.ptr.base->size;
    enm.alignment = enm.ptr.base->alignment;
    return jitc_register_type(context, &enm, false);
}

jitc_type_t* jitc_typecache_structref(jitc_context_t* context, const char* name) {
    jitc_type_t ref = {};
    ref.kind = Type_StructRef;
    ref.ref.name = name;
    return jitc_register_type(context, &ref, false);
}

jitc_type_t* jitc_typecache_unionref(jitc_context_t* context, const char* name) {
    jitc_type_t ref = {};
    ref.kind = Type_UnionRef;
    ref.ref.name = name;
    return jitc_register_type(context, &ref, false);
}

jitc_type_t* jitc_typecache_enumref(jitc_context_t* context, const char* name) {
    jitc_type_t ref = {};
    ref.kind = Type_EnumRef;
    ref.ref.name = name;
    return jitc_register_type(context, &ref, false);
}

jitc_type_t* jitc_typecache_named(jitc_context_t* context, jitc_type_t* base, const char* name) {
    jitc_type_t type = jitc_copy_type(base);
    type.name = name;
    type.hash = 0;
    return jitc_register_type(context, &type, false);
}

jitc_type_t* jitc_typecache_decay(jitc_context_t* context, jitc_type_t* from) {
    if (from->kind != Type_Array && from->kind != Type_Function) return from;
    jitc_type_t type = {};
    type.kind = Type_Pointer;
    type.name = from->name;
    type.ptr.base = from->kind == Type_Array ? from->ptr.base : from;
    type.ptr.prev = from->kind;
    if (from->kind == Type_Array) type.ptr.arr_size = from->arr.size;
    type.size = type.alignment = 8;
    type.hash = 0;
    return jitc_register_type(context, &type, false);
}

bool jitc_typecmp(jitc_context_t* context, jitc_type_t* a, jitc_type_t* b) {
    if ((a->kind == Type_Struct || a->kind == Type_Union) && (b->kind == Type_Struct || b->kind  == Type_Union))
        return a->str.source_token = b->str.source_token;
    return jitc_typecache_named(context, a, NULL) == jitc_typecache_named(context, b, NULL);
}

jitc_type_t* jitc_to_method(jitc_context_t* context, jitc_type_t* type) {
    if (type->kind == Type_Function &&
        type->func.num_params > 0 &&
        type->func.params[0]->name &&
        type->func.params[0]->kind == Type_Pointer &&
        strcmp(type->func.params[0]->name, "this") == 0
    ) {
        uint64_t hash = jitc_typecache_named(context, type->func.params[0]->ptr.base, NULL)->hash;
        char new_name[2 + 16 + 1 + strlen(type->name) + 1];
        sprintf(new_name, "__%16lx_%s", hash, type->name);
        type = jitc_typecache_named(context, type, jitc_append_string(context, new_name));
    }
    return type;
}

bool jitc_declare_variable(jitc_context_t* context, jitc_type_t* type, jitc_decltype_t decltype, jitc_preserve_t preserve_policy, uint64_t value) {
    if (!type->name) return true;
    jitc_variable_t* prev = jitc_get_variable(context, type->name);
    if (prev) {
        if (list_size(context->scopes) > 1) return false;
        bool policy_ifconst = preserve_policy == Preserve_IfConst && prev->preserve_policy == Preserve_IfConst;
        if (preserve_policy == Preserve_IfConst) preserve_policy = prev->preserve_policy;
        if (preserve_policy == Preserve_IfConst) {
            if (type->kind == Type_Array || type->kind == Type_Function) preserve_policy = Preserve_Never;
            else preserve_policy = type->is_const ? Preserve_Never : Preserve_Always;
        }
        if (!jitc_typecmp(context, prev->type, type)) preserve_policy = Preserve_Never; // todo: add compatible merges
        if (prev->decltype != decltype) preserve_policy = Preserve_Never;
        if (preserve_policy == Preserve_Never) {
            if (prev->decltype != Decltype_EnumItem && prev->type->kind != Type_Function) free(prev->ptr);
            prev->type = type;
            prev->decltype = decltype;
            if (prev->type->kind != Type_Function) prev->enum_value = value;
        }
        prev->preserve_policy = policy_ifconst ? Preserve_IfConst : preserve_policy;
        return true;
    }
    jitc_scope_t* scope = NULL;
    if (decltype == Decltype_Static) scope = &list_get(context->scopes, 0);
    else scope = &list_get(context->scopes, list_size(context->scopes) - 1);
    bool global = scope == &list_get(context->scopes, 0);
    map_add(scope->variables) = (char*)type->name;
    map_commit(scope->variables);
    jitc_variable_t* var = &map_get_value(scope->variables);
    var->type = type;
    var->decltype = decltype;
    var->enum_value = value;
    var->preserve_policy = preserve_policy;
    var->initial = true;
    return true;
}

bool jitc_declare_tagged_type(jitc_context_t* context, jitc_type_t* type, const char* name) {
    jitc_scope_t* scope = &list_get(context->scopes, list_size(context->scopes) - 1);
    map(char*, jitc_type_t*)* map = NULL;
    if (type->kind == Type_Struct) map = (void*)scope->structs;
    if (type->kind == Type_Union) map = (void*)scope->unions;
    if (type->kind == Type_Enum) map = (void*)scope->enums;
    if (!map) return false;
    if (map_find(map, &name)) return false;
    map_add(map) = (char*)name;
    map_commit(map);
    map_get_value(map) = type;
    return true;
}

jitc_variable_t* jitc_get_variable(jitc_context_t* context, const char* name) {
    if (!name) return NULL;
    bool outside_of_function = false;
    for (size_t i = list_size(context->scopes) - 1; i < list_size(context->scopes) /* rely on underflow */; i--) {
        if (i != 0 && outside_of_function) continue;
        jitc_scope_t* scope = &list_get(context->scopes, i);
        if (!map_find(scope->variables, &name)) continue;
        return &map_get_value(scope->variables);
    }
    return NULL;
}

jitc_type_t* jitc_get_tagged_type(jitc_context_t* context, jitc_type_kind_t kind, const char* name) {
    if (!name) return NULL;
    for (size_t i = list_size(context->scopes) - 1; i < list_size(context->scopes) /* rely on underflow */; i--) {
        jitc_scope_t* scope = &list_get(context->scopes, i);
        map(char*, jitc_type_t*)* map = NULL;
        if (kind == Type_StructRef || kind == Type_Struct) map = (void*)scope->structs;
        if (kind == Type_UnionRef || kind == Type_Union) map = (void*)scope->unions;
        if (kind == Type_EnumRef || kind == Type_Enum) map = (void*)scope->enums;
        if (!map) return NULL;
        if (!map_find(map, &name)) continue;
        return map_get_value(map);
    }
    return NULL;
}

void jitc_push_scope(jitc_context_t* context) {
    jitc_scope_t* scope = &list_add(context->scopes);
    scope->variables = map_new(compare_string, char*, jitc_variable_t);
    scope->structs = map_new(compare_string, char*, jitc_type_t*);
    scope->unions = map_new(compare_string, char*, jitc_type_t*);
    scope->enums = map_new(compare_string, char*, jitc_type_t*);
}

static void jitc_destroy_scope(jitc_scope_t* scope) {
    map_delete(scope->variables);
    map_delete(scope->structs);
    map_delete(scope->unions);
    map_delete(scope->enums);
}

bool jitc_pop_scope(jitc_context_t* context) {
    if (list_size(context->scopes) <= 1) return false;
    jitc_scope_t* scope = &list_get(context->scopes, list_size(context->scopes) - 1);
    list_remove(context->scopes, list_size(context->scopes) - 1);
    jitc_destroy_scope(scope);
    return true;
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

void jitc_report_error(jitc_context_t* context, FILE* file) {
    jitc_error_t* error = move(context->error);
    if (!error) return;
    fprintf(file, "Error: %s ", error->msg);
    if (error->col == 0 && error->row == 0) {
        fprintf(file, "(in %s)\n", error->file ?: "linker");
    }
    else fprintf(file, "(in %s at %d:%d)\n", error->file ?: "<memory>", error->row, error->col);
    jitc_destroy_error(error);
}

jitc_error_t* jitc_get_error(jitc_context_t* context) {
    return move(context->error);
}

void jitc_destroy_error(jitc_error_t* error) {
    if (!error) return;
    free((char*)error->msg);
    free(error);
}

void jitc_error_set(jitc_context_t* context, jitc_error_t* error) {
    jitc_destroy_error(context->error);
    context->error = error;
}

bool jitc_validate_type(jitc_type_t* type, jitc_type_policy_t policy) {
    if ((policy & TypePolicy_NoArray) && type->kind == Type_Array) return false;
    if ((policy & TypePolicy_NoFunction) && type->kind == Type_Function) return false;
    if ((policy & TypePolicy_NoVoid) && type->kind == Type_Void) return false;
    if ((policy & TypePolicy_NoUnkArrSize) && (type->kind == Type_Array && type->arr.size == -1)) return false;
    if ((policy & TypePolicy_NoUndefTags) && (type->kind == Type_StructRef || type->kind == Type_UnionRef || type->kind == Type_EnumRef)) return false;
    return true;
}

char* jitc_append_string(jitc_context_t* context, const char* str) {
    if (!str) return NULL;
    char** ptr = set_find(context->strings, &str);
    if (ptr) return *ptr;
    char* string = strdup(str);
    set_add(context->strings) = string;
    set_commit(context->strings);
    return string;
}

jitc_context_t* jitc_create_context() {
    jitc_context_t* context = malloc(sizeof(jitc_context_t));
    context->strings = set_new(compare_string, char*);
    context->typecache = map_new(compare_int64, char*, jitc_type_t);
    context->headers = map_new(compare_string, char*, char*);
    context->labels = list_new(char*);
    context->scopes = list_new(jitc_scope_t);
    context->memchunks = list_new(jitc_memchunk_t);
    context->error = NULL;
    jitc_push_scope(context);
    return context;
}

static jitc_variable_t* jitc_get_symbol(jitc_context_t* context, const char* name, bool normal_only) {
    jitc_scope_t* scope = &list_get(context->scopes, 0);
    if (!map_find(scope->variables, &name)) return NULL;
    jitc_variable_t* var = &map_get_value(scope->variables);
    if (var->decltype == Decltype_Typedef) return NULL;
    if (var->decltype == Decltype_Extern && normal_only) return NULL;
    if (var->decltype == Decltype_Static && normal_only) return NULL;
    return var;
}

static void jitc_link_error_stub() {
    fprintf(stderr, "[JITC] Calling function with some symbols unresolved\n");
    fflush(stderr);
    abort();
}

void jitc_link(jitc_context_t* context) {
    context->all_linked = true;
    jitc_scope_t* scope = &list_get(context->scopes, 0);
    for (size_t i = 0; i < map_size(scope->variables); i++) {
        map_index(scope->variables, i);
        jitc_variable_t* var = &map_get_value(scope->variables);
        if (var->decltype == Decltype_EnumItem || var->decltype == Decltype_Typedef || var->ptr) continue;
        if (var->decltype != Decltype_Extern) {
            context->all_linked = false;
            continue;
        }
        jitc_variable_t* symbol = jitc_get_symbol(context, var->type->name, true);
        var->ptr = symbol ? symbol->ptr : dlsym(RTLD_DEFAULT, var->type->name);
        if (!var->ptr) {
            context->all_linked = false;
            continue;
        }
    }
    for (size_t i = 0; i < map_size(scope->variables); i++) {
        map_index(scope->variables, i);
        jitc_variable_t* var = &map_get_value(scope->variables);
        if (var->decltype == Decltype_EnumItem || var->decltype == Decltype_Typedef || var->decltype == Decltype_Extern) continue;
        if (var->type->kind != Type_Function || !var->func) continue;
        var->func->addr->curr_ptr = context->all_linked ? var->func->addr->ptr : (void*)jitc_link_error_stub;
    }
}

jitc_variable_t* jitc_get_or_static(jitc_context_t* context, const char* name) {
    return jitc_get_symbol(context, name, false);
}

jitc_variable_t* jitc_get_method(jitc_context_t* context, jitc_type_t* base, const char* name) {
    base = jitc_typecache_named(context, base, NULL);
    jitc_variable_t* var = NULL;
    char method_name[2 + 16 + 1 + strlen(name) + 1];
    sprintf(method_name, "__%16lx_%s", base->hash, name);
    if ((var = jitc_get_or_static(context, method_name))) return var;
    return NULL;
}

bool jitc_walk_struct(jitc_type_t* str, const char* name, jitc_type_t** field_type, size_t* offset) {
    for (size_t i = 0; i < str->str.num_fields; i++) {
        jitc_type_t* field = str->str.fields[i];
        if (!field->name) {
            if (field->kind == Type_Struct || field->kind == Type_Union) {
                size_t off = 0;
                if (jitc_walk_struct(field, name, field_type, &off)) {
                    if (offset) *offset = off + str->str.offsets[i];
                    return true;
                }
            }
            continue;
        }
        if (strcmp(field->name, name) == 0) {
            if (offset) *offset = str->str.offsets[i];
            if (field_type) *field_type = field;
            return true;
        }
    }
    return false;
}

bool jitc_struct_field_exists_list(list_t* _list, const char* name) {
    list(jitc_type_t*)* list = _list;
    for (size_t i = 0; i < list_size(list); i++) {
        jitc_type_t* field = list_get(list, i);
        if (!field->name) {
            if (field->kind == Type_Struct || field->kind == Type_Union)
                if (jitc_walk_struct(field, name, NULL, NULL)) return true;
            continue;
        }
        if (strcmp(field->name, name) == 0) return true;
    }
    return false;
}

static char* read_whole_file(jitc_context_t* context, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        replace(context->error) = jitc_error_syntax(filename, 0, 0, "Failed to open: %s", strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* code = malloc(size + 1);
    fread(code, size, 1, f);
    code[size] = 0;
    fclose(f);
    return code;
}

queue_t* jitc_include(jitc_context_t* context, jitc_token_t* token, const char* filename, map_t* macros) {
    smartptr(queue(jitc_token_t)) tokens = NULL;
    if (!map_find(context->headers, &filename)) {
        autofree char* content = try(read_whole_file(context, filename));
        tokens = try(jitc_lex(context, content, filename));
    }
    else tokens = try(jitc_lex(context, map_get_value(context->headers), filename));
    tokens = try(jitc_preprocess(context, move(tokens), macros));
    return move(tokens);
}

void jitc_create_header(jitc_context_t* context, const char* name, const char* content) {
    map_add(context->headers) = (char*)name;
    map_commit(context->headers);
    map_get_value(context->headers) = jitc_append_string(context, content);
}

bool jitc_parse(jitc_context_t* context, const char* code, const char* filename) {
    smartptr(queue(jitc_token_t)) tokens = try(jitc_lex(context, code, filename));
    tokens = try(jitc_preprocess(context, move(tokens), NULL));
    smartptr(jitc_ast_t) ast = jitc_parse_ast(context, tokens);
    while (jitc_pop_scope(context));
    if (!ast) return false;
    for (size_t i = 0; i < list_size(ast->list.inner); i++) {
        jitc_compile(context, list_get(ast->list.inner, i));
    }
    jitc_link(context);
    return true;
}

bool jitc_parse_file(jitc_context_t* context, const char* filename) {
    autofree char* code = try(read_whole_file(context, filename));
    return try(jitc_parse(context, code, filename));
}

void* jitc_get(jitc_context_t* context, const char* name) {
    if (!context->all_linked) return jitc_error_set(context, jitc_error_syntax(NULL, 0, 0, "Some symbols aren't resolved yet")), NULL;
    jitc_variable_t* var = jitc_get_symbol(context, name, true);
    if (!var) return jitc_error_set(context, jitc_error_syntax(NULL, 0, 0, "Unable to resolve symbol '%s'", name)), NULL;
    return var->ptr;
}

void jitc_destroy_context(jitc_context_t* context) {
    for (size_t i = 0; i < set_size(context->strings); i++) {
        free(set_get(context->strings, i));
    }
    set_delete(context->strings);
    for (size_t i = 0; i < map_size(context->typecache); i++) {
        map_index(context->typecache, i);
        jitc_type_t* type = map_get_value(context->typecache);
        if (type->kind == Type_Function) free(type->func.params);
        if (type->kind == Type_Struct || type->kind == Type_Union) {
            free(type->str.fields);
            free(type->str.offsets);
        }
        free(type);
    }
    map_delete(context->typecache);
    map_delete(context->headers);
    list_delete(context->labels);
    jitc_delete_memchunks(context);
    while (list_size(context->scopes) > 1) jitc_pop_scope(context);
    jitc_destroy_scope(&list_get(context->scopes, 0));
    list_delete(context->scopes);
    free(context);
}
