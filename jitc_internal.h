#ifndef JITC_INTERNAL_H
#define JITC_INTERNAL_H

#include "jitc.h"
#include "dynamics.h"
#include "cleanups.h"

typedef struct jitc_token_t jitc_token_t;

typedef void*(*job_func_t)(void* ctx);

struct jitc_context_t {
    set_t* strings;
    map_t* typecache;
    list_t* scopes;
    jitc_error_t* error;
};

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
} jitc_type_kind_t;

typedef enum: uint8_t {
    AST_Unary,
    AST_Binary,
    AST_Ternary,
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
} jitc_ast_type_t;

typedef enum: uint8_t {
    Decltype_None,
    Decltype_Static,
    Decltype_Extern,
    Decltype_Typedef,
    Decltype_EnumItem,
} jitc_decltype_t;

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
    Binary_CompoundExpr,
    Binary_FunctionCall,
    Binary_PtrAddition,
    Binary_PtrSubtraction,
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
    Binary_AssignPtrAddition,
    Binary_AssignPtrSubtraction,
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

    Binary_Tern1,
    Binary_Tern2,
} jitc_binary_op_t;

typedef enum {
    TypePolicy_NoVoid = (1 << 0),
    TypePolicy_NoUnkArrSize = (1 << 1),
    TypePolicy_NoFunction = (1 << 2),
    TypePolicy_NoArray = (1 << 3),
    TypePolicy_NoUndefTags = (1 << 4),

    TypePolicy_NoDerived = TypePolicy_NoFunction | TypePolicy_NoArray,
    TypePolicy_NoIncomplete = TypePolicy_NoVoid | TypePolicy_NoUnkArrSize | TypePolicy_NoUndefTags,
} jitc_type_policy_t;

typedef struct {
    map_t* variables;
    map_t* structs;
    map_t* unions;
    map_t* enums;
} jitc_scope_t;

typedef struct jitc_type_t jitc_type_t;
struct jitc_type_t {
    jitc_type_kind_t kind;
    bool is_const;
    bool is_unsigned;
    const char* name;
    uint64_t alignment, size;
    uint64_t hash;
    union {
        struct {
            jitc_type_t* base;
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
        } str;
        struct {
            const char* name;
        } ref;
    };
};

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
            list_t* inner;
        } list;
        struct {
            jitc_type_t* type;
        } type;
        struct {
            jitc_decltype_t decltype;
            jitc_type_t* type;
            void* symbol_ptr;
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
            jitc_type_kind_t type_kind;
            bool is_unsigned;
            uint64_t value;
        } integer;
        struct {
            bool is_single_precision;
            double value;
        } floating;
        struct {
            const char* ptr;
        } string;
        struct {
            const char* name;
        } variable;
        struct {
            jitc_ast_t* struct_ptr;
            const char* field_name;
            size_t offset;
        } walk_struct;
    };
};

typedef struct {
    jitc_type_t* type;
    jitc_decltype_t decltype;
    bool defined;
    uint64_t value;
} jitc_variable_t;

#define OPCODES(X) \
    X(pushi, INT(TYPE(uint64_t) value), INT(TYPE(jitc_type_kind_t) kind), INT(TYPE(bool) is_unsigned)) \
    X(pushf, FLT(TYPE(double) value)) \
    X(pushd, FLT(TYPE(double) value)) \
    X(pop) \
    X(load, INT(TYPE(jitc_type_kind_t) kind), INT(TYPE(bool) is_unsigned)) \
    X(laddr, INT(TYPE(void*) ptr), INT(TYPE(jitc_type_kind_t) kind), INT(TYPE(bool) is_unsigned)) \
    X(lstack, INT(TYPE(int32_t) offset), INT(TYPE(jitc_type_kind_t) kind), INT(TYPE(bool) is_unsigned)) \
    X(store) \
    X(add) \
    X(sub) \
    X(mul) \
    X(div) \
    X(mod) \
    X(shl) \
    X(shr) \
    X(and) \
    X(or) \
    X(xor) \
    X(sadd) \
    X(ssub) \
    X(smul) \
    X(sdiv) \
    X(smod) \
    X(sshl) \
    X(sshr) \
    X(sand) \
    X(sor) \
    X(sxor) \
    X(not) \
    X(neg) \
    X(inc, INT(TYPE(bool) suffix), INT(TYPE(int32_t) step)) \
    X(zero) \
    X(addrof) \
    X(eql) \
    X(neq) \
    X(lst) \
    X(lte) \
    X(grt) \
    X(gte) \
    X(swp) \
    X(cvt, INT(TYPE(jitc_type_kind_t) kind), INT(TYPE(bool) is_unsigned)) \
    X(stackalloc, INT(TYPE(uint32_t) offset)) \
    X(offset, INT(TYPE(int32_t) offset)) \
    X(if) \
    X(then) \
    X(else) \
    X(end) \
    X(goto_start) \
    X(goto_end) \
    X(call, PTR(TYPE(jitc_type_t*) signature)) \
    X(ret) \
    X(func, PTR(TYPE(jitc_type_t*) signature), INT(TYPE(size_t) stack_size)) \
    X(func_end)

