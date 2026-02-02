#ifndef JITC_CLEANUPS_H
#define JITC_CLEANUPS_H

#define smartptr(type) __attribute__((cleanup(__cleanup_##type))) type*
#define autofree __attribute__((cleanup(__cleanup_alloc)))

#define move(ptr) ({ \
    typeof(ptr)* _ptr = &(ptr); \
    typeof(ptr) _tmp = *_ptr; \
    *_ptr = 0; \
    _tmp; \
})


#ifdef DEBUG
#define TRACE_ERRORS(str) fprintf(stderr, str " at %s:%d (%s)\n", __FILE__, __LINE__, __FUNCTION__)
//#define TRACE_ERRORS(str) abort()
#else
#define TRACE_ERRORS(str)
#endif

#define try(x) ({ \
    typeof(x) _tmp = (x); \
    if (!_tmp) { \
        TRACE_ERRORS("try(): caught error"); \
    return 0; \
    } \
    _tmp; \
})

#define throw(...) ({ \
    TRACE_ERRORS("throw(): thrown error"); \
    throw_impl(__VA_ARGS__); \
    return 0; \
})

#define replace(x) *({ \
    typeof(x)* _tmp = &(x); \
    free(*_tmp); \
    *_tmp = 0; \
    _tmp; \
})

void __cleanup_alloc(void* ptr);

void __cleanup_FILE(void* file);
void __cleanup_string_t(void* str);
void __cleanup_list_t(void* list);
void __cleanup_map_t(void* map);
void __cleanup_set_t(void* set);
void __cleanup_stack_t(void* stack);
void __cleanup_queue_t(void* queue);
void __cleanup_bytewriter_t(void* writer);

void __cleanup_jitc_ast_t(void* type);
void __cleanup_jitc_context_t(void* context);

#define __cleanup_list(...) __cleanup_list_t
#define __cleanup_map(...) __cleanup_map_t
#define __cleanup_set(...) __cleanup_set_t
#define __cleanup_stack(...) __cleanup_stack_t
#define __cleanup_queue(...) __cleanup_queue_t

#endif
