#include <sys/mman.h>
#include <unistd.h>

static void protect_rw(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
}

static void protect_rx(void* ptr, size_t size) {
    mprotect(ptr, size, PROT_READ | PROT_EXEC);
}

static size_t page_size() {
    return getpagesize();
}

static void* alloc_page(size_t size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

static void free_page(void* ptr, size_t size) {
    munmap(ptr, size);
}
