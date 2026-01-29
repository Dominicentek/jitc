#include "cleanups.h"
#include "dynamics.h"
#include "jitc_internal.h"

#include "compares.h"

#include <stdlib.h>
#include <time.h>

#define throw_impl(token, ...) jitc_error_set(context, jitc_error_parser(token, "(Preprocessor) " __VA_ARGS__))

typedef enum {
    MacroType_Ordinary,
    MacroType_OrdinaryFunction,
    MacroType_LINE,
    MacroType_FILE,
    MacroType_DATE,
    MacroType_TIME,
    MacroType_ID,
} macro_type_t;

typedef struct {
    macro_type_t type;
    list(jitc_token_t)* tokens;
    list(char*)* args;
} macro_t;

typedef struct {
    list(jitc_token_t)* tokens;
    int ptr;
} token_stream_t;

#define number_token(val) (jitc_token_t){ \
    .type = TOKEN_INTEGER, \
    .value.integer = val, \
    .flags.int_flags.type_kind = Type_Int32, \
    .flags.int_flags.is_unsigned = false, \
}

#define string_token(val) (jitc_token_t){ \
    .type = TOKEN_STRING, \
    .value.string = val, \
}

#define identifier_token(val) (jitc_token_t){ \
    .type = TOKEN_IDENTIFIER, \
    .value.string = val, \
}

static macro_t* new_macro(map_t* _macros, const char* name, macro_type_t type) {
    map(char*, macro_t)* macros = _macros;
    map_add(macros) = (char*)name;
    map_commit(macros);
    macro_t* macro = &map_get_value(macros);
    macro->type = type;
    if (type == MacroType_Ordinary)
        macro->tokens = list_new(jitc_token_t);
    return macro;
}

static void predefine(map_t* macros) {
    list_add(new_macro(macros, "__STDC_HOSTED__", MacroType_Ordinary)->tokens) = number_token(1);
    list_add(new_macro(macros, "__STDC_NO_ATOMICS__", MacroType_Ordinary)->tokens) = number_token(1);
    list_add(new_macro(macros, "__STDC_NO_COMPLEX__", MacroType_Ordinary)->tokens) = number_token(1);
    list_add(new_macro(macros, "__STDC_NO_THREADS__", MacroType_Ordinary)->tokens) = number_token(1);
    list_add(new_macro(macros, "__STDC_NO_VLA__", MacroType_Ordinary)->tokens) = number_token(1);
    list_add(new_macro(macros, "__JITC__", MacroType_Ordinary)->tokens) = number_token(1);
#ifdef __x86_64__
    list_add(new_macro(macros, "__x86_64__", MacroType_Ordinary)->tokens) = number_token(1);
#endif
#ifdef __aarch64__
    list_add(new_macro(macros, "__aarch64__", MacroType_Ordinary)->tokens) = number_token(1);
#endif
#ifdef _WIN32
    list_add(new_macro(macros, "_WIN32", MacroType_Ordinary)->tokens) = number_token(1);
#endif
#ifdef __APPLE__
    list_add(new_macro(macros, "__APPLE__", MacroType_Ordinary)->tokens) = number_token(1);
#endif
#ifdef __linux__
    list_add(new_macro(macros, "__linux__", MacroType_Ordinary)->tokens) = number_token(1);
#endif
#ifdef __unix__
    list_add(new_macro(macros, "__unix__", MacroType_Ordinary)->tokens) = number_token(1);
#endif
    new_macro(macros, "__LINE__", MacroType_LINE);
    new_macro(macros, "__FILE__", MacroType_FILE);
    new_macro(macros, "__DATE__", MacroType_DATE);
    new_macro(macros, "__TIME__", MacroType_TIME);
    new_macro(macros, "__ID__", MacroType_ID);
}

static jitc_token_t* advance(token_stream_t* tokens, int* curr_line) {
    jitc_token_t* token = &list_get(tokens->tokens, tokens->ptr);
    if (token->type == TOKEN_END_OF_FILE) return NULL;
    while (true) {
        if (token->row != *curr_line) return NULL;
        if (token->type == TOKEN_BACKSLASH) {
            jitc_token_t* backslash = &list_get(tokens->tokens, tokens->ptr++);
            token = &list_get(tokens->tokens, tokens->ptr);
            if (token->row == *curr_line) return backslash;
            else (*curr_line)++;
        }
        else return &list_get(tokens->tokens, tokens->ptr++);
    }
}

