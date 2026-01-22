#ifndef JITC_INTERNAL_H
#define JITC_INTERNAL_H

#include "jitc.h"
#include "dynamics.h"
#include "cleanups.h"

typedef struct jitc_token_t jitc_token_t;

typedef enum: uint8_t {
    Decltype_None,
    Decltype_Static,
    Decltype_Extern,
    Decltype_Typedef,
    Decltype_EnumItem,
    Decltype_Template = (1 << 3)
} jitc_decltype_t;

typedef enum: uint8_t {
    Preserve_IfConst,
    Preserve_Always,
    Preserve_Never
} jitc_preserve_t;

typedef enum: uint8_t {
    Type_Int8,
    Type_Int16,
    Type_Int32,
    Type_Int64,
    Type_Float32,
    Type_Float64,
    Type_Pointer,
    Type_Array,
    Type_Function,
    Type_Struct,
    Type_Union,
    Type_Enum,
    Type_StructRef,
    Type_UnionRef,
    Type_EnumRef,
    Type_Void,
    Type_Varargs,
    Type_Template,
    Type_Placeholder,
} jitc_type_kind_t;

typedef enum: uint8_t {
    AST_Unary,
    AST_Binary,
    AST_Ternary,
    AST_Branch,
    AST_List,
    AST_Type,
    AST_Declaration,
    AST_Function,
    AST_Loop,
    AST_Scope,
    AST_Break,
    AST_Continue,
    AST_Return,
    AST_Integer,
    AST_Floating,
    AST_StringLit,
    AST_Variable,
    AST_WalkStruct,
    AST_Initializer,
    AST_Goto,
    AST_Label,
} jitc_ast_type_t;

typedef enum: uint8_t {
    Unary_PrefixIncrement,
    Unary_PrefixDecrement,
    Unary_SuffixIncrement,
    Unary_SuffixDecrement,
    Unary_PtrPrefixIncrement,
    Unary_PtrPrefixDecrement,
    Unary_PtrSuffixIncrement,
    Unary_PtrSuffixDecrement,
    Unary_ArithPlus,
    Unary_ArithNegate,
    Unary_LogicNegate,
    Unary_BinaryNegate,
    Unary_AddressOf,
    Unary_Dereference,
} jitc_unary_op_t;

typedef enum: uint8_t {
    Binary_Cast,
    Binary_FunctionCall,
    Binary_PtrAddition,
    Binary_PtrSubtraction,
    Binary_PtrDiff,
    Binary_Addition,
    Binary_Subtraction,
    Binary_Multiplication,
    Binary_Division,
    Binary_Modulo,
    Binary_BitshiftLeft,
    Binary_BitshiftRight,
    Binary_LessThan,
    Binary_GreaterThan,
    Binary_LessThanOrEqualTo,
    Binary_GreaterThanOrEqualTo,
    Binary_Equals,
    Binary_NotEquals,
    Binary_And,
    Binary_Or,
    Binary_Xor,
    Binary_LogicAnd,
    Binary_LogicOr,
    Binary_Assignment,
    Binary_AssignConst,
    Binary_AssignPtrAddition,
    Binary_AssignPtrSubtraction,
    Binary_AssignPtrDiff,
    Binary_AssignAddition,
    Binary_AssignSubtraction,
    Binary_AssignMultiplication,
    Binary_AssignDivision,
    Binary_AssignModulo,
    Binary_AssignBitshiftLeft,
    Binary_AssignBitshiftRight,
    Binary_AssignAnd,
    Binary_AssignOr,
    Binary_AssignXor,
    Binary_Comma,
} jitc_binary_op_t;

typedef enum {
    TypePolicy_NoVoid = (1 << 0),
    TypePolicy_NoUnkArrSize = (1 << 1),
    TypePolicy_NoFunction = (1 << 2),
    TypePolicy_NoArray = (1 << 3),
    TypePolicy_NoUndefTags = (1 << 4),
    TypePolicy_NoTemplates = (1 << 5),

    TypePolicy_NoDerived = TypePolicy_NoFunction | TypePolicy_NoArray,
    TypePolicy_NoIncomplete = TypePolicy_NoVoid | TypePolicy_NoUnkArrSize | TypePolicy_NoUndefTags | TypePolicy_NoTemplates,
} jitc_type_policy_t;

