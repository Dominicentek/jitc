#ifndef JITC_INTERNAL_H
#define JITC_INTERNAL_H

#include "jitc.h"
#include "dynamics.h"

typedef struct jitc_token_t jitc_token_t;

struct jitc_error_t {
    const char* msg;
    const char* file;
    int row, col;
} ;

struct jitc_context_t {
    set_t* strings;
    map_t* symbols;
    jitc_error_t* error;
};

jitc_error_t* jitc_error_syntax(const char* filename, int row, int col, const char* str, ...);
jitc_error_t* jitc_error_parser(jitc_token_t* token, const char* str, ...);
void jitc_error_set(jitc_context_t* context, jitc_error_t* error);

void jitc_report_error(jitc_error_t* error, FILE* file);
void jitc_free_error(jitc_error_t* error);

#endif