static jitc_token_t* lookahead(token_stream_t* tokens, int curr_line) {
    jitc_token_t* token = &list_get(tokens->tokens, tokens->ptr);
    if (token->type == TOKEN_END_OF_FILE) return NULL;
    while (true) {
        if (token->row != curr_line) return NULL;
        if (token->type == TOKEN_BACKSLASH) {
            jitc_token_t* backslash = &list_get(tokens->tokens, tokens->ptr);
            token = &list_get(tokens->tokens, tokens->ptr + 1);
            if (token->row == curr_line) return backslash;
            else curr_line++;
        }
        else return &list_get(tokens->tokens, tokens->ptr);
    }
}

static bool is_identifier(jitc_token_t* token, const char* id) {
    return
        (token->type == TOKEN_IDENTIFIER && strcmp(token->value.string, id) == 0) ||
        (token_table[token->type] && strcmp(token_table[token->type], id) == 0);
}

#define optional(x, y) ({ \
    typeof(x) _tmp = (x); \
    if (!_tmp) y; \
    _tmp; \
})

#define expect(x, msg, ...) ({ \
    typeof(x) _tmp = (x); \
    if (!_tmp) throw(token, msg __VA_OPT__(,) __VA_ARGS__); \
    _tmp; \
})

#define expect_and(x, cond, msg, ...) ({ \
    typeof(x) this = (x); \
    if (!this || !(cond)) throw(token, msg __VA_OPT__(,) __VA_ARGS__); \
    this; \
})

#define next_token(kind) ((token = &list_get(tokens->tokens, tokens->ptr)) && token->type == (kind) && ++tokens->ptr)

static bool compute_expression(jitc_context_t* context, map_t* macros, token_stream_t* stream, int64_t* left, int min_prec);

static bool get_operand(jitc_context_t* context, map_t* macros, token_stream_t* stream, int64_t* value) {
    jitc_token_t* token;
    smartptr(stack(jitc_token_type_t)) stack = stack_new(jitc_token_type_t);
    while (true) {
        token = expect(&list_get(stream->tokens, stream->ptr++), "Expected operand");
        if (
            token->type == TOKEN_MINUS ||
            token->type == TOKEN_PLUS ||
            token->type == TOKEN_TILDE ||
            token->type == TOKEN_EXCLAMATION_MARK
        ) stack_push(stack) = token->type;
        else break;
    }
    if (is_identifier(token, "defined")) {
        token = expect_and(
            &list_get(stream->tokens, stream->ptr++),
            this->type == TOKEN_IDENTIFIER || this->type == TOKEN_PARENTHESIS_OPEN,
            "Macro name expected"
        );
        const char* name = NULL;
        if (token->type == TOKEN_IDENTIFIER) name = token->value.string;
        else if (token->type == TOKEN_PARENTHESIS_OPEN) {
            token = expect_and(&list_get(stream->tokens, stream->ptr++), this->type == TOKEN_IDENTIFIER, "Macro name expected");
            name = token->value.string;
            token = expect_and(&list_get(stream->tokens, stream->ptr++), this->type == TOKEN_PARENTHESIS_CLOSE, "Expected ')'");
        }
        if (map_find(macros, &name)) *value = 1;
        else *value = 0;
    }
    else if (token->type == TOKEN_INTEGER) *value = token->value.integer;
    else if (token->type == TOKEN_PARENTHESIS_OPEN) {
        try(compute_expression(context, macros, stream, value, 1));
        expect_and(&list_get(stream->tokens, stream->ptr++), this->type == TOKEN_PARENTHESIS_CLOSE, "Expected ')'");
    }
    else throw(token, "Expected operand");
    while (stack_size(stack)) {
        jitc_token_type_t type = stack_pop(stack);
        if (type == TOKEN_PLUS) continue;
        else if (type == TOKEN_MINUS) *value = -*value;
        else if (type == TOKEN_TILDE) *value = ~*value;
        else if (type == TOKEN_EXCLAMATION_MARK) *value = !*value;
    }
    return true;
}

