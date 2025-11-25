#include "jitc_internal.h"
#include "lexer.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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

void jitc_error_set(jitc_context_t* context, jitc_error_t* error) {
    free(context->error);
    context->error = error;
}

void jitc_report_error(jitc_error_t* error, FILE* file) {
    fprintf(file, "Error: %s (in %s at %d:%d)\n", error->msg, error->file, error->row, error->col);
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

jitc_context_t* jitc_create_context() {
    jitc_context_t* context = malloc(sizeof(jitc_context_t));
    context->strings = set_new(compare_string);
    context->symbols = map_new(compare_string);
    context->error = NULL;
    return context;
}
