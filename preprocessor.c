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
    MacroType_DEFINE,
    MacroType_UNDEF,
    MacroType_RECURSE,
    MacroType_IF,
    MacroType_EVAL,
} macro_type_t;

typedef struct {
    macro_type_t type;
    list_t* tokens;
    list_t* args;
} macro_t;

typedef struct {
    list_t* tokens;
    int ptr;
} token_stream_t;

static jitc_token_t* number_token(uint64_t value) {
    jitc_token_t* token = calloc(sizeof(jitc_token_t), 1);
    token->type = TOKEN_INTEGER;
    token->value.integer = value;
    token->flags.int_flags.type_kind = Type_Int32;
    token->flags.int_flags.is_unsigned = false;
    return token;
}

static jitc_token_t* string_token(const char* value) {
    jitc_token_t* token = calloc(sizeof(jitc_token_t), 1);
    token->type = TOKEN_STRING;
    token->value.string = (char*)value;
    return token;
}

static jitc_token_t* copy_token(jitc_token_t* token) {
    jitc_token_t* copy = malloc(sizeof(jitc_token_t));
    memcpy(copy, token, sizeof(jitc_token_t));
    return copy;
}

static macro_t* new_macro(map_t* macros, const char* name, macro_type_t type) {
    macro_t* macro = calloc(sizeof(macro_t), 1);
    macro->type = type;
    if (type == MacroType_Ordinary || type == MacroType_OrdinaryFunction)
        macro->tokens = list_new();
    if (type == MacroType_OrdinaryFunction)
        macro->args = list_new();
    map_get_ptr(macros, (char*)name);
    map_store_ptr(macros, macro);
    return macro;
}

static void predefine(map_t* macros) {
    list_add_ptr(new_macro(macros, "__STDC_HOSTED__", MacroType_Ordinary)->tokens, number_token(1));
    list_add_ptr(new_macro(macros, "__STDC_NO_ATOMICS__", MacroType_Ordinary)->tokens, number_token(1));
    list_add_ptr(new_macro(macros, "__STDC_NO_COMPLEX__", MacroType_Ordinary)->tokens, number_token(1));
    list_add_ptr(new_macro(macros, "__STDC_NO_THREADS__", MacroType_Ordinary)->tokens, number_token(1));
    list_add_ptr(new_macro(macros, "__STDC_NO_VLA__", MacroType_Ordinary)->tokens, number_token(1));
    list_add_ptr(new_macro(macros, "__JITC__", MacroType_Ordinary)->tokens, number_token(1));
#ifdef __x86_64__
    list_add_ptr(new_macro(macros, "__x86_64__", MacroType_Ordinary)->tokens, number_token(1));
#endif
#ifdef __aarch64__
    list_add_ptr(new_macro(macros, "__aarch64__", MacroType_Ordinary)->tokens, number_token(1));
#endif
#ifdef _WIN32
    list_add_ptr(new_macro(macros, "_WIN32", MacroType_Ordinary)->tokens, number_token(1));
#endif
#ifdef __APPLE__
    list_add_ptr(new_macro(macros, "__APPLE__", MacroType_Ordinary)->tokens, number_token(1));
#endif
#ifdef __linux__
    list_add_ptr(new_macro(macros, "__linux__", MacroType_Ordinary)->tokens, number_token(1));
#endif
#ifdef __unix__
    list_add_ptr(new_macro(macros, "__unix__", MacroType_Ordinary)->tokens, number_token(1));
#endif
    new_macro(macros, "__LINE__", MacroType_LINE);
    new_macro(macros, "__FILE__", MacroType_FILE);
    new_macro(macros, "__DATE__", MacroType_DATE);
    new_macro(macros, "__TIME__", MacroType_TIME);
    new_macro(macros, "__DEFINE__", MacroType_DEFINE);
    new_macro(macros, "__UNDEF__", MacroType_UNDEF);
    new_macro(macros, "__RECURSE__", MacroType_RECURSE);
    new_macro(macros, "__IF__", MacroType_IF);
    new_macro(macros, "__EVAL__", MacroType_EVAL);
}