static struct {
    bool rtl_assoc;
    int precedence;
} op_info[TOKEN_COUNT] = {
    [TOKEN_ASTERISK]                   = { false, 11, },
    [TOKEN_SLASH]                      = { false, 11, },
    [TOKEN_PERCENT]                    = { false, 11, },
    [TOKEN_PLUS]                       = { false, 10, },
    [TOKEN_MINUS]                      = { false, 10, },
    [TOKEN_DOUBLE_LESS_THAN]           = { false, 9,  },
    [TOKEN_DOUBLE_GREATER_THAN]        = { false, 9,  },
    [TOKEN_LESS_THAN]                  = { false, 8,  },
    [TOKEN_GREATER_THAN]               = { false, 8,  },
    [TOKEN_LESS_THAN_EQUALS]           = { false, 8,  },
    [TOKEN_GREATER_THAN_EQUALS]        = { false, 8,  },
    [TOKEN_DOUBLE_EQUALS]              = { false, 7,  },
    [TOKEN_NOT_EQUALS]                 = { false, 7,  },
    [TOKEN_AMPERSAND]                  = { false, 6,  },
    [TOKEN_HAT]                        = { false, 5,  },
    [TOKEN_PIPE]                       = { false, 4,  },
    [TOKEN_DOUBLE_AMPERSAND]           = { false, 3,  },
    [TOKEN_DOUBLE_PIPE]                = { false, 2,  },
    [TOKEN_QUESTION_MARK]              = { false, 1   },
};

static bool compute_expression(jitc_context_t* context, map_t* macros, token_stream_t* stream, int64_t* left, int min_prec) {
    try(get_operand(context, macros, stream, left));
    while (true) {
        jitc_token_t* token = optional(&list_get(stream->tokens, stream->ptr), break);
        int precedence = op_info[token->type].precedence;
        if (precedence < min_prec) break;
        stream->ptr++;
        if (token->type == TOKEN_QUESTION_MARK) {
            int64_t then, otherwise;
            try(compute_expression(context, macros, stream, &then, precedence));
            expect_and(&list_get(stream->tokens, stream->ptr++), this->type == TOKEN_COLON, "Expected ':'");
            try(compute_expression(context, macros, stream, &otherwise, precedence));
            *left = *left != 0 ? then : otherwise;
            continue;
        }
        int64_t right;
        try(compute_expression(context, macros, stream, &right, precedence + !op_info[token->type].rtl_assoc));
        switch (token->type) {
            case TOKEN_PLUS: *left += right; break;
            case TOKEN_MINUS: *left -= right; break;
            case TOKEN_ASTERISK: *left *= right; break;
            case TOKEN_SLASH: if (right == 0) throw(token, "Division by 0"); *left /= right; break;
            case TOKEN_PERCENT: if (right == 0) throw(token, "Division by 0"); *left %= right; break;
            case TOKEN_AMPERSAND: *left &= right; break;
            case TOKEN_PIPE: *left |= right; break;
            case TOKEN_HAT: *left ^= right; break;
            case TOKEN_DOUBLE_LESS_THAN: *left <<= right; break;
            case TOKEN_DOUBLE_GREATER_THAN: *left >>= right; break;
            case TOKEN_DOUBLE_EQUALS: *left = *left == right; break;
            case TOKEN_NOT_EQUALS: *left = *left == right; break;
            case TOKEN_LESS_THAN: *left = *left < right; break;
            case TOKEN_GREATER_THAN: *left = *left > right; break;
            case TOKEN_LESS_THAN_EQUALS: *left = *left <= right; break;
            case TOKEN_GREATER_THAN_EQUALS: *left = *left >= right; break;
            case TOKEN_DOUBLE_AMPERSAND: *left = *left && right; break;
            case TOKEN_DOUBLE_PIPE: *left = *left || right; break;
            default: break;
        }
    }
    return true;
}

static bool process_identifier(jitc_context_t* context, token_stream_t* dest, token_stream_t* tokens, map_t* _macros, set_t* _used_macros, int recurse_limit);

