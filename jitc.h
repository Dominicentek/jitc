#ifndef JITC_H
#define JITC_H

#include <stdbool.h>
#include <stdio.h>

#ifndef JITC_LOCATION_DEPTH
#define JITC_LOCATION_DEPTH 16
#endif

typedef struct {
    int row, col;
    const char* filename;
} jitc_source_location_t;

typedef struct jitc_context_t jitc_context_t;
typedef struct jitc_error_t jitc_error_t;
struct jitc_error_t {
    const char* msg;
    int num_locations;
    union {
        jitc_source_location_t locations[JITC_LOCATION_DEPTH];
        struct {
            int row, col;
            const char* file;
        };
    };
};

jitc_context_t* jitc_create_context();
void jitc_create_header(jitc_context_t* context, const char* name, const char* content);
bool jitc_parse(jitc_context_t* context, const char* code, const char* filename);
bool jitc_parse_file(jitc_context_t* context, const char* filename);
void* jitc_get(jitc_context_t* context, const char* name);
void jitc_destroy_context(jitc_context_t* context);

jitc_error_t* jitc_get_error(jitc_context_t* context);
void jitc_destroy_error(jitc_error_t* error);

void jitc_report_error(jitc_context_t* context, FILE* stream);

#endif
