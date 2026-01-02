# jitc

**This project is NOT usable yet (check TODO)**

An embeddable C-like JIT compiler.

> [!WARNING]
> This compiler is **NOT** suited for real-world C code.
> It's intentionally incomplete and implements only a small subset of C23 (see below).
> It's meant to compile small files written in C to provide a scripting interface for host programs like game engines.

## Mission

Fill the hole where there's no lightweight, embeddable C JIT compiler that makes interop near plug and play.

## TODO

- Parameters/varargs/returns in function prolog and epilog
- Switch statements
- Initializers
- Compound expressions
- Preprocessor
- Windows support

Calling it done here, the things below are planned but not guaranteed

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
  - Compound expressions
  - Everything else is an rvalue
- Variadic arguments (varargs)
- Initializers
  - Designated initializers
  - Initialized to 0
  - Compound expressions
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
- Preprocessor directives
  - `#define`, `#undef`, `#include`, `#embed`, `#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef`, `#elifdef`, `#elifndef`, `#error`
    - `defined` operator is supported, but `__has_include` is not
    - `embed` has no parameters
  - `#error`, `#warning`, `#pragma` and `#line` not supported
  - `__STDC_HOSTED__` - `1`
  - `__STDC_NO_ATOMICS__`
  - `__STDC_NO_COMPLEX__`
  - `__STDC_NO_THREADS__`
  - `__STDC_NO_VLA__`
  - `__JITC__`
  - `__x86_64__` - Defined only on x86_64 architectures
  - `__aarch64__` - Defined only on aarch64 architectures
  - `_WIN32` - Defined only on Windows
  - `__APPLE__` - Defined only on macOS
  - `__linux__` - Defined only on Linux
  - `__unix__` - Defined only on macOS and Linux
  - `__LINE__` - Current line number
  - `__FILE__` - Current file (string literal)
  - `__DATE__` - Compilation date
  - `__TIME__` - Compilation time
  - `__VA_ARGS__` - Set of varargs
  - `__VA_OPT__` - Expands to parameter if there are any varargs
  - `__DEFINE__`, `__UNDEF__`, `__RECURSE__`, `__IF__`, `__EVAL__` - Check [preprocessor extensions](#preprocessor-extensions)
  - Includes and defines pasted during lexing as a series of tokens

Explicitly unsupported features

- `volatile`, `register`, `restrict`, `inline`, `auto`, `constexpr`, `signed` keywords
- `goto`
- VLAs (including `alloca`)
- Bitfields
- Variable shadowing
- Wide strings/characters
- `#warning`, `#pragma` and `#line`
- Attributes
  - Both GNU `__attribute__` and C23 `[[attribute]]`
- All underscore-prefixed keywords
  - `_Alignas`, `_Alignof` (supported via `alignof`), `_Atomic`, `_Bool` (supported via `bool`), `_Complex`, `_DecimalX`, `_Generic`, `_Imaginary`, `_Noreturn`, `_Static_assert`, `_Thread_local`
- `asm` and `fortran`
- K&R-style function definitions and syntax

### Preprocessor Extensions

jitc's preprocessor aims to be a turing complete extension of the C preprocessor. It achieves this by adding some special macros.

#### `__DEFINE__(name, ...)`

Expands to nothing.

Defines a new macro `name` that expands to `__VA_ARGS__`.

#### `__UNDEF__(macro)`

Expands to nothing.

Undefines a macro.

#### `__RECURSE__(...)`

Forces an expansion of `__VA_ARGS__` bypassing the recursion check. Has a hard limit of 1024 expansions per run.

#### `__IF__(cond, ...)`

Expands to `__VA_ARGS__` if `cond` doesn't evaluate to `0` and isn't empty. `cond` has the semantics as with the `#if` directive.

#### `__EVAL__(expr)`

Evaluates the expression `expr`. Same semantics as with the `#if` directive.

## API

- `struct jitc_error_t`
  - `const char* msg` - Error message
  - `const char* file` - Name of the file where the error occured
  - `int row` - Line number of the error location
  - `int col` - Column number of the error location
  - It is the caller's responsibility to `free` the object if it receives it from `jitc_parse` or `jitc_parse_file`

- `jitc_context_t* jitc_create_context()`
  - Creates a new context
- `void jitc_create_header(jitc_context_t* context, const char* name, const char* content)`
  - Creates a virtual header
- `jitc_error_t* jitc_parse(jitc_context_t* context, const char* code, const char* filename)`
  - Parses source code from memory, returns `NULL` if successful, an error object if not
  - `filename`: Can be `NULL`, specifies the filename for the lexer
- `jitc_error_t* jitc_parse_file(jitc_context* context, const char* file)`
  - Reads a file from the filesystem and parses its contents, returns `NULL` if successful, an error object if not
- `void* jitc_get(jitc_context* context, const char* name)`
  - Returns a symbol from the context, `NULL` if it doesn't exist
- `void jitc_destroy_context(jitc_context_t* context)`
  - Destroys a context and all the variables and functions declared with it

To use a symbol from the host, use `extern`. `extern` cannot be used to reference symbols from other contexts.