static list_t* scan_macro_args(token_stream_t* tokens, bool varargs) {
    list(jitc_token_t)* arg = list_new(jitc_token_t);
    int depth = 0;
    while (true) {
        jitc_token_t* next_token = &list_get(tokens->tokens, tokens->ptr);
        if (next_token->type == TOKEN_END_OF_FILE) break;
        if (depth == 0) {
            if (next_token->type == TOKEN_COMMA && !varargs) break;
            if (next_token->type == TOKEN_PARENTHESIS_CLOSE) break;
        }
        tokens->ptr++;
        if (next_token->type == TOKEN_PARENTHESIS_OPEN) depth++;
        if (next_token->type == TOKEN_PARENTHESIS_CLOSE) depth--;
        list_add(arg) = *next_token;
    }
    return arg;
}

static void expand(jitc_context_t* context, jitc_token_t* base, token_stream_t* dest, token_stream_t* tokens, map_t* _args, map_t* macros, set_t* used_macros, int recurse_limit) {
    map(char*, list_t*)* args = _args;
    smartptr(list(jitc_token_t)) list1 = list_new(jitc_token_t);
    smartptr(list(jitc_token_t)) list2 = list_new(jitc_token_t);
    token_stream_t stream1 = {(void*)list1};
    token_stream_t stream2 = {(void*)list2};
    bool expanded = false;
    while (tokens->ptr < list_size(tokens->tokens)) {
        jitc_token_t* token = &list_get(tokens->tokens, tokens->ptr++);
        bool va_opt = token->type == TOKEN_IDENTIFIER && strcmp(token->value.string, "__VA_OPT__") == 0;
        bool va_args = token->type == TOKEN_IDENTIFIER && strcmp(token->value.string, "__VA_ARGS__") == 0;
        if (token->type == TOKEN_IDENTIFIER && args) {
            if (va_opt && next_token(TOKEN_PARENTHESIS_OPEN)) {
                int depth = 0;
                while (tokens->ptr < list_size(tokens->tokens)) {
                    const char* va_args = "__VA_ARGS__";
                    if (depth == 0 && next_token(TOKEN_PARENTHESIS_CLOSE)) break;
                    jitc_token_t* token = &list_get(tokens->tokens, tokens->ptr++);
                    if (token->type == TOKEN_PARENTHESIS_OPEN) depth++;
                    if (token->type == TOKEN_PARENTHESIS_CLOSE) depth--;
                    if (!map_find(args, &va_args) || list_size(map_get_value(args)) == 0) continue;
                    list_add(stream2.tokens) = *token;
                }
            }
            else if (map_find(args, &token->value.string)) {
                list(jitc_token_t)* list = map_get_value(args);
                for (int i = 0; i < list_size(list); i++)
                    list_add(stream2.tokens) = list_get(list, i);
            }
            else if (!va_args && !va_opt) list_add(stream2.tokens) = *token;
        }
        else list_add(stream2.tokens) = *token;
    }
    do {
        expanded = false;
        stream1.ptr = 0;
        while (list_size(stream1.tokens) > 0) list_remove(stream1.tokens, list_size(stream1.tokens) - 1); // clear 1
        for (int i = 0; i < list_size(stream2.tokens); i++) list_add(stream1.tokens) = list_get(stream2.tokens, i); // 2 -> 1
        while (list_size(stream2.tokens) > 0) list_remove(stream2.tokens, list_size(stream2.tokens) - 1); // clear 2
        while (stream1.ptr < list_size(stream1.tokens)) { // process 1 -> 2
            jitc_token_t* token = &list_get(stream1.tokens, stream1.ptr++);
            if (token->type == TOKEN_IDENTIFIER) {
                if (process_identifier(context, &stream2, &stream1, macros, used_macros, recurse_limit))
                    expanded = true;
            }
            else list_add(stream2.tokens) = *token;
        }
    }
    while (expanded);
    while (stream2.ptr < list_size(stream2.tokens)) { // 2 -> dest
        jitc_token_t token = list_get(stream2.tokens, stream2.ptr++);
        token.row = base->row;
        token.col = base->col;
        token.filename = base->filename;
        list_add(dest->tokens) = token;
    }
}