static jitc_token_t* advance(token_stream_t* tokens, int* curr_line) {
    jitc_token_t* token = list_get_ptr(tokens->tokens, tokens->ptr);
    if (token->type == TOKEN_END_OF_FILE) return NULL;
    while (true) {
        if (token->row != *curr_line) return NULL;
        if (token->type == TOKEN_BACKSLASH) {
            jitc_token_t* backslash = list_get_ptr(tokens->tokens, tokens->ptr++);
            token = list_get_ptr(tokens->tokens, tokens->ptr);
            if (token->row == *curr_line) return backslash;
            else (*curr_line)++;
        }
        else return list_get_ptr(tokens->tokens, tokens->ptr++);
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

static void run_macro(jitc_context_t* context, token_stream_t* dest, token_stream_t* tokens, map_t* macros);
static bool compute_expression(jitc_context_t* context, map_t* macros, token_stream_t* stream, int64_t* left, int min_prec);

static bool get_operand(jitc_context_t* context, map_t* macros, token_stream_t* stream, int64_t* value) {
    jitc_token_t* token;
    smartptr(stack_t) stack = stack_new();
    while (true) {
        token = expect(list_get_ptr(stream->tokens, stream->ptr++), "Expected operand");
        if (
            token->type == TOKEN_MINUS ||
            token->type == TOKEN_PLUS ||
            token->type == TOKEN_TILDE ||
            token->type == TOKEN_EXCLAMATION_MARK
        ) stack_push_int(stack, token->type);
        else break;
    }
    if (is_identifier(token, "defined")) {
        token = expect_and(
            (jitc_token_t*)list_get_ptr(stream->tokens, stream->ptr++),
            this->type == TOKEN_IDENTIFIER || this->type == TOKEN_PARENTHESIS_OPEN,
            "Macro name expected"
        );
        const char* name = NULL;
        if (token->type == TOKEN_IDENTIFIER) name = token->value.string;
        else if (token->type == TOKEN_PARENTHESIS_OPEN) {
            token = expect_and((jitc_token_t*)list_get_ptr(stream->tokens, stream->ptr++), this->type == TOKEN_IDENTIFIER, "Macro name expected");
            name = token->value.string;
            token = expect_and((jitc_token_t*)list_get_ptr(stream->tokens, stream->ptr++), this->type == TOKEN_PARENTHESIS_CLOSE, "Expected ')'");
        }
        if (map_find_ptr(macros, (char*)name)) *value = 1;
        else *value = 0;
    }
    else if (token->type == TOKEN_INTEGER) *value = token->value.integer;
    else if (token->type == TOKEN_PARENTHESIS_OPEN) {
        try(compute_expression(context, macros, stream, value, 1));
        expect_and((jitc_token_t*)list_get_ptr(stream->tokens, stream->ptr++), this->type == TOKEN_PARENTHESIS_CLOSE, "Expected ')'");
    }
    else throw(token, "Expected operand");
    while (stack_size(stack)) {
        jitc_token_type_t type = stack_pop_int(stack);
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
        jitc_token_t* token = optional(list_get_ptr(stream->tokens, stream->ptr), break);
        int precedence = op_info[token->type].precedence;
        if (precedence < min_prec) break;
        stream->ptr++;
        if (token->type == TOKEN_QUESTION_MARK) {
            int64_t then, otherwise;
            try(compute_expression(context, macros, stream, &then, precedence));
            expect_and((jitc_token_t*)list_get_ptr(stream->tokens, stream->ptr++), this->type == TOKEN_COLON, "Expected ':'");
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

static void expand(jitc_context_t* context, jitc_token_t* base, map_t* macros, token_stream_t* dest, token_stream_t* tokens, map_t* args, list_t* varargs, set_t* used_macros, int recurse_uses) {
    // todo: expand and rescan
    while (tokens->ptr < list_size(tokens->tokens)) {
        jitc_token_t* token = copy_token(list_get_ptr(tokens->tokens, tokens->ptr++));
        token->row = base->row;
        token->col = base->col;
        list_add_ptr(dest->tokens, token);
    }
}

static void process_identifier(jitc_context_t* context, token_stream_t* dest, token_stream_t* tokens, map_t* macros, set_t* used_macros, int recurse_uses) {
    if (!used_macros) used_macros = set_new(compare_string);
    jitc_token_t* token = list_get_ptr(tokens->tokens, tokens->ptr - 1);
    if (set_find_ptr(used_macros, token->value.string) || !map_find_ptr(macros, token->value.string)) {
        list_add_ptr(dest->tokens, copy_token(token));
        return;
    }
    macro_t* macro = map_as_ptr(macros);
    switch (macro->type) {
        case MacroType_Ordinary:
            set_add_ptr(used_macros, token->value.string);
            expand(context, token, macros, dest, &(token_stream_t){macro->tokens}, NULL, NULL, used_macros, recurse_uses);
            set_remove_ptr(used_macros, token->value.string);
            break;
        case MacroType_OrdinaryFunction: {
            // todo
        } break;
        case MacroType_LINE:
            list_add_ptr(dest->tokens, number_token(token->row));
            break;
        case MacroType_FILE:
            list_add_ptr(dest->tokens, string_token(token->filename ?: "<memory>"));
            break;
        case MacroType_DATE: {
            struct tm* t = localtime((time_t[]){time(NULL)});
            char formatted[256];
            sprintf(formatted, "%s %2d %d", (const char*[]){
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
            }[t->tm_mon], t->tm_mday, t->tm_year + 1900);
            list_add_ptr(dest->tokens, string_token(jitc_append_string(context, formatted)));
        } break;
        case MacroType_TIME: {
            struct tm* t = localtime((time_t[]){time(NULL)});
            char formatted[256];
            sprintf(formatted, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            list_add_ptr(dest->tokens, string_token(jitc_append_string(context, formatted)));
        } break;
        case MacroType_DEFINE: {
            // todo
        } break;
        case MacroType_UNDEF: {
            // todo
        } break;
        case MacroType_RECURSE: {
            // todo
        } break;
        case MacroType_IF: {
            // todo
        } break;
        case MacroType_EVAL: {
            // todo
        } break;
    }
}

static void run_macro(jitc_context_t* context, token_stream_t* dest, token_stream_t* tokens, map_t* macros) {
    process_identifier(context, dest, tokens, macros, NULL, 0);
}

queue_t* jitc_preprocess(jitc_context_t* context, queue_t* token_queue, map_t* macros) {
    typedef struct {
        jitc_token_t* start;
        enum {
            Cond_WillExpand,
            Cond_Expanding,
            Cond_Expanded,
        } state;
        bool has_else;
    } cond_t;

    smartptr(list_t) result = list_new();
    smartptr(list_t) tokens = list_new();
    smartptr(stack_t) cond_stack = stack_new();
    smartptr(map_t) _macros = NULL;
    if (!macros) macros = _macros = map_new(compare_string);
    while (queue_size(token_queue) > 0) list_add_ptr(tokens, queue_pop_ptr(token_queue));
    queue_delete(token_queue);
    predefine(macros);
    token_stream_t stream = {tokens};
    token_stream_t out_stream = {result};
    int curr_line = 0;
    while (stream.ptr < list_size(stream.tokens)) {
        jitc_token_t* token = list_get_ptr(stream.tokens, stream.ptr++);
        bool do_things = true;
        if (stack_size(cond_stack) > 0) do_things = ((cond_t*)stack_peek_ptr(cond_stack))->state == Cond_Expanding;
        if (token->type == TOKEN_HASHTAG && curr_line != token->row) {
            curr_line = token->row;
            token = optional(advance(&stream, &curr_line), continue);
            if (is_identifier(token, "define")) {
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                macro_t* macro = new_macro(macros, token->value.string, MacroType_Ordinary);
                while ((token = advance(&stream, &curr_line))) list_add_ptr(macro->tokens, token);
            }
            else if (is_identifier(token, "undef")) {
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                map_find_ptr(macros, token->value.string);
                map_remove(macros);
            }
            else if (is_identifier(token, "include") || is_identifier(token, "embed")) {
                // todo: support <FILENAME>
                token = expect_and(advance(&stream, &curr_line),
                    this->type == TOKEN_STRING ||
                    this->type == TOKEN_IDENTIFIER,
                "Expected string");
                token_stream_t macro_stream;
                token_stream_t* curr_stream = &stream;
                if (token->type == TOKEN_IDENTIFIER) {
                    curr_stream = &macro_stream;
                    run_macro(context, curr_stream, &stream, macros);
                    token = list_get_ptr(curr_stream->tokens, curr_stream->ptr++);
                }
                char* filename = NULL;
                if (token->type == TOKEN_STRING) filename = token->value.string;
                else throw(token, "Expected string");
                queue_t* included = try(jitc_include(context, token, filename, macros));
                while (queue_size(included) > 1) list_add_ptr(out_stream.tokens, queue_pop_ptr(included));
                // skip over EOF token
            }
            else if (is_identifier(token, "if")) {
                smartptr(list_t) list = list_new();
                token_stream_t expr = {list};
                while ((token = advance(&stream, &curr_line))) list_add_ptr(expr.tokens, token);
                int64_t value = 0;
                try(compute_expression(context, macros, &expr, &value, 1));
                cond_t* cond = malloc(sizeof(cond_t));
                cond->start = token;
                cond->state = do_things ? value != 0 ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
                cond->has_else = false;
                stack_push_ptr(cond_stack, move(cond));
            }
            else if (is_identifier(token, "elif")) {
                if (stack_size(cond_stack) == 0) throw(token, "else without if");
                smartptr(list_t) list = list_new();
                token_stream_t expr = {list};
                while ((token = advance(&stream, &curr_line))) list_add_ptr(expr.tokens, token);
                int64_t value;
                try(compute_expression(context, macros, &expr, &value, 1));
                cond_t* cond = stack_peek_ptr(cond_stack);
                if (cond->has_else) throw(token, "Duplicate else");
                cond->start = token;
                cond->state = cond->state != Cond_Expanded ? value != 0 ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
            }
            else if (is_identifier(token, "else")) {
                if (stack_size(cond_stack) == 0) throw(token, "else without if");
                cond_t* cond = stack_peek_ptr(cond_stack);
                if (cond->has_else) throw(token, "Duplicate else");
                cond->has_else = true;
                cond->state = cond->state != Cond_Expanded ? Cond_Expanding : Cond_Expanded;
            }
            else if (is_identifier(token, "endif")) {
                if (stack_size(cond_stack) == 0) throw(token, "endif without if");
                free(stack_pop_ptr(cond_stack));
            }
            else if ((is_identifier(token, "ifdef") || is_identifier(token, "ifndef"))) {
                bool negative = is_identifier(token, "ifndef");
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                bool pass = !!map_find_ptr(macros, token->value.string) ^ negative;
                cond_t* cond = malloc(sizeof(cond_t));
                cond->start = token;
                cond->state = do_things ? pass ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
                cond->has_else = false;
                stack_push_ptr(cond_stack, move(cond));
            }
            else if ((is_identifier(token, "elifdef")) || (is_identifier(token, "elifndef"))) {
                if (stack_size(cond_stack) == 0) throw(token, "else without if");
                bool negative = is_identifier(token, "ifndef");
                token = expect_and(advance(&stream, &curr_line), this->type == TOKEN_IDENTIFIER, "Expected identifier");
                bool pass = !!map_find_ptr(macros, token->value.string) ^ negative;
                cond_t* cond = stack_peek_ptr(cond_stack);
                if (cond->has_else) throw(token, "Duplicate else");
                cond->start = token;
                cond->state = cond->state != Cond_Expanded ? pass ? Cond_Expanding : Cond_WillExpand : Cond_Expanded;
            }
            else throw(token, "Invalid preprocessor directive");
            while (advance(&stream, &curr_line));
        }
        else if (do_things) {
            curr_line = token->row;
            if (token->type == TOKEN_IDENTIFIER) run_macro(context, &out_stream, &stream, macros);
            else list_add_ptr(out_stream.tokens, copy_token(token));
        }
        else curr_line = token->row;
    }
    if (stack_size(cond_stack) > 0) throw(((cond_t*)stack_peek_ptr(cond_stack))->start, "Unterminated condition");
    queue_t* queue = queue_new();
    for (size_t i = 0; i < list_size(result); i++) {
        jitc_token_t* token = list_get_ptr(result, i);
        if (token->type == TOKEN_STRING && ((jitc_token_t*)list_get_ptr(result, i + 1))->type == TOKEN_STRING) {
            string_t* str = str_new();
            int row = token->row; int col = token->col; char* filename = token->filename;
            while (token->type == TOKEN_STRING) {
                str_append(str, token->value.string);
                free(token);
                token = list_get_ptr(result, ++i);
            }
            i--;
            token = string_token(jitc_append_string(context, str_data(str)));
            token->row = row; token->col = col; token->filename = filename;
            queue_push_ptr(queue, token);
            str_delete(str);
            continue;
        }
        queue_push_ptr(queue, list_get_ptr(result, i));
    }
    for (size_t i = 0; i < list_size(tokens); i++) {
        free(list_get_ptr(tokens, i));
    }
    return queue;
}