typedef struct jitc_type_t jitc_type_t;
struct jitc_type_t {
    jitc_type_kind_t kind;
    bool is_const;
    bool is_unsigned;
    const char* name;
    uint32_t alignment, size;
    uint64_t hash;
    union {
        struct {
            jitc_type_t* base;
            size_t arr_size;
            jitc_type_kind_t prev;
        } ptr;
        struct {
            jitc_type_t* base;
            size_t size;
        } arr;
        struct {
            jitc_type_t* ret;
            jitc_type_t** params;
            size_t num_params;
        } func;
        struct {
            jitc_type_t** fields;
            size_t* offsets;
            size_t num_fields;
            jitc_token_t* source_token;
        } str;
        struct {
            const char* name;
            size_t templ_num_types;
            jitc_type_t** templ_types;
        } ref;
        struct {
            const char* name;
        } placeholder;
        struct {
            jitc_type_t* base;
            const char** names;
            size_t num_names;
        } templ;
    };
};

typedef struct {
    void* curr_ptr;
    void* ptr;
    size_t size;
} jitc_func_cell_t;

typedef struct __attribute__((packed)) {
    char mov_rax[2];
    jitc_func_cell_t* addr;
    char jmp_rax[2];
} jitc_func_trampoline_t;

typedef struct {
    jitc_type_t* type;
    jitc_decltype_t decltype;
    jitc_preserve_t preserve_policy;
    bool initial;
    union {
        void* ptr;
        uint64_t enum_value;
        jitc_func_trampoline_t* func;
    };
} jitc_variable_t;

typedef struct {
    jitc_decltype_t decltype;
    jitc_preserve_t preserve_policy;
    list_t* tokens;
    jitc_type_t* target_type;
    map_t* template_map;
} jitc_instantiation_request_t;

typedef struct jitc_ast_t jitc_ast_t;
struct jitc_ast_t {
    jitc_ast_type_t node_type;
    jitc_token_t* token;
    jitc_type_t* exprtype;
    size_t su_number;
    union {
        struct {
            jitc_unary_op_t operation;
            jitc_ast_t* inner;
        } unary;
        struct {
            jitc_binary_op_t operation;
            jitc_ast_t* left;
            jitc_ast_t* right;
        } binary;
        struct {
            jitc_ast_t* when;
            jitc_ast_t* then;
            jitc_ast_t* otherwise;
        } ternary;
        struct {
            list(jitc_ast_t*)* inner;
        } list;
        struct {
            jitc_type_t* type;
        } type;
        struct {
            jitc_decltype_t decltype;
            jitc_type_t* type;
            jitc_variable_t* variable;
        } decl;
        struct {
            jitc_type_t* variable;
            jitc_ast_t* body;
        } func;
        struct {
            jitc_ast_t* cond;
            jitc_ast_t* body;
        } loop;
        struct {
            jitc_ast_t* expr;
        } ret;
        struct {
            uint64_t value;
            jitc_type_kind_t type_kind;
            bool is_unsigned;
        } integer;
        struct {
            double value;
            bool is_single_precision;
        } floating;
        struct {
            const char* ptr;
        } string;
        struct {
            const char* name;
        } label;
        struct {
            const char* name;
            jitc_ast_t* this_ptr;
            bool write_dest;
            map_t* templ_map;
        } variable;
        struct {
            jitc_ast_t* struct_ptr;
            const char* field_name;
            size_t offset;
        } walk_struct;
        struct {
            jitc_type_t* type;
            jitc_ast_t* store_to;
            list(size_t)* offsets;
            list(jitc_ast_t*)* items;
        } init;
    };
};

