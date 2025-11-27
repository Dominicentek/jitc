#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "jitc_internal.h"
#include "lexer.h"

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
    Type_Varargs
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
    Binary_ArraySubscript,
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
    Binary_AssignXor
} jitc_binary_op_t;

typedef struct jitc_type_t jitc_type_t;
struct jitc_type_t {
    jitc_type_kind_t kind;
    bool is_const;
    bool is_unsigned;
    int alignment, size;
    const char* name;
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
            const char* name;
            list_t* fields;
        } str;
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
        } declaration;
        struct {
            const char* variable;
            list_t* param_names;
            jitc_ast_t* body;
        } function;
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

jitc_type_t* jitc_parse_type(jitc_context_t* context, queue_t* tokens, jitc_decltype_t* decltype);
jitc_ast_t* jitc_parse_ast(jitc_context_t* context, queue_t* token_queue);

void jitc_delete_type(jitc_type_t* type);

#endif
