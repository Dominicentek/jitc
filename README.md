# jitc

**This project is usable, but incomplete and possibly full of bugs. Use at your own risk.**

An embeddable JIT compiler for C' with hot reload support

## Mission

Fill the hole where there's no lightweight, embeddable JIT compiler that
compiles code that is nearly source compatible with C and
makes interop near plug and play.

## TODO

- Rewrite IR
- Switch statements
- Switch expressions
- Windows support (x64 ABI)
- User friendly segfault reporting and handling
- GNU statement expressions
- GNU ternary syntax (`x ?: y`)
- Suffix pointer syntax (`void()*` instead of `void(*)()`)
- Zig's `defer` and GNU `__attribute__((cleanup))`
- Vectors, matrices, quaternions (also support swizzles)
- Runtime type reflection
- Closures
- Documentation

## About C'

C' (pronounced "C prime") is a language that is *derived* from C (get it)
that preserves the language's minimalism and philosophy.

Syntactically and semantically, it's nearly identical to C, but it adds extra features.

### Features

- Redefinition of functions and global variables
- `hotswap` and `preserve`
- `defer` and `cleanup`*
- Single-expression functions
- Anonymous functions (lambdas)
- Closures*
- Templates (type substitution)
- Methods
- Vectors, matrices, quaternions and swizzles*
- Runtime type reflection*
- Switch expressions*
- Suffix pointer syntax*
- `extern("name")`
- `#depends`
- `__ID__` macro

\*Not implemented yet