typedef struct {
    void* ptr;
    size_t capacity;
    size_t avail;
} jitc_memchunk_t;

typedef struct {
    map(char*, jitc_variable_t)* variables;
    map(char*, jitc_type_t*)* structs;
    map(char*, jitc_type_t*)* unions;
    map(char*, jitc_type_t*)* enums;
    map(char*, int)* struct_template_params;
    map(char*, int)* union_template_params;
} jitc_scope_t;

struct jitc_context_t {
    set(char*)* strings;
    map(uint64_t, jitc_type_t*)* typecache;
    map(char*, char*)* headers;
    list(char*)* labels;
    list(jitc_scope_t)* scopes;
    list(jitc_memchunk_t)* memchunks;
    queue(jitc_instantiation_request_t)* instantiation_requests;
    jitc_error_t* error;
    bool all_linked;
};

#define PROCESS_TOKENS(type) TOKENS(type##_KEYWORD, type##_SYMBOL, type##_SPECIAL)

#define ENUM_KEYWORD(x) TOKEN_##x,
#define ENUM_SPECIAL(x) TOKEN_##x,
#define ENUM_SYMBOL(x, y) TOKEN_##y,
#define DECL_KEYWORD(x) #x,
#define DECL_SPECIAL(x) NULL,
#define DECL_SYMBOL(x, y) x,

#define TOKENS(KEYWORD, SYMBOL, SPECIAL) \
    SPECIAL(END_OF_FILE) \
    SPECIAL(IDENTIFIER) \
    SPECIAL(STRING) \
    SPECIAL(INTEGER) \
    SPECIAL(FLOAT) \
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
    KEYWORD(hotswap) \
    KEYWORD(if) \
    KEYWORD(inline) \
    KEYWORD(int) \
    KEYWORD(long) \
    KEYWORD(nullptr) \
    KEYWORD(offsetof) \
    KEYWORD(preserve) \
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
    SYMBOL("(", PARENTHESIS_OPEN) \
    SYMBOL(")", PARENTHESIS_CLOSE) \
    SYMBOL("[", BRACKET_OPEN) \
    SYMBOL("]", BRACKET_CLOSE) \
    SYMBOL("{", BRACE_OPEN) \
    SYMBOL("}", BRACE_CLOSE) \
    SYMBOL("->", ARROW) \
    SYMBOL("=>", EQUALS_ARROW) \
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
    SYMBOL("#", HASHTAG) \
    SYMBOL("##", DOUBLE_HASHTAG) \
    SYMBOL("\\", BACKSLASH) \
    SYMBOL("//", COMMENT) \
    SYMBOL("/""*", MULTILINE_COMMENT) \
    SPECIAL(COUNT) \

typedef enum: uint8_t {
    PROCESS_TOKENS(ENUM)
} jitc_token_type_t;

static const char* token_table[] = {
    PROCESS_TOKENS(DECL)
};
static int num_token_table_entries = sizeof(token_table) / sizeof(*token_table);

typedef union {
    struct {
        bool is_unsigned : 1;
        unsigned int type_kind : 2;
        // there are 4 integer types: 8-bit, 16-bit, 32-bit, 64-bit; fits within 2 bits
    } int_flags;
    struct {
        bool is_single_precision : 1;
    } float_flags;
} jitc_token_flags_t;

struct jitc_token_t {
    char* filename;
    jitc_token_type_t type;
    jitc_token_flags_t flags;
    int row, col;
    union {
        char* string;
        uint64_t integer;
        double floating;
    } value;
};

jitc_token_t* jitc_token_expect(queue_t* token_queue, jitc_token_type_t kind);
queue_t* jitc_lex(jitc_context_t* context, const char* code, const char* filename);
queue_t* jitc_preprocess(jitc_context_t* context, queue_t* tokens, map_t* macros);

