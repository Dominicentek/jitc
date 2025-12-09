# jitc

**This project is NOT usable yet**

An embeddable C JIT compiler.

## Mission

Fill the hole where there's no lightweight, embeddable C JIT compiler that makes interop near plug and play.

## Differences from real C

- `volatile`, `register`, `restrict` and `inline` do nothing
- `long double` is the same as `double`
- No VLAs
- No Variable shadowing
- No `goto` (for now)
- No `alloca`, `auto`, `constexpr` or `signed`
- `sizeof` and `alignof` **must** have parentheses
- No underscore-prefixed keywords:
  - `_Alignas` -> `alignas`
  - `_Alignof` -> `alignof`
  - `_Bool` -> `bool`
  - Every other underscore-prefixed keyword is unsupported

## API

- `struct jitc_error_t`
  - `const char* msg` - Error message
  - `const char* file` - Name of the file where the error occured
  - `int row` - Line number of the error location
  - `int col` - Column number of the error location
  - It is the caller's responsibility to `free` the object if it receives it from `jitc_parse` or `jitc_parse_file`

- `jitc_context_t* jitc_create_context()`
  - Creates a new context
- `jitc_error_t* jitc_parse(jitc_context_t* context, const char* code, const char* filename)`
  - Parses source code from memory, returns `NULL` if successful, an error object if not
  - `filename`: Can be `NULL`, specifies the filename for the lexer
- `jitc_error_t* jitc_parse_file(jitc_context* context, const char* file)`
  - Reads a file from the filesystem and parses its contents, returns `NULL` if successful, an error object if not
- `void* jitc_get(jitc_context* context, const char* name)`
  - Returns a symbol from the context, `NULL` if it doesn't exist
- `void jitc_destroy_context(jitc_context_t* context)`
  - Destroys a context and all the variables and functions declared with it

To use a symbol from the host or some other symbol from the current context in a script, use `extern`. `extern` cannot be used to reference symbols from other contexts.
