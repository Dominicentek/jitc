#ifndef JITC_LEXER_H
#define JITC_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jitc_internal.h"

#define PROCESS_TOKENS(type) TOKENS(type##_KEYWORD, type##_SYMBOL, type##_SPECIAL)

#define ENUM_KEYWORD(x) TOKEN_##x,
#define ENUM_SPECIAL(x) TOKEN_##x,
#define ENUM_SYMBOL(x, y) TOKEN_##y,
#define DECL_KEYWORD(x) #x,
#define DECL_SPECIAL(x) NULL,
#define DECL_SYMBOL(x, y) x,

#define TOKENS(KEYWORD, SYMBOL, SPECIAL) \
    SPECIAL(END_OF_FILE) \
    KEYWORD(alignas) \
    KEYWORD(alignof) \
    KEYWORD(auto) \
    KEYWORD(bool) \
    KEYWORD(break) \
    KEYWORD(case) \
    KEYWORD(char) \
    KEYWORD(const) \
    KEYWORD(continue) \
    KEYWORD(default) \
    KEYWORD(do) \
    KEYWORD(double) \
    KEYWORD(else) \
    KEYWORD(enum) \
    KEYWORD(extern) \
    KEYWORD(false) \
    KEYWORD(float) \
    KEYWORD(for) \
    KEYWORD(goto) \
    KEYWORD(if) \
    KEYWORD(inline) \
    KEYWORD(int) \
    KEYWORD(long) \
    KEYWORD(nullptr) \
    KEYWORD(register) \
    KEYWORD(restrict) \
    KEYWORD(return) \
    KEYWORD(short) \
    KEYWORD(sizeof) \
    KEYWORD(static) \
    KEYWORD(struct) \
    KEYWORD(switch) \
    KEYWORD(true) \
    KEYWORD(typedef) \
    KEYWORD(typeof) \
    KEYWORD(union) \
    KEYWORD(unsigned) \
    KEYWORD(void) \
    KEYWORD(volatile) \
    KEYWORD(while) \
    SPECIAL(IDENTIFIER) \
    SPECIAL(STRING) \
    SPECIAL(INTEGER) \
    SPECIAL(FLOAT) \
    SYMBOL("(", PARENTHESIS_OPEN) \
    SYMBOL(")", PARENTHESIS_CLOSE) \
    SYMBOL("[", BRACKET_OPEN) \
    SYMBOL("]", BRACKET_CLOSE) \
    SYMBOL("{", BRACE_OPEN) \
    SYMBOL("}", BRACE_CLOSE) \
    SYMBOL("->", ARROW) \
    SYMBOL(",", COMMA) \
    SYMBOL(":", COLON) \
    SYMBOL(";", SEMICOLON) \
    SYMBOL(".", DOT) \
    SYMBOL("+", PLUS) \
    SYMBOL("-", MINUS) \
    SYMBOL("/", SLASH) \
    SYMBOL("%", PERCENT) \
    SYMBOL("*", ASTERISK) \
    SYMBOL("^", HAT) \
    SYMBOL("&", AMPERSAND) \
    SYMBOL("|", PIPE) \
    SYMBOL("?", QUESTION_MARK) \
    SYMBOL("++", DOUBLE_PLUS) \
    SYMBOL("--", DOUBLE_MINUS) \
    SYMBOL("&&", DOUBLE_AMPERSAND) \
    SYMBOL("||", DOUBLE_PIPE) \
    SYMBOL("==", DOUBLE_EQUALS) \
    SYMBOL("!=", NOT_EQUALS) \
    SYMBOL("<", LESS_THAN) \
    SYMBOL(">", GREATER_THAN) \
    SYMBOL("<=", LESS_THAN_EQUALS) \
    SYMBOL(">=", GREATER_THAN_EQUALS) \
    SYMBOL("<<", DOUBLE_LESS_THAN) \
    SYMBOL(">>", DOUBLE_GREATER_THAN) \
    SYMBOL("=", EQUALS) \
    SYMBOL("+=", PLUS_EQUALS) \
    SYMBOL("-=", MINUS_EQUALS) \
    SYMBOL("*=", ASTERISK_EQUALS) \
    SYMBOL("/=", SLASH_EQUALS) \
    SYMBOL("%=", PERCENT_EQUALS) \
    SYMBOL("&=", AMPERSAND_EQUALS) \
    SYMBOL("|=", PIPE_EQUALS) \
    SYMBOL("^=", HAT_EQUALS) \
    SYMBOL("<<=", DOUBLE_LESS_THAN_EQUALS) \
    SYMBOL(">>=", DOUBLE_GREATER_THAN_EQUALS) \
    SYMBOL("~", TILDE) \
    SYMBOL("!", EXCLAMATION_MARK) \
    SYMBOL("...", TRIPLE_DOT) \

typedef enum: uint8_t {
    PROCESS_TOKENS(ENUM)
} jitc_token_type_t;

static const char* token_table[] = {
    PROCESS_TOKENS(DECL)
};
static int num_token_table_entries = sizeof(token_table) / sizeof(*token_table);

struct jitc_token_t {
    jitc_token_type_t type;
    char* filename;
    int row, col, flags;
    union {
        char* string;
        uint64_t integer;
        double floating;
    } value;
};

jitc_token_t* jitc_token_expect(queue_t* token_queue, jitc_token_type_t kind);
queue_t* jitc_lex(jitc_context_t* context, const char* code, const char* filename);

#endif