jitc_type_t* jitc_typecache_primitive(jitc_context_t* context, jitc_type_kind_t kind);
jitc_type_t* jitc_typecache_unsigned(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_const(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_align(jitc_context_t* context, jitc_type_t* base, uint64_t new_align);
jitc_type_t* jitc_typecache_pointer(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_array(jitc_context_t* context, jitc_type_t* base, size_t size);
jitc_type_t* jitc_typecache_function(jitc_context_t* context, jitc_type_t* retval, list_t* params);
jitc_type_t* jitc_typecache_struct(jitc_context_t* context, list_t* fields, jitc_token_t* source);
jitc_type_t* jitc_typecache_union(jitc_context_t* context, list_t* fields, jitc_token_t* source);
jitc_type_t* jitc_typecache_enum(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_structref(jitc_context_t* context, const char* name, list_t* template_list);
jitc_type_t* jitc_typecache_unionref(jitc_context_t* context, const char* name, list_t* template_list);
jitc_type_t* jitc_typecache_enumref(jitc_context_t* context, const char* name);
jitc_type_t* jitc_typecache_placeholder(jitc_context_t* context, const char* name);
jitc_type_t* jitc_typecache_template(jitc_context_t* context, jitc_type_t* base, list_t* names);
jitc_type_t* jitc_typecache_fill_template(jitc_context_t* context, jitc_type_t* base, map_t* mappings);
jitc_type_t* jitc_typecache_named(jitc_context_t* context, jitc_type_t* base, const char* name);
jitc_type_t* jitc_typecache_decay(jitc_context_t* context, jitc_type_t* from);
bool jitc_typecmp(jitc_context_t* context, jitc_type_t* a, jitc_type_t* b);

jitc_type_t* jitc_to_method(jitc_context_t* context, jitc_type_t* type);
bool jitc_declare_variable(jitc_context_t* context, jitc_type_t* type, jitc_decltype_t decltype, jitc_preserve_t preserve_policy, uint64_t value);
bool jitc_declare_tagged_type(jitc_context_t* context, jitc_type_t* type, const char* name);
bool jitc_template_params_check(jitc_context_t* context, jitc_type_t* type, const char* name);

jitc_variable_t* jitc_get_variable(jitc_context_t* context, const char* name);
jitc_type_t* jitc_mangle_template(jitc_context_t* context, jitc_type_t* type);
jitc_type_t* jitc_get_tagged_type_notype(jitc_context_t* context, jitc_type_kind_t kind, const char* name);
jitc_type_t* jitc_get_tagged_type(jitc_context_t* context, jitc_type_t* type);
jitc_variable_t* jitc_get_or_static(jitc_context_t* context, const char* name);

jitc_variable_t* jitc_get_method(jitc_context_t* context, jitc_type_t* base, const char* name);
bool jitc_walk_struct(jitc_type_t* str, const char* name, jitc_type_t** field_type, size_t* offset);
bool jitc_struct_field_exists_list(list_t* list, const char* name);

void jitc_push_scope(jitc_context_t* context);
bool jitc_pop_scope(jitc_context_t* context);

jitc_error_t* jitc_error_syntax(const char* filename, int row, int col, const char* str, ...);
jitc_error_t* jitc_error_parser(jitc_token_t* token, const char* str, ...);
void jitc_error_set(jitc_context_t* context, jitc_error_t* error);

bool jitc_validate_type(jitc_type_t* type, jitc_type_policy_t policy);

char* jitc_append_string(jitc_context_t* context, const char* string);
queue_t* jitc_include(jitc_context_t* context, jitc_token_t* token, const char* filename, map_t* macros);

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype, jitc_preserve_t* preserve_policy);
jitc_ast_t* jitc_parse_expression(jitc_context_t* context, queue_t* tokens, int min_prec, jitc_type_t** exprtype);
jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* token_queue);
void* jitc_compile_func(jitc_context_t* context, jitc_ast_t* ast, int* size);
void jitc_compile(jitc_context_t* context, jitc_ast_t* ast);
void jitc_link(jitc_context_t* context);

void jitc_destroy_ast(jitc_ast_t* ast);
void jitc_delete_memchunks(jitc_context_t* context);

#endif
