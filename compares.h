#ifndef JITC_COMPARES_H
#define JITC_COMPARES_H

#include <string.h>
#include <stdint.h>

static int compare_string(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

static int compare_int64(const void* a, const void* b) {
    return (*(uint64_t*)a > *(uint64_t*)b) - (*(uint64_t*)a < *(uint64_t*)b);
}

#endif
