#ifndef JITC_INTERNAL_H
#define JITC_INTERNAL_H

#include "jitc.h"
#include "dynamics.h"

typedef struct jitc_token_t jitc_token_t;

struct jitc_error_t {
    const char* msg;
    const char* file;
    int row, col;
} ;

struct jitc_context_t {
    set_t* strings;
    map_t* symbols;
    map_t* typecache;
    jitc_error_t* error;
};

typedef enum: uint8_t {
    Type_Void,
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
    Decltype_Typedef
} jitc_decltype_t;

typedef enum: uint8_t {
    Unary_SuffixIncrement,
    Unary_SuffixDecrement,
    Unary_PrefixIncrement,
    Unary_PrefixDecrement,
    Unary_ArithPlus,
    Unary_ArithNegate,
    Unary_LogicNegate,
    Unary_BinaryNegate,
    Unary_AddressOf,
    Unary_Dereference,
    Unary_Sizeof,
    Unary_Alignof,
} jitc_unary_op_t;

typedef enum: uint8_t {
    Binary_Cast,
    Binary_CompoundExpr,
    Binary_FunctionCall,
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
        } ptr;
        struct {
            jitc_type_t* base;
            size_t size;
        } arr;
        struct {
            jitc_type_t* ret;
            list_t* params;
        } func;
        struct {
            list_t* fields;
        } str;
        struct {
            const char* name;
        } ref;
    };
};

typedef struct jitc_ast_t jitc_ast_t;
struct jitc_ast_t {
    jitc_ast_type_t node_type;
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
        } walk_struct;
    };
};

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

bool jitc_declare_variable(jitc_context_t* context, jitc_type_t* type, jitc_decltype_t decltype);
bool jitc_declare_tagged_type(jitc_context_t* context, jitc_type_t* type);
bool jitc_declare_enum_item(jitc_context_t* context, jitc_type_t* type, const char* name, uint64_t value);

void jitc_push_scope(jitc_context_t* context);
void jitc_push_function_scope(jitc_context_t* context);
void jitc_pop_scope(jitc_context_t* context);

jitc_error_t* jitc_error_syntax(const char* filename, int row, int col, const char* str, ...);
jitc_error_t* jitc_error_parser(jitc_token_t* token, const char* str, ...);
void jitc_error_set(jitc_context_t* context, jitc_error_t* error);

void jitc_report_error(jitc_error_t* error, FILE* file);
void jitc_free_error(jitc_error_t* error);

bool jitc_validate_type(jitc_type_t* type, jitc_type_policy_t policy);

#endif
