#include "cleanups.h"
#include "parser.h"

#include <stdlib.h>
#include <stdio.h>

typedef void alloc;

#define cleanup_func(type, func) void __cleanup_##type(void* ptr) { if (*(void**)ptr) func(*(type**)ptr); }

cleanup_func(alloc, free)
cleanup_func(FILE, fclose)
cleanup_func(string_t, str_delete)
cleanup_func(list_t, list_delete)
cleanup_func(map_t, map_delete)
cleanup_func(set_t, set_delete)
cleanup_func(stack_t, stack_delete)
cleanup_func(queue_t, queue_delete)
cleanup_func(structwriter_t, structwriter_delete)
cleanup_func(bytewriter_t, bytewriter_delete)
cleanup_func(bytereader_t, bytereader_delete)

cleanup_func(jitc_type_t, free)
cleanup_func(jitc_ast_t, jitc_destroy_ast)
