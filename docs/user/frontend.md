# Frontend User Guide

This guide explains how to use and read the first NEX frontend tools. It is the
practical companion to the normative language spec in
`docs/language/core_v0.md` and the implementation contract in
`docs/design/frontend_contract.md`.

For a code-level explanation of the implementation, see
`docs/design/frontend_walkthrough.md`.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The compiler executable is:

```sh
build/nexc
```

## Dump Tokens

The lexer turns source text into a flat stream of tokens:

```sh
build/nexc --dump-tokens examples/minimal.nexs
```

Example output:

```text
KwFn [0, 2)
Identifier [3, 7) `main`
LeftParen [7, 8)
RightParen [8, 9)
Arrow [10, 12)
KwVoid [13, 17)
LeftBrace [18, 19)
KwReturn [24, 30)
Semicolon [30, 31)
RightBrace [32, 33)
EndOfFile [34, 34)
```

The span `[start, end)` is a byte range into the source file. The lexer stores
positions this way so diagnostics can point back to the exact source text.

## Dump AST

The parser turns the token stream into a syntax tree:

```sh
build/nexc --dump-ast examples/add.nexs
```

Example shape:

```text
TranslationUnit
  ConstDecl N: i32
    IntegerLiteral 4
  FunctionDecl add
    Parameters
      Parameter a: i32
      Parameter b: i32
    ReturnType i32
    BlockStmt
      ReturnStmt
        BinaryExpr Plus
          NameExpr a
          NameExpr b
```

The AST is still syntax, not full meaning. For example, the parser can build a
tree for `return true;` inside an `i32` function. A later semantic analysis pass
will reject that type mismatch.

## What Exists Now

The current frontend supports the Core v0 parser surface:

- module-level `const`
- function definitions
- scalar type syntax
- blocks, `let`, `let mut`, assignment, `return`, `if`, `else`, and `while`
- integer and boolean literals
- function calls
- unary operators `-` and `!`
- binary arithmetic, comparison, equality, `&&`, and `||`

The parser intentionally does not yet perform semantic checks such as undefined
names, immutable assignment errors, return type mismatches, or discarded
non-`void` call results.

## Mental Model

The frontend is split into stages:

```text
source text -> lexer -> tokens -> parser -> AST -> semantic analysis
```

Each stage should answer only one kind of question:

- Lexer: what are the words and symbols?
- Parser: do those words and symbols form valid syntax?
- Semantic analysis: does that syntax mean something valid?

Keeping these boundaries clear makes the compiler easier to explain, test, and
extend.
