#include "jitc_internal.h"
#include "dynamics.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

static bool is_alphanumeric(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_numeric(char c) {
    return c >= '0' && c <= '9';
}

static bool is_symbol(char c) {
    return c > ' ' && c <= '~' && !is_alphanumeric(c);
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

static bool get_octal(char c, int* out) {
    if (c >= '0' && c <= '7') *out = c - '0';
    else return false;
    return true;
}

static bool get_decimal(char c, int* out) {
    if (c >= '0' && c <= '9') *out = c - '0';
    else return false;
    return true;
}

static bool get_hexadecimal(char c, int* out) {
    if      (c >= '0' && c <= '9') *out = c - '0';
    else if (c >= 'a' && c <= 'f') *out = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') *out = c - 'A' + 10;
    else return false;
    return true;
}

static void encode_utf8(string_t* str, uint32_t value) {
    if      (value <= 0x7F)     str_append(str, (char[]){ value, 0 });
    else if (value <= 0x7FF)    str_append(str, (char[]){ 0xC0 | (value >> 6), 0x80  | (value & 0x3F), 0 });
    else if (value <= 0xFFFF)   str_append(str, (char[]){ 0xE0 | (value >> 12), 0x80 | ((value >> 6)  & 0x3F), 0x80 | (value & 0x3F), 0 });
    else if (value <= 0x10FFFF) str_append(str, (char[]){ 0xF0 | (value >> 18), 0x80 | ((value >> 12) & 0x3F), 0x80 | ((value >> 6) & 0x3F), 0x80 | (value & 0x3F) });
}

static int decode_utf8(char* seq, int* out) {
    unsigned char* bytes = (unsigned char*)seq;
    if ((bytes[0] & 0x80) == 0x00) {
        *out = bytes[0];
        return 1;
    }
    else if ((bytes[0] & 0xE0) == 0xC0) {
        *out = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        return 2;
    }
    else if ((bytes[0] & 0xF0) == 0xE0) {
        *out = ((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        return 3;
    }
    else if ((bytes[0] & 0xF8) == 0xF0) {
        *out = ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12) | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
        return 4;
    }
    return 0;
}

int hi = 1LLU;

static bool try_parse_int(string_t* string, uint64_t* out, jitc_token_flags_t* flags) {
    int base = 10;
    int ptr = 0;
    if (str_data(string)[0] == '0') {
        ptr = 2;
        switch (str_data(string)[1]) {
            case 0:             ptr = 0;   break;
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base = 2;  break;
            case '0'...'7':     base = 8; ptr = 1; break;
            default: return false;
        }
    }
    if (str_data(string)[ptr] == 0) return false;
    char last_suffix = 0;
    bool is_unsigned = false;
    bool any_digits = false;
    int longs = 0;
    *out = 0;
    for (; str_data(string)[ptr]; ptr++) {
        int digit;
        if (any_digits) {
            if (str_data(string)[ptr] == 'U' || str_data(string)[ptr] == 'u') {
                if (is_unsigned) return false;
                last_suffix = str_data(string)[ptr];
                is_unsigned = true;
                continue;
            }
            if (str_data(string)[ptr] == 'L' || str_data(string)[ptr] == 'l') {
                if ((last_suffix == 'u' || last_suffix == 'U') && longs == 1) return false;
                if (longs == 2) return false;
                last_suffix = str_data(string)[ptr];
                longs++;
                continue;
            }
        }
        if (is_unsigned || longs != 0) return false;
        if (!get_hexadecimal(str_data(string)[ptr], &digit)) return false;
        if (digit >= base) return false;
        any_digits = true;
        *out = *out * base + digit;
    }
    if (flags) {
        flags->int_flags.is_unsigned = is_unsigned;
        flags->int_flags.type_kind = longs != 0 ? Type_Int64 : Type_Int32;
    }
    return true;
}

static bool try_parse_flt(string_t* string, double* out, jitc_token_flags_t* flags) {
    int base = 10;
    int ptr = 0;
    int exp = 0;
    int frac = 0;
    bool neg_exp = false;
    bool hex = false;
    bool is_float = false;
    enum {
        Integer,
        Fraction,
        Exponent,
        ExponentFirstChar,
        ExponentFirstCharNoSign,
    } state = Integer;
    if (str_data(string)[0] == '0' && (str_data(string)[1] == 'x' || str_data(string)[1] == 'X')) {
        ptr = 2;
        base = 16;
        hex = true;
    }
    if (str_data(string)[ptr] == 0) return false;
    *out = 0;
    for (; str_data(string)[ptr]; ptr++) {
        if (is_float) return false;
        int digit = 0;
        bool valid = get_hexadecimal(str_data(string)[ptr], &digit) && digit < base;
        if ((str_data(string)[ptr] == 'f' || str_data(string)[ptr] == 'F') && state != Integer) {
            is_float = true;
            continue;
        }
        switch (state) {
            case Integer:
                if (str_data(string)[ptr] == '.') state = Fraction;
                else if (((str_data(string)[ptr] == 'e' || str_data(string)[ptr] == 'E') && !hex) || ((str_data(string)[ptr] == 'p' || str_data(string)[ptr] == 'P') && hex)) {
                    state = ExponentFirstChar;
                    base = 10;
                }
                else if (valid) *out = *out * base + digit;
                else return false;
                break;
            case Fraction:
                if (((str_data(string)[ptr] == 'e' || str_data(string)[ptr] == 'E') && !hex) || ((str_data(string)[ptr] == 'p' || str_data(string)[ptr] == 'P') && hex)) {
                    state = ExponentFirstChar;
                    base = 10;
                }
                else if (valid) *out += digit * pow(base, -(++frac));
                else return false;
                break;
            case Exponent:
            case ExponentFirstChar:
            case ExponentFirstCharNoSign:
                if ((str_data(string)[ptr] == '-' || str_data(string)[ptr] == '+') && state == ExponentFirstChar) {
                    neg_exp = str_data(string)[ptr] == '-';
                    state = ExponentFirstCharNoSign;
                    continue;
                }
                if (!get_decimal(str_data(string)[ptr], &digit)) return false;
                if (valid) exp = exp * base + digit;
                else return false;
                state = Exponent;
        }
    }
    *out *= pow(hex ? 2 : 10, neg_exp ? -exp : exp);
    if (flags) flags->float_flags.is_single_precision = is_float;
    return (!hex && (state == Integer || state == Fraction || state == Exponent)) || (hex && state == Exponent);
}

static jitc_token_t* mktoken(queue_t* _tokens, jitc_token_type_t type, const char* filename, int row, int col) {
    queue(jitc_token_t)* tokens = _tokens;
    jitc_token_t* token = &queue_push(tokens);
    token->type = type;
    token->filename = (char*)filename;
    token->row = row;
    token->col = col;
    return token;
}

queue_t* jitc_lex(jitc_context_t* context, const char* code, const char* filename) {
    char c;
    size_t ptr = 0;
    bool no_increment = false;
    bool zero = false;
    bool asterisk = false;
    int digit = 0;
    int row = 1, col = 0;
    char* file = jitc_append_string(context, filename);
    smartptr(queue(jitc_token_t)) tokens = queue_new(jitc_token_t);
    struct {
        enum State {
            Idle,
            ParsingWord,
            ParsingSymbol,
            ParsingStringLiteral,
            ParsingCharLiteral,
            ParsingComment,
            ParsingMultilineComment
        } parse_state;
        string_t* buffer;
        int row, col;
        union {
            struct {
                int num_digits;
                uint32_t value;
                enum {
                    None,
                    CharStart,
                    Backslash,
                    Octal,
                    Hex2,
                    Hex4,
                    Hex8,
                } state;
            } string_state;
            struct {
                bool first_char;
                bool dot_in_word;
                bool can_have_sign;
                bool can_be_sign;
            } word_state;
        };
    } state;
    smartptr(string_t) _buffer;
    memset(&state, 0, sizeof(state));
    _buffer = state.buffer = str_new();
    while (ptr == 0 || code[ptr - 1]) {
        c = code[no_increment ? ptr - 1 : ptr++];
        if (c == 0) c = '\n'; // basically inserts a new line at the end of files
        if (!no_increment) {
            col++;
            if (c == '\n') {
                col = 0;
                row++;
            }
        }
        no_increment = false;
        if (state.parse_state == ParsingComment) {
            if (c == '\n') state.parse_state = Idle;
        }
        else if (state.parse_state == ParsingMultilineComment) {
            if (c == '/' && asterisk) state.parse_state = Idle;
            asterisk = c == '*';
        }
        else if (state.parse_state == Idle) {
            if      (c == '"' )                       state.parse_state = ParsingStringLiteral;
            else if (c == '\'')                       state.parse_state = ParsingCharLiteral;
            else if (is_alphanumeric(c) || (c == '.' && is_numeric(code[ptr]))) state.parse_state = ParsingWord;
            else if (is_symbol      (c)) state.parse_state = ParsingSymbol;
            else if (is_whitespace  (c)) continue;
            else { jitc_error_set(context, jitc_error_syntax(file, row, col, "Invalid codepoint: \\x%02x", c)); return NULL; }
            str_clear(state.buffer);
            state.row = row;
            state.col = col;
            state.string_state.state = state.parse_state == ParsingCharLiteral ? CharStart : None;
            if (state.parse_state == ParsingWord || state.parse_state == ParsingSymbol) no_increment = true;
            if (state.parse_state == ParsingWord) state.word_state.first_char = true;
        }
        else if (state.parse_state == ParsingStringLiteral || state.parse_state == ParsingCharLiteral) {
            switch (state.string_state.state) {
                case CharStart:
                    if (c == '\'') { jitc_error_set(context, jitc_error_syntax(file, row, col, "Empty char literal")); return NULL; }
                    state.string_state.state = None;
                case None:
                    if (c == '\\') state.string_state.state = Backslash;
                    else if (c == '"' && state.parse_state == ParsingStringLiteral) {
                        jitc_token_t* token = mktoken(tokens, TOKEN_STRING, file, state.row, state.col);
                        token->value.string = jitc_append_string(context, str_data(state.buffer));
                        state.parse_state = Idle;
                    }
                    else if (c == '\'' && state.parse_state == ParsingCharLiteral) {
                        char* data = str_data(state.buffer);
                        jitc_token_t* token = mktoken(tokens, TOKEN_INTEGER, file, state.row, state.col);
                        token->flags.int_flags.type_kind = Type_Int32;
                        token->flags.int_flags.is_unsigned = false;
                        data += decode_utf8(str_data(state.buffer), (int*)&token->value.integer);
                        memset((char*)&token->value.integer + 1, (token->value.integer & (1 << 7)) ? 0xFF : 0x00, 7);
                        if (*data != 0) { jitc_error_set(context, jitc_error_syntax(file, row, col, "Multiple characters in char literal")); return NULL; }
                        state.parse_state = Idle;
                    }
                    else str_append(state.buffer, (char[]){ c, 0 });
                    break;
                case Backslash:
                    state.string_state.num_digits = 0;
                    state.string_state.value = 0;
                    switch (c) {
                        case 'a':  c = '\a'; break;
                        case 'b':  c = '\b'; break;
                        case 'e':  c = '\e'; break;
                        case 'f':  c = '\f'; break;
                        case 'n':  c = '\n'; break;
                        case 'r':  c = '\r'; break;
                        case 't':  c = '\t'; break;
                        case 'v':  c = '\v'; break;
                        case '"':  c = '"';  break;
                        case '\\': c = '\\'; break;
                        case '\'': c = '\''; break;
                        case 'x':       state.string_state.state = Hex2;  break;
                        case 'u':       state.string_state.state = Hex4;  break;
                        case 'U':       state.string_state.state = Hex8;  break;
                        case '0'...'7': state.string_state.state = Octal; no_increment = true; break;
                        case '\n': c = '\n'; break;
                        default: { jitc_error_set(context, jitc_error_syntax(file, row, col, "Invalid escape code")); return NULL; }
                    }
                    if (state.string_state.state == Backslash) {
                        str_append(state.buffer, (char[]){ c, 0 });
                        state.string_state.state = None;
                    }
                    break;
                case Octal:
                case Hex2:
                case Hex4:
                case Hex8:
                if ((state.string_state.state == Octal ? get_octal : get_hexadecimal)(c, &digit))
                    state.string_state.value = state.string_state.value * (state.string_state.state == Octal ? 8 : 16) + digit;
                else if (state.string_state.state == Octal) state.string_state.num_digits = 2;
                else { jitc_error_set(context, jitc_error_syntax(file, row, col, "Not a valid digit")); return NULL; }
                state.string_state.num_digits++;
                if (state.string_state.num_digits == (
                    state.string_state.state == Octal ? 3 :
                    state.string_state.state == Hex2  ? 2 :
                    state.string_state.state == Hex4  ? 4 :
                    state.string_state.state == Hex8  ? 8 : 1
                )) {
                    encode_utf8(state.buffer, state.string_state.value);
                    state.string_state.state = None;
                }
                break;
            }
        }
        else if (state.parse_state == ParsingWord) {
            if (state.word_state.first_char && (is_numeric(c) || c == '.')) {
                state.word_state.first_char = false;
                state.word_state.dot_in_word = true;
                state.word_state.can_have_sign = true;
            }
            bool just_set = false;
            if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') && state.word_state.can_have_sign) {
                state.word_state.can_be_sign = true;
                state.word_state.can_have_sign = false;
                just_set = true;
            }
            if (is_alphanumeric(c) || (c == '.' && state.word_state.dot_in_word) || ((c == '+' || c == '-') && state.word_state.can_be_sign)) {
                if (c == '.') state.word_state.dot_in_word = false;
                if (state.word_state.can_be_sign && !just_set) state.word_state.can_be_sign = false;
                str_append(state.buffer, (char[]){ c, 0 });
            }
            else {
                jitc_token_t* token = mktoken(tokens, TOKEN_END_OF_FILE, file, state.row, state.col);
                if      (try_parse_int(state.buffer, &token->value.integer,  &token->flags)) token->type = TOKEN_INTEGER;
                else if (try_parse_flt(state.buffer, &token->value.floating, &token->flags)) token->type = TOKEN_FLOAT;
                else {
                    char last = str_data(state.buffer)[str_length(state.buffer) - 1];
                    if (is_numeric(str_data(state.buffer)[0]) || str_data(state.buffer)[0] == '.')
                        { jitc_error_set(context, jitc_error_syntax(file, row, col, "Invalid number")); return NULL; }
                    if (last == '+' || last == '-') str_data(state.buffer)[str_length(state.buffer) - 1] = 0;
                    else last = 0;
                    token->type = TOKEN_IDENTIFIER;
                    /*for (int i = 0; i < num_token_table_entries; i++) {
                        if (!token_table[i]) continue;
                        if (strcmp(token_table[i], str_data(state.buffer)) == 0) token->type = i;
                    }
                    if (token->type == TOKEN_IDENTIFIER) {*/
                        jitc_token_t* curr_token = token;
                        int ptr = 0;
                        char buf[str_length(state.buffer) + 1];
                        for (int i = 0; i < str_length(state.buffer); i++) {
                            if (str_data(state.buffer)[i] == '.') {
                                if (ptr != 0) {
                                    buf[ptr] = 0;
                                    if (!curr_token) curr_token = mktoken(tokens, TOKEN_IDENTIFIER, file, state.row, state.col + i - strlen(buf));
                                    curr_token->value.string = jitc_append_string(context, buf);
                                    curr_token = NULL;
                                }
                                if (!curr_token) curr_token = mktoken(tokens, (jitc_token_type_t[]){
                                    ['.'] = TOKEN_DOT,
                                    ['-'] = TOKEN_MINUS,
                                    ['+'] = TOKEN_PLUS,
                                }[str_data(state.buffer)[i]], file, state.row, state.col + i);
                                curr_token = NULL;
                                ptr = 0;
                                continue;
                            }
                            buf[ptr++] = str_data(state.buffer)[i];
                        }
                        if (ptr != 0) {
                            buf[ptr] = 0;
                            if (!curr_token) curr_token = mktoken(tokens, TOKEN_IDENTIFIER, file, state.row, state.col + str_length(state.buffer) - strlen(buf));
                            curr_token->value.string = jitc_append_string(context, buf);
                            curr_token = NULL;
                        }
                    //}
                    if (last) ptr--;
                }
                state.parse_state = Idle;
                no_increment = true;
            }
        }
        else if (state.parse_state == ParsingSymbol) {
            int starts_with = 0;
            int exact_match = -1;
            for (int i = 0; i < num_token_table_entries; i++) {
                if (!token_table[i]) continue;
                if (strncmp(str_data(state.buffer), token_table[i], str_length(state.buffer)) == 0) starts_with++;
                if (strcmp(str_data(state.buffer), token_table[i]) == 0) exact_match = i;
            }
            if (exact_match != -1 && (starts_with == 1 || !is_symbol(c))) {
                if (exact_match == TOKEN_COMMENT || exact_match == TOKEN_MULTILINE_COMMENT) {
                    state.parse_state = exact_match == TOKEN_COMMENT ? ParsingComment : ParsingMultilineComment;
                    continue;
                }
                mktoken(tokens, exact_match, file, state.row, state.col);
                state.parse_state = Idle;
                no_increment = true;
                continue;
            }
            if (is_symbol(c) && starts_with != 0) {
                str_append(state.buffer, (char[]){ c, 0 });
                starts_with = 0;
                for (int i = 0; i < num_token_table_entries; i++) {
                    if (!token_table[i]) continue;
                    if (strncmp(str_data(state.buffer), token_table[i], str_length(state.buffer)) == 0) starts_with++;
                }
                if (starts_with == 0 && exact_match != -1) {
                    if (exact_match == TOKEN_COMMENT || exact_match == TOKEN_MULTILINE_COMMENT) {
                        state.parse_state = exact_match == TOKEN_COMMENT ? ParsingComment : ParsingMultilineComment;
                        continue;
                    }
                    mktoken(tokens, exact_match, file, state.row, state.col);
                    state.parse_state = Idle;
                    no_increment = true;
                }
                continue;
            }
            if (exact_match == -1) { jitc_error_set(context, jitc_error_syntax(filename, row, col - str_length(state.buffer), "Invalid token")); return NULL; };
        }
    }
    mktoken(tokens, TOKEN_END_OF_FILE, file, state.row, state.col);
    return move(tokens);
}

jitc_token_t* jitc_token_expect(queue_t* _token_queue, jitc_token_type_t kind) {
    queue(jitc_token_t)* token_queue = _token_queue;
    jitc_token_t* token = &queue_peek(token_queue);
    if (token->type == kind) return &queue_pop(token_queue);
    return NULL;
}