static bool process_identifier(jitc_context_t* context, token_stream_t* dest, token_stream_t* tokens, map_t* _macros, set_t* _used_macros, int recurse_limit) {
    map(char*, macro_t)* macros = _macros;
    set(char*)* used_macros = _used_macros;
    jitc_token_t* token = &list_get(tokens->tokens, tokens->ptr - 1);
    if (!map_find(macros, &token->value.string) || set_find(used_macros, &token->value.string)) {
        list_add(dest->tokens) = *token;
        return false;
    }
    macro_t* macro = &map_get_value(macros);
    switch (macro->type) {
        case MacroType_Ordinary:
            set_add(used_macros) = token->value.string;
            set_commit(used_macros);
            expand(context, token, dest, &(token_stream_t){(void*)macro->tokens}, NULL, macros, used_macros, recurse_limit);
            set_remove(used_macros, set_indexof(used_macros, &token->value.string));
            break;
        case MacroType_OrdinaryFunction: {
            jitc_token_t* id = token;
            set_add(used_macros) = id->value.string;
            set_commit(used_macros);
            map(char*, list(jitc_token_t)*)* map = NULL;
            if (next_token(TOKEN_PARENTHESIS_OPEN)) {
                map = map_new(compare_string, char*, char*);
                bool force_add = list_size(macro->tokens) > 0;
                int index = 0;
                while (true) {
                    if (!force_add && next_token(TOKEN_PARENTHESIS_CLOSE)) break;
                    smartptr(list(jitc_token_t)) arg = scan_macro_args(tokens,
                        index == list_size(macro->args) - 1 && strcmp(list_get(macro->args, index), "__VA_ARGS__") == 0
                    );
                    if (index < list_size(macro->args)) {
                        map_add(map) = list_get(macro->args, index);
                        map_commit(map);
                        map_get_value(map) = (void*)move(arg);
                        if (strcmp(list_get(macro->args, index), "__VA_ARGS__") != 0) index++;
                    }
                    if (next_token(TOKEN_COMMA)) force_add = true;
                    else if (next_token(TOKEN_PARENTHESIS_CLOSE) || 1) break;
                }
            }
            if (map) {
                expand(context, id, dest, &(token_stream_t){(void*)macro->tokens}, map, macros, used_macros, recurse_limit);
                for (size_t i = 0; i < map_size(map); i++) {
                    map_index(map, i);
                    list_delete(map_get_value(map));
                }
                map_delete(map);
            }
            else list_add(dest->tokens) = *id;
            set_remove(used_macros, set_indexof(used_macros, &id->value.string));
        } break;
        case MacroType_LINE:
            list_add(dest->tokens) = number_token(token->row);
            break;
        case MacroType_FILE:
            list_add(dest->tokens) = string_token(token->filename ?: "<memory>");
            break;
        case MacroType_DATE: {
            struct tm* t = localtime((time_t[]){time(NULL)});
            char formatted[256];
            sprintf(formatted, "%s %2d %d", (const char*[]){
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
            }[t->tm_mon], t->tm_mday, t->tm_year + 1900);
            list_add(dest->tokens) = string_token(jitc_append_string(context, formatted));
        } break;
        case MacroType_TIME: {
            struct tm* t = localtime((time_t[]){time(NULL)});
            char formatted[256];
            sprintf(formatted, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            list_add(dest->tokens) = string_token(jitc_append_string(context, formatted));
        } break;
        case MacroType_ID: {
            smartptr(string_t) new_id = NULL;
            if (next_token(TOKEN_PARENTHESIS_OPEN)) {
                new_id = str_new();
                int depth = 0;
                while (tokens->ptr < list_size(tokens->tokens)) {
                    jitc_token_t* next = &list_get(tokens->tokens, tokens->ptr++);
                    if (depth == 0 && next->type == TOKEN_PARENTHESIS_CLOSE) break;
                    if (next->type == TOKEN_PARENTHESIS_OPEN) depth++;
                    if (next->type == TOKEN_PARENTHESIS_CLOSE) depth--;
                    if (depth != 0) continue;
                    if (next->type == TOKEN_IDENTIFIER) str_append(new_id, next->value.string);
                    if (next->type == TOKEN_INTEGER) str_appendf(new_id, "%lu", next->value.integer);
                }
                list_add(dest->tokens) = identifier_token(jitc_append_string(context, str_data(new_id)));
            }
            else list_add(dest->tokens) = *token;
        }
    }
    return true;
}

queue_t* jitc_preprocess(jitc_context_t* context, queue_t* _token_queue, map_t* _macros) {
    typedef struct {
        jitc_token_t* start;
        enum {
            Cond_WillExpand,
            Cond_Expanding,
            Cond_Expanded,
        } state;
        bool has_else;
    } cond_t;

    map(char*, macro_t)* macros = _macros;
    queue(jitc_token_t)* token_queue = _token_queue;
    smartptr(list(jitc_token_t)) result = list_new(jitc_token_t);
    smartptr(list(jitc_token_t)) tokens = list_new(jitc_token_t);
    smartptr(stack(cond_t)) cond_stack = stack_new(cond_t);
    smartptr(map(char*, macro_t)) __macros = NULL;
    if (!macros) macros = (void*)(__macros = map_new(compare_string, char*, macro_t));
    while (queue_size(token_queue) > 0) list_add(tokens) = queue_pop(token_queue);
    queue_delete(token_queue);
    predefine(macros);
    token_stream_t stream = {(void*)tokens};
    token_stream_t out_stream = {(void*)result};
    int curr_line = 0;
    while (stream.ptr < list_size(stream.tokens)) {
        jitc_token_t* token = &list_get(stream.tokens, stream.ptr++);
        bool do_things = true;
        if (stack_size(cond_stack) > 0) do_things = stack_peek(cond_stack).state == Cond_Expanding;
        if (token->type == TOKEN_HASHTAG && curr_line != token->row) {
            curr_line = token->row;
            token = optional(advance(&stream, &curr_line), continue);
            if (is_identifier(token, "define")) {
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                macro_t* macro = new_macro(macros, token->value.string, MacroType_Ordinary);
                jitc_token_t* paren = lookahead(&stream, curr_line);
                if (paren && paren->type == TOKEN_PARENTHESIS_OPEN && paren->row == token->row && paren->col == token->col + strlen(token->value.string)) {
                    advance(&stream, &curr_line);
                    smartptr(list(char*)) list = list_new(char*);
                    macro->type = MacroType_OrdinaryFunction;
                    if ((token = lookahead(&stream, curr_line)) && token->type != TOKEN_PARENTHESIS_CLOSE) while (true) {
                        token = expect(advance(&stream, &curr_line), "Unexpected EOL");
                        const char* name = NULL;
                        bool vararg = false;
                        if (token->type == TOKEN_TRIPLE_DOT) {
                            vararg = true;
                            name = "__VA_ARGS__";
                        }
                        else {
                            name = token->value.string;
                            if (strcmp(name, "__VA_ARGS__") == 0) throw(token, "Usage of reserved argument name: __VA_ARGS__");
                            if (strcmp(name, "__VA_OPT__") == 0) throw(token, "Usage of reserved argument name: __VA_OPT__");
                        }
                        list_add(list) = (char*)name;
                        token = expect(advance(&stream, &curr_line), "Unexpected EOL");
                        if (token->type == TOKEN_COMMA && !vararg) continue;
                        if (token->type == TOKEN_PARENTHESIS_CLOSE) break;
                        throw(token, "Expected %s')'", vararg ? "" : "',' or ");
                    }
                    else advance(&stream, &curr_line);
                    macro->args = (void*)move(list);
                }
                while ((token = advance(&stream, &curr_line))) list_add(macro->tokens) = *token;
            }
            else if (is_identifier(token, "undef")) {
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                map_find(macros, &token->value.string);
                map_remove(macros);
            }
            else if (is_identifier(token, "include") || is_identifier(token, "embed")) {
                token = expect_and(advance(&stream, &curr_line),
                    this->type == TOKEN_STRING ||
                    this->type == TOKEN_IDENTIFIER,
                "Expected string");
                token_stream_t macro_stream;
                token_stream_t* curr_stream = &stream;
                if (token->type == TOKEN_IDENTIFIER) {
                    curr_stream = &macro_stream;
                    smartptr(set(char*)) used_macros = set_new(compare_string, char*);
                    process_identifier(context, curr_stream, &stream, macros, used_macros, 0);
                    token = &list_get(curr_stream->tokens, curr_stream->ptr++);
                }
                char* filename = NULL;
                if (token->type == TOKEN_STRING) filename = token->value.string;
                else throw(token, "Expected string");
                queue(jitc_token_t)* included = try(jitc_include(context, token, filename, macros));
                while (queue_size(included) > 1) list_add(out_stream.tokens) = queue_pop(included);
                // skip over EOF token
            }
            else if (is_identifier(token, "if")) {
                smartptr(list(jitc_token_t)) list = list_new(jitc_token_t);
                token_stream_t expr = {(void*)list};
                while ((token = advance(&stream, &curr_line))) list_add(expr.tokens) = *token;
                int64_t value = 0;
                try(compute_expression(context, macros, &expr, &value, 1));
                cond_t* cond = &stack_push(cond_stack);
                cond->start = token;
                cond->state = do_things ? value != 0 ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
                cond->has_else = false;
            }
            else if (is_identifier(token, "elif")) {
                if (stack_size(cond_stack) == 0) throw(token, "else without if");
                smartptr(list(jitc_token_t)) list = list_new(jitc_token_t);
                token_stream_t expr = {(void*)list};
                while ((token = advance(&stream, &curr_line))) list_add(expr.tokens) = *token;
                int64_t value;
                try(compute_expression(context, macros, &expr, &value, 1));
                cond_t* cond = &stack_peek(cond_stack);
                if (cond->has_else) throw(token, "Duplicate else");
                cond->start = token;
                cond->state = cond->state != Cond_Expanded ? value != 0 ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
            }
            else if (is_identifier(token, "else")) {
                if (stack_size(cond_stack) == 0) throw(token, "else without if");
                cond_t* cond = &stack_peek(cond_stack);
                if (cond->has_else) throw(token, "Duplicate else");
                cond->has_else = true;
                cond->state = cond->state == Cond_WillExpand ? Cond_Expanding : Cond_Expanded;
            }
            else if (is_identifier(token, "endif")) {
                if (stack_size(cond_stack) == 0) throw(token, "endif without if");
                stack_pop(cond_stack);
            }
            else if ((is_identifier(token, "ifdef") || is_identifier(token, "ifndef"))) {
                bool negative = is_identifier(token, "ifndef");
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                bool pass = !!map_find(macros, &token->value.string) ^ negative;
                cond_t* cond = &stack_push(cond_stack);
                cond->start = token;
                cond->state = do_things ? pass ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
                cond->has_else = false;
            }
            else if ((is_identifier(token, "elifdef")) || (is_identifier(token, "elifndef"))) {
                if (stack_size(cond_stack) == 0) throw(token, "else without if");
                bool negative = is_identifier(token, "ifndef");
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                bool pass = !!map_find(macros, token->value.string) ^ negative;
                cond_t* cond = &stack_peek(cond_stack);
                if (cond->has_else) throw(token, "Duplicate else");
                cond->start = token;
                cond->state = cond->state != Cond_Expanded ? pass ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
            }
            else throw(token, "Invalid preprocessor directive");
            while (advance(&stream, &curr_line));
        }
        else if (do_things) {
            curr_line = token->row;
            smartptr(set(char*)) used_macros = set_new(compare_string, char*);
            if (token->type == TOKEN_IDENTIFIER) process_identifier(context, &out_stream, &stream, macros, used_macros, 0);
            else list_add(out_stream.tokens) = *token;
        }
        else curr_line = token->row;
    }
    if (stack_size(cond_stack) > 0) throw(stack_peek(cond_stack).start, "Unterminated condition");
    queue(jitc_token_t)* queue = queue_new(jitc_token_t);
    for (size_t i = 0; i < list_size(result); i++) {
        jitc_token_t* token = &list_get(result, i);
        if (token->type == TOKEN_STRING && list_get(result, i + 1).type == TOKEN_STRING) {
            string_t* str = str_new();
            int row = token->row; int col = token->col; char* filename = token->filename;
            while (token->type == TOKEN_STRING) {
                str_append(str, token->value.string);
                token = &list_get(result, ++i);
            }
            i--;
            token = &string_token(jitc_append_string(context, str_data(str)));
            token->row = row; token->col = col; token->filename = filename;
            queue_push(queue) = *token;
            str_delete(str);
            continue;
        }
        for (int i = 0; i < num_token_table_entries && token->type == TOKEN_IDENTIFIER; i++) {
            if (!token_table[i]) continue;
            if (strcmp(token->value.string, token_table[i]) == 0) {
                token->type = i;
                break;
            }
        }
        queue_push(queue) = *token;
    }
    return queue;
}
