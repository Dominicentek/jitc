#ifndef JITC_H
#define JITC_H

#include <stdbool.h>
#include <stdio.h>

typedef struct jitc_context_t jitc_context_t;
typedef struct {
    const char* msg;
    const char* file;
    int row, col;
} jitc_error_t;

jitc_context_t* jitc_create_context();
void jitc_create_header(jitc_context_t* context, const char* name, const char* content);
bool jitc_parse(jitc_context_t* context, const char* code, const char* filename);
bool jitc_parse_file(jitc_context_t* context, const char* filename);
bool jitc_reload_file(jitc_context_t* context, const char* filename);
void* jitc_get(jitc_context_t* context, const char* name);
void jitc_destroy_context(jitc_context_t* context);

jitc_error_t* jitc_get_error(jitc_context_t* context);
void jitc_destroy_error(jitc_error_t* error);

void jitc_report_error(jitc_context_t* context, FILE* stream);

#endif
