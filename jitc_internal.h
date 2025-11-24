#ifndef JITC_INTERNAL_H
#define JITC_INTERNAL_H

#include "jitc.h"

typedef struct {
    struct ErrorScope* parent;
    const char* file;
    const char* name;
    int row, col;
} jitc_error_scope_t;

struct jitc_error_t {
    jitc_error_scope_t* scope;
    char* msg;
};

struct jitc_context_t {

};


static jitc_* syntax(const char* filename, int row, int col, String str);
static jitc_* parser(struct Token* token, String str);
static jitc_* runtime(struct Context* context, String str);

#endif
