#include "dynamics.h"
#include "jitc_internal.h"

typedef struct {
    const char* name;
    void* from;
    void* to;
} jitc_funcmap_t;

static list(jitc_funcmap_t)* funcmap;

void jitc_gdb_map_function(void* from, void* to, const char* name) {
    if (!funcmap) funcmap = list_new(jitc_funcmap_t);
    list_add(funcmap) = (jitc_funcmap_t){
        .name = name,
        .from = from,
        .to = to,
    };
}

const char* jitc_gdb_whereami(void* rip) {
    if (!funcmap) return NULL;
    for (size_t i = 0; i < list_size(funcmap); i++) {
        jitc_funcmap_t* func = &list_get(funcmap, i);
        if (rip >= func->from && rip < func->to) return func->name;
    }
    return NULL;
}

void jitc_gdb_backtrace(void* rip, void* rbp) {
    int index = 0;
    const char* name;
    while ((name = jitc_gdb_whereami(rip))) {
        printf("#%d: %s\n", index, name);
        rip = ((void**)rbp)[6];
        rbp = ((void**)rbp)[0];
        index++;
    }
    printf("%d frames\n", index);
}