#define NOTHING(...)
#define EXPAND(...) __VA_ARGS__

#define ENUM(x, ...) IROpCode_##x,
typedef enum {
    OPCODES(ENUM)
} jitc_opcode_t;
#undef ENUM

typedef struct {
    jitc_opcode_t opcode;
    union {
        uint64_t as_integer;
        void* as_pointer;
        double as_float;
    } params[3];
} jitc_ir_t;

#undef NOTHING
#undef EXPAND

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
    KEYWORD(if) \
    KEYWORD(inline) \
    KEYWORD(int) \
    KEYWORD(long) \
    KEYWORD(nullptr) \
    KEYWORD(offsetof) \
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

jitc_type_t* jitc_typecache_primitive(jitc_context_t* context, jitc_type_kind_t kind);
jitc_type_t* jitc_typecache_unsigned(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_const(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_align(jitc_context_t* context, jitc_type_t* base, uint64_t new_align);
jitc_type_t* jitc_typecache_pointer(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_array(jitc_context_t* context, jitc_type_t* base, size_t size);
jitc_type_t* jitc_typecache_function(jitc_context_t* context, jitc_type_t* retval, list_t* params);
jitc_type_t* jitc_typecache_struct(jitc_context_t* context, list_t* fields);
jitc_type_t* jitc_typecache_union(jitc_context_t* context, list_t* fields);
jitc_type_t* jitc_typecache_enum(jitc_context_t* context, jitc_type_t* base);
jitc_type_t* jitc_typecache_structref(jitc_context_t* context, const char* name);
jitc_type_t* jitc_typecache_unionref(jitc_context_t* context, const char* name);
jitc_type_t* jitc_typecache_enumref(jitc_context_t* context, const char* name);
jitc_type_t* jitc_typecache_named(jitc_context_t* context, jitc_type_t* base, const char* name);
jitc_type_t* jitc_typecache_decay(jitc_context_t* context, jitc_type_t* from);
bool jitc_typecmp(jitc_context_t* context, jitc_type_t* a, jitc_type_t* b);

bool jitc_declare_variable(jitc_context_t* context, jitc_type_t* type, jitc_decltype_t decltype, uint64_t value);
bool jitc_declare_tagged_type(jitc_context_t* context, jitc_type_t* type, const char* name);
bool jitc_set_defined(jitc_context_t* context, const char* name);

jitc_variable_t* jitc_get_variable(jitc_context_t* context, const char* name);
jitc_type_t* jitc_get_tagged_type(jitc_context_t* context, jitc_type_kind_t kind, const char* name);
void* jitc_get_or_static(jitc_context_t* context, const char* name);

bool jitc_walk_struct(jitc_type_t* str, const char* name, jitc_type_t** field_type, size_t* offset);
bool jitc_struct_field_exists_list(list_t* list, const char* name);

void jitc_push_scope(jitc_context_t* context);
bool jitc_pop_scope(jitc_context_t* context);

jitc_error_t* jitc_error_syntax(const char* filename, int row, int col, const char* str, ...);
jitc_error_t* jitc_error_parser(jitc_token_t* token, const char* str, ...);
void jitc_error_set(jitc_context_t* context, jitc_error_t* error);

void jitc_report_error(jitc_error_t* error, FILE* file);
void jitc_free_error(jitc_error_t* error);

bool jitc_validate_type(jitc_type_t* type, jitc_type_policy_t policy);

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype);
jitc_ast_t* jitc_parse_expression(jitc_context_t* context, queue_t* tokens, jitc_type_t** exprtype);
jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* token_queue);
void jitc_compile(jitc_context_t* context, jitc_ast_t* ast);

void jitc_destroy_ast(jitc_ast_t* ast);

#endif
