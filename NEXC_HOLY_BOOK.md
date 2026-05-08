# NEXC Holy Book

This guide explains the compiler as it grows. It is intentionally educational:
when we add a compiler concept, this file should explain **what it is**, **why it
exists**, and **where it lives in the codebase**.

The language rules live in [docs/language/core_v0.md](docs/language/core_v0.md).
This guide is about how the compiler implementation works.

## Table of Contents

- [Developer Workflow](#developer-workflow)
- [1. Current Pipeline](#1-current-pipeline)
- [2. Build Shape](#2-build-shape)
- [3. Source Files and Spans](#3-source-files-and-spans)
- [4. Diagnostics and Carets](#4-diagnostics-and-carets)
- [5. Tokens](#5-tokens)
- [6. Lexer](#6-lexer)
- [7. AST](#7-ast)
- [8. Parser](#8-parser)
- [9. Semantic Analysis](#9-semantic-analysis)
- [10. CLI Inspection Modes](#10-cli-inspection-modes)
- [11. Tests, Golden Files, and CI](#11-tests-golden-files-and-ci)
- [12. Current Limitations](#12-current-limitations)
- [13. Recommended Next Steps](#13-recommended-next-steps)

## Developer Workflow

The easiest way to build and test the project is the root helper script:

```sh
./nexc.sh check
```

That runs the normal development loop:

```text
configure CMake -> build nexc -> run CTest
```

The script is intentionally a thin wrapper. It prints the underlying command
before it runs it, so you can learn the CMake/CTest shape without memorizing it.
The real build system is still CMake, and the real test runner is still CTest.

Common commands:

```sh
./nexc.sh configure
./nexc.sh build
./nexc.sh test
./nexc.sh check
./nexc.sh clean
./nexc.sh rebuild
```

Frontend inspection commands:

```sh
./nexc.sh tokens examples/minimal.nexs
./nexc.sh ast examples/add.nexs
./nexc.sh ast-graph examples/add.nexs
```

That writes `ast.dot` and `ast.svg` using Graphviz. Use
`./nexc.sh ast-dot <file.nexs>` when you only want raw DOT on stdout.

Useful environment variables:

```sh
BUILD_TYPE=Release ./nexc.sh rebuild
BUILD_DIR=build-release ./nexc.sh check
CMAKE_GENERATOR=Ninja ./nexc.sh configure
```

## 1. Current Pipeline

The compiler currently implements a checked Core v0 frontend:

```text
source text -> SourceFile -> Lexer -> tokens -> Parser -> AST -> SemanticAnalyzer
```

MLIR generation, LLVM lowering, and native code generation are not implemented
yet.

Each stage has a narrow job:

- **SourceFile:** owns the file path and source text.
- **Lexer:** turns characters into tokens.
- **Parser:** turns tokens into a tree-shaped AST.
- **Semantic analysis:** later, decides whether the tree means something valid.

That separation is important. For example, this can parse successfully:

```nex
fn f() -> i32 {
    return true;
}
```

The parser only knows that `return expression;` is valid syntax. A later semantic
pass must reject returning `bool` from an `i32` function.

## 2. Build Shape

The root [CMakeLists.txt](CMakeLists.txt) defines two main targets:

- `nexc_frontend`: reusable compiler frontend library
- `nexc`: command-line executable

The frontend library lives under:

```text
include/nexc/frontend/
src/frontend/
```

The CLI lives at:

```text
src/tools/nexc/main.cpp
```

Keeping the CLI thin matters because the lexer and parser should eventually be
reused by tests, editor tooling, formatters, and future compiler drivers.

## 3. Source Files and Spans

Files:

```text
include/nexc/frontend/source.h
src/frontend/source.cpp
```

`SourceFile` owns the source path and source text. It also records the byte
offset where each line starts, which makes line/column lookup cheap when
printing diagnostics.

`SourceSpan` is a half-open byte range into the source buffer:

```cpp
struct SourceSpan {
    std::size_t start = 0;
    std::size_t end = 0;
};
```

The range `[start, end)` means:

- `start` is included
- `end` is excluded

For example, in:

```nex
fn main() -> void {
```

the token `fn` has span `[0, 2)`.

Spans are one of the most important pieces of compiler infrastructure. They let
later code answer:

- Which source text produced this token?
- Where should an error message point?
- Which AST node caused this diagnostic?

The compiler stores byte offsets internally and converts to line/column only
when printing diagnostics. That keeps the core representation simple.

## 4. Diagnostics and Carets

Files:

```text
include/nexc/frontend/diagnostic.h
src/frontend/diagnostic.cpp
```

A diagnostic is a compiler message tied to source text:

```cpp
struct Diagnostic {
    DiagnosticSeverity severity;
    std::string message;
    SourceSpan span;
};
```

`DiagnosticBag` collects diagnostics during lexing and parsing. The frontend
does not throw exceptions for normal user mistakes like missing semicolons.
Those are expected compiler errors, so they are collected and printed.

Diagnostics now print both a location and a source snippet:

```text
tests/invalid_expression_statement.nexs:2:5: error: only call expressions may be used as expression statements in Core v0
  |     1 + 2;
  |     ^~~~~
```

This is called **caret diagnostics**. The `^` points to the start of the span;
the `~` characters extend across the rest of the source range. For now,
multi-line spans underline only the first line.

## 5. Tokens

Files:

```text
include/nexc/frontend/token.h
src/frontend/token.cpp
```

The lexer turns characters into tokens. A token is a classified piece of source:

```cpp
struct Token {
    TokenKind kind;
    SourceSpan span;
};
```

For example:

```nex
fn add(a: i32, b: i32) -> i32
```

becomes a flat stream shaped like:

```text
KwFn
Identifier(add)
LeftParen
Identifier(a)
Colon
Identifier(i32)
Comma
Identifier(b)
Colon
Identifier(i32)
RightParen
Arrow
Identifier(i32)
```

The token stream does not know that `add` is a function name or that `a: i32` is
a parameter. That structure appears in the parser.

### Keywords vs Identifiers

The lexer scans the longest identifier-like spelling, then checks whether that
spelling is reserved. This is commonly called **maximal munch**.

```nex
letx
```

`letx` is one identifier, not the keyword `let` followed by identifier `x`.

Integer type names such as `i32` and `u64` currently lex as identifiers. The
parser recognizes them as built-in types only while parsing type syntax.

## 6. Lexer

Files:

```text
include/nexc/frontend/lexer.h
src/frontend/lexer.cpp
```

The lexer consumes raw source text and produces `std::vector<Token>`.

Its responsibilities are narrow:

- skip whitespace
- skip comments
- recognize keywords
- recognize identifiers
- recognize integer literals
- recognize operators and punctuation
- attach spans
- report invalid characters and malformed literals

The lexer does not know grammar. It will happily produce a `KwReturn` token at
the top level; the parser decides whether that token is syntactically legal
there.

Important cursor helpers:

- `peek()` looks at the current character without consuming it.
- `peek(1)` looks one character ahead.
- `advance()` consumes one character.
- `match(expected)` consumes the next character only if it matches.

These helpers make multi-character tokens straightforward:

```text
-  -> Minus
-> -> Arrow
=  -> Equal
== -> EqualEqual
```

Whitespace and comments are often called **trivia**. They matter for source
positions but not for grammar, so Core v0 skips them instead of producing
parser-visible tokens.

## 7. AST

Files:

```text
include/nexc/frontend/ast.h
src/frontend/ast.cpp
```

AST means **abstract syntax tree**. Tokens are flat, but programs are nested.

For example:

```nex
return a + b * c;
```

has a tree shape like:

```text
ReturnStmt
  BinaryExpr Plus
    NameExpr a
    BinaryExpr Star
      NameExpr b
      NameExpr c
```

That tree captures precedence. `b * c` is nested under `a + ...`, so later
compiler stages know multiplication happens before addition.

The current AST has four broad families:

- `TranslationUnit`: the whole source file
- `Item`: top-level declarations such as functions and constants
- `Stmt`: statements inside function bodies
- `Expr`: expressions that produce values

The current AST uses `std::unique_ptr` for owned child nodes. This makes
ownership explicit and easy to debug. Arena allocation can be introduced later
if AST allocation becomes noisy or performance-sensitive.

## 8. Parser

Files:

```text
include/nexc/frontend/parser.h
src/frontend/parser.cpp
```

The parser consumes tokens and produces an AST.

Most parser functions are **recursive descent** functions. Each function parses
one kind of syntax:

```text
parseTranslationUnit()
parseItem()
parseFunctionDecl()
parseConstDecl()
parseBlockStmt()
parseStmt()
parseLetStmt()
parseReturnStmt()
parseIfStmt()
parseWhileStmt()
```

This style is useful for an educational compiler because parser code maps
directly to the grammar.

### Statement Ambiguity

In Core v0, a statement beginning with an identifier can mean either assignment
or call statement:

```nex
x = x + 1;
foo(x);
```

The parser resolves this with one token of lookahead:

- identifier followed by `=` means assignment
- otherwise, parse an expression and require it to be a call expression

This will need to evolve when the language adds richer assignment targets such
as indexing or field access.

### Expression Parsing

Expressions use **precedence climbing**.

The parser must make this:

```nex
1 + 2 * 3
```

mean this:

```text
1 + (2 * 3)
```

not this:

```text
(1 + 2) * 3
```

`parseExpr(minPrecedence)` handles binary operators by comparing the next
operator's precedence against the minimum precedence required at the current
recursion level.

Core v0 binary operators are left-associative:

```nex
a - b - c
```

parses as:

```text
(a - b) - c
```

Calls are parsed as postfix expressions so they bind very tightly:

```nex
square(7) + 1
```

becomes:

```text
BinaryExpr Plus
  CallExpr
    Callee
      NameExpr square
    Arguments
      IntegerLiteral 7
  IntegerLiteral 1
```

## 9. Semantic Analysis

Files:

```text
include/nexc/frontend/semantic.h
src/frontend/semantic.cpp
```

Semantic analysis is the first compiler stage that checks **meaning** instead
of syntax.

The parser can build a tree for this:

```nex
fn f() -> i32 {
    return true;
}
```

because it has valid syntax: `return expression;` inside a function body.
Semantic analysis rejects it because the expression has type `bool`, but the
function promised to return `i32`.

The current analyzer checks:

- duplicate top-level names
- duplicate parameter/local names in the same scope
- undefined names
- calls to undefined functions
- function call argument counts
- exact scalar type matching for locals, assignments, arguments, and returns
- assignment only to `let mut`
- `if` / `while` conditions using `bool` or integer types
- `&&`, `||`, and `!` over `bool` or integer operands
- integer arithmetic/comparison operands
- discarded call results: only `void` calls may be statements
- string literals as `str`
- built-in `print(str) -> void` and `println(str) -> void`
- `main`, if present, has no parameters and returns `void` or `i32`
- module-level `const` initializers are compile-time expressions
- integer literals fit their selected type

The analyzer uses a symbol table. A **symbol** is the compiler's record for a
declared name: for example, `x` is an `i32` local, or `add` is a function taking
two `i32` parameters and returning `i32`.

Scopes are tracked as a stack. Entering a block pushes a new scope; leaving the
block pops it. Lookup starts in the innermost scope and walks outward.

The first analyzer is intentionally strict. It does not yet do integer
promotions, coercions, inter-file lookup, full constant folding, or formatted
string interpolation.

## 10. CLI Inspection Modes

File:

```text
src/tools/nexc/main.cpp
```

The CLI currently supports four frontend modes:

```sh
build/nexc --dump-tokens examples/minimal.nexs
build/nexc --dump-ast examples/add.nexs
build/nexc --dump-ast-dot examples/add.nexs
build/nexc --check examples/add.nexs
```

All four modes lex the file first. `--dump-tokens` prints the token stream and
stops. `--dump-ast` and `--dump-ast-dot` pass the token stream into the parser
and print the resulting tree as either plain text or Graphviz DOT. `--check`
parses the file and then runs semantic analysis without dumping the tree.

These modes are intentionally early because they let us inspect every compiler
stage while building it.

## 11. Tests, Golden Files, and CI

CTest is the local test runner:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The current tests cover:

- smoke checks for `--dump-tokens` and `--dump-ast`
- golden output checks for token, AST, and Graphviz DOT dumps
- parser-negative fixtures
- semantic success checks for valid examples
- semantic-negative fixtures for type errors, undefined names, mutability,
  invalid `main`, discarded non-`void` calls, print argument types, and integer
  literal range errors
- golden diagnostic checks for selected semantic errors

The scalable pattern for output-sensitive frontend tests is **golden files**:

```text
tests/golden/dump_tokens/minimal.tokens.txt
tests/golden/dump_ast/add.ast.txt
tests/golden/dump_ast/control_flow.ast.txt
tests/golden/dump_ast_dot/add.dot
tests/golden/diagnostics/semantic_return_type_mismatch.stderr.txt
```

A golden test runs the compiler, captures stdout, and compares it byte-for-byte
against the checked-in file. If the AST dump changes accidentally, the test
fails. If the AST dump changes intentionally, update the golden file in the same
commit as the compiler change.

The helper script is:

```text
cmake/RunGolden.cmake
```

The root `CMakeLists.txt` exposes a small function:

```cmake
add_nexc_golden_test(name mode input expected)
```

That keeps future tests easy to add.

GitHub Actions runs the same build and CTest loop on pushes and pull requests:

```text
.github/workflows/ci.yml
```

The goal is one local command and one CI command path. As more frontend and
semantic tests appear, they should become CTest entries so CI picks them up
automatically.

## 12. Current Limitations

The current frontend does not yet implement:

- type coercions or integer promotions
- full constant-expression evaluation across named `const` values
- precise signed negative constant values
- formatting/interpolation for strings
- runtime implementation for `print` / `println`
- inter-file/module resolution
- MLIR generation
- LLVM lowering
- native code generation

Those are later stages. The current project state is a checked Core v0 frontend,
not a full compiler.

## 13. Recommended Next Steps

The safest next steps are:

1. Decide the next lowering target: a tiny typed IR, direct textual LLVM IR, or
   first MLIR output.
2. Keep semantic tests growing as new frontend behavior appears.
3. Consider a minimal `print_i32` runtime helper soon so examples can show output
   instead of only exit codes.
4. Then lower a tiny valid program such as `fn main() -> i32 { return 42; }`.

Lowering should stay boring at first. The goal is to prove the frontend can feed
a backend with checked Core v0 programs before adding richer language features.
