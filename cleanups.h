#ifndef JITC_CLEANUPS_H
#define JITC_CLEANUPS_H

#define smartptr(type) __attribute__((cleanup(__cleanup_##type))) type*
#define autofree __attribute__((cleanup(__cleanup_alloc)))

#define move(ptr) ({ \
    typeof(ptr) temp = (ptr); \
    (ptr) = NULL; \
    temp; \
})

void __cleanup_alloc(void* ptr);

void __cleanup_FILE(void* file);
void __cleanup_string_t(void* str);
void __cleanup_list_t(void* list);
void __cleanup_map_t(void* map);
void __cleanup_set_t(void* set);
void __cleanup_stack_t(void* stack);
void __cleanup_queue_t(void* queue);
void __cleanup_structwriter_t(void* writer);
void __cleanup_bytewriter_t(void* writer);
void __cleanup_bytereader_t(void* reader);

void __cleanup_jitc_type_t(void* type);

#endif
