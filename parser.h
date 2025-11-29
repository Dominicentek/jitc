#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "jitc_internal.h"

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype);
jitc_ast_t* jitc_parse_expression(jitc_context_t* context, queue_t* tokens, jitc_type_t** exprtype);
jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* token_queue);

void jitc_destroy_ast(jitc_ast_t* ast);

#endif
