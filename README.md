# jitc

**This project is NOT usable yet (check TODO)**

An embeddable C JIT compiler.

## Mission

Fill the hole where there's no lightweight, embeddable C JIT compiler that makes interop near plug and play.

## TODO

- Parameters/varargs/returns in function prolog and epilog
- Short circuiting
- Switch statements
- Initializers
- Assembling to machine code
- Virtual headers (#include)
- Windows support

Calling it done here, the things below are planned but not guaranteed

- Implement macro support
- Implement `goto`
- aarch64 support
- Rewrite IR into SSA
- Implement custom extensions
  - GNU statement expressions
  - GNU ternary syntax (`x ? : y`)
  - switch expressions
  - Zig's `defer` or GNU `__attribute__((cleanup))`, either one of the two
  - C++ lambdas
  - C++ templates (won't blow up to the complexity of C++)
  - C++ operator overloading (also won't blow up to C++'s complexity)
  
Note to self: When implementing extension features, still make programs as explicit as possible (minimal implicit behavior *cough cough C++ cough*)

## List of C features supported and compiler behavior

If a feature is not on this list, it's very likely not supported.

- All binary and unary operators (including comma operator)
  - Overflow wraps (modulo 2^n where n is the bit width of the type)
  - Usual type promotion rules (rule 3 overrides rule 2, rule 2 overrides rule 1, etc.)
    - Types smaller than 32 bits get promoted to `int`
    - If any type is 64-bit, everything else is 64-bit
    - If any type is unsigned, everything else is unsigned
      - Does not apply to comparsions
    - If any type is a `float`, everything else is a `float`
    - If any type is a `double`, everything else is a `double`
  - Floating point behavior (dividing by zero, operating on `NaN` or `Infinity`) depends on the architecture
- Ternary expressions (`?:`)
- Integer, float, character, bool and string literals
  - Multicharacter character constants
  - Escape sequences (`\a`, `\b`, `\e`, `\f`, `\n`, `\r`, `\t`, `\v`, `\"`, `\\`, `\'`, `\xNN`, `\uNNNN`, `\UNNNNNNNN`, `\NNN`)
  - String literals are concatted
  - `true` is the same as `(bool)1` and `false` is the same as `(bool)0`
  - `0x`, `0b` and `0` integer prefixes
  - Scientific notation for floats and hex floats
  - `L`, `LL`, `U`, `UL`, `ULL` and `F` suffixes for integer and float constants
    - Case insensitive
  - String literals are static, but read-write
- Comments
- Structs and unions
  - Can be anonymous and nest
  - Fields get aligned
- Enums
  - Can be anonymous
  - Underlying type is `int` by default, can be overriden via `: type` syntax
- Incomplete struct/union/enum types
- All basic types
  - `char` - Signed 8-bit integer
  - `short` - Signed 16-bit integer
  - `int` - Signed 32-bit integer
  - `long` - Signed 64-bit integer
  - `long long` - Signed 64-bit integer
  - `float` - 32-bit floating point
  - `double` - 64-bit floating point
  - `long double` - 64-bit floating point
  - `bool` - Unsigned 8-bit integer (`_Bool` unsupported)
  - `unsigned` and `const` qualifiers
    - `const` is enforced but can be casted away
  - `void`
  - Pointers, arrays and functions
    - Arrays and functions cannot be assigned to even if non-const
    - Decays into pointers
- Casting rules
  - Pointer <-> 64-bit integer
  - Pointer <-> Pointer
  - Pointer <-> Non 64-bit integer
    - Only if the cast is explicit
  - 0 integer literal -> Pointer
  - Integer <-> Integer
  - Struct/union <-> Struct/union
    - Must be the exact same struct type
- lvalues
  - Variables
  - Dereferences (and as a result, array subscripts)
  - Struct fields
  - Everything else is an rvalue
- Variadic arguments (varargs)
- Initializers
  - Designated initializers
  - Initialized to 0
- Integer constant expressions (`1 - 1` is the same as `0`, also applies to casting rules)
- `typedef`, `static` and `extern`
  - `static` variables are initialized to 0
- `sizeof`, `typeof` and `alignof` (`_Alignof` unsupported)
  - Must have parentheses, `sizeof x` is invalid
- Basic control flow (`if`, `else`, `while`, `do`, `for`, `break`, `continue`, `return`, `switch`, `case`, `default`)
  - `case` is integer constant only
  - Cases fall through without `break`
  - Function calls must have visible prototypes
    - Fixed argument count must match
    - Empty parameter list is the same as `void`
- `#include` macro directive
  - Recursive includes are ignored as there's no way to explicitly prevent it
  - Pasted during lexing as a series of tokens

Explicitly unsupported features

- `volatile`, `register`, `restrict`, `inline`, `auto`, `constexpr`, `signed` keywords
- `goto`
- VLAs (including `alloca`)
- Bitfields
- Variable shadowing
- Wide strings/characters
- All preprocessor directives besides `#include`
  - `#define`, `#undef`, `#ifdef`, `#ifndef`, `#else`, `#if`, `#elif`, `#elifdef`, `#elifndef`, `#endif`, `#error`, `#warning`, `#embed`, `#pragma`, `#line`
- Attributes
  - Both GNU `__attribute__` and C23 `[[attribute]]`
- All underscore-prefixed keywords
  - `_Alignas`, `_Alignof` (supported via `alignof`), `_Atomic`, `_Bool` (supported via `bool`), `_Complex`, `_DecimalX`, `_Generic`, `_Imaginary`, `_Noreturn`, `_Static_assert`, `_Thread_local`
- `asm` and `fortran`
- K&R-style function definitions and syntax

## API

- `struct jitc_error_t`
  - `const char* msg` - Error message
  - `const char* file` - Name of the file where the error occured
  - `int row` - Line number of the error location
  - `int col` - Column number of the error location
  - It is the caller's responsibility to `free` the object if it receives it from `jitc_parse` or `jitc_parse_file`

- `jitc_context_t* jitc_create_context()`
  - Creates a new context
- `bool jitc_include(jitc_context_t* context, const char* file)`
  - Includes a header into the context
  - Returns `true` on success, `false` if the header isn't found or if it was already included
- `jitc_error_t* jitc_parse(jitc_context_t* context, const char* code, const char* filename)`
  - Parses source code from memory, returns `NULL` if successful, an error object if not
  - `filename`: Can be `NULL`, specifies the filename for the lexer
- `jitc_error_t* jitc_parse_file(jitc_context* context, const char* file)`
  - Reads a file from the filesystem and parses its contents, returns `NULL` if successful, an error object if not
- `void* jitc_get(jitc_context* context, const char* name)`
  - Returns a symbol from the context, `NULL` if it doesn't exist
- `void jitc_destroy_context(jitc_context_t* context)`
  - Destroys a context and all the variables and functions declared with it
- `void jitc_create_header(const char* name, const char* content)`
  - Creates a virtual header

To use a symbol from the host, use `extern`. `extern` cannot be used to reference symbols from other contexts.
