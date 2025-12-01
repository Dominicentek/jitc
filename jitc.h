#ifndef JITC_H
#define JITC_H

#include <stdbool.h>
#include <stdio.h>

typedef struct jitc_context_t jitc_context_t;
typedef struct jitc_error_t jitc_error_t;
typedef struct jitc_errors_t jitc_errors_t;

jitc_context_t* jitc_create_context();
void jitc_destroy_context(jitc_context_t* context);
void jitc_queue(jitc_context_t* context, const char* code, const char* filename);
void jitc_queue_file(jitc_context_t* context, const char* filename);
bool jitc_get(jitc_context_t* context, const char* name, void* ptr);
bool jitc_set(jitc_context_t* context, const char* name, void* ptr);

jitc_errors_t* jitc_parse();
void jitc_report_errors(jitc_errors_t* errors, FILE* stream);
void jitc_ignore_errors(jitc_errors_t* errors);

#endif
