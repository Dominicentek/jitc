#include <sys/mman.h>
#include <unistd.h>

#include <string.h>
#include <stdlib.h>

#include "../jitc_internal.h"

typedef struct {
    void* ptr;
    size_t capacity;
    size_t avail;
} memchunk_t;

static void chunk_protect(jitc_context_t* context, void* ptr, int prot) {
    for (size_t i = 0; i < list_size(context->memchunks); i++) {
        memchunk_t* memchunk = list_get_ptr(context->memchunks, i);
        if (ptr >= memchunk->ptr && (uint8_t*)ptr < (uint8_t*)memchunk->ptr + memchunk->capacity) {
            mprotect(memchunk->ptr, memchunk->capacity, prot);
            return;
        }
    }
}

static void chunk_modify(jitc_context_t* context, void* ptr) {
    chunk_protect(context, ptr, PROT_READ | PROT_WRITE);
}

static void chunk_commit(jitc_context_t* context, void* ptr) {
    chunk_protect(context, ptr, PROT_READ | PROT_EXEC);
}

static void* make_executable(jitc_context_t* context, void* ptr, size_t size) {
    size_t chunk_size = size;
    if (chunk_size % 16 != 0) chunk_size += 16 - (chunk_size % 16);
    for (size_t i = 0; i < list_size(context->memchunks); i++) {
        memchunk_t* memchunk = list_get_ptr(context->memchunks, i);
        if (memchunk->avail >= chunk_size) {
            mprotect(memchunk->ptr, memchunk->capacity, PROT_READ | PROT_WRITE);
            void* chunk = (char*)memchunk->ptr + memchunk->capacity - memchunk->avail;
            memchunk->avail -= chunk_size;
            memcpy(chunk, ptr, size);
            mprotect(memchunk->ptr, memchunk->capacity, PROT_READ | PROT_EXEC);
            return chunk;
        }
    }
    size_t page_size = getpagesize();
    size_t num_pages = (chunk_size + page_size - 1) / page_size;
    void* chunk = mmap(NULL, num_pages * page_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memcpy(chunk, ptr, size);
    memchunk_t* memchunk = malloc(sizeof(memchunk_t));
    memchunk->avail = num_pages * page_size - chunk_size;
    memchunk->capacity = num_pages * page_size;
    memchunk->ptr = chunk;
    mprotect(chunk, num_pages * page_size, PROT_READ | PROT_EXEC);
    list_add_ptr(context->memchunks, memchunk);
    return chunk;
}

void jitc_delete_memchunks(jitc_context_t* context) {
    for (size_t i = 0; i < list_size(context->memchunks); i++) {
        memchunk_t* memchunk = list_get(context->memchunks, i);
        munmap(memchunk->ptr, memchunk->capacity);
        free(memchunk);
    }
    list_delete(context->memchunks);
}
