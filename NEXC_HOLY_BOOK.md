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
- [10. Typed IR](#10-typed-ir)
- [11. CLI Inspection Modes](#11-cli-inspection-modes)
- [12. Tests, Golden Files, and CI](#12-tests-golden-files-and-ci)
- [13. Current Limitations](#13-current-limitations)
- [14. Recommended Next Steps](#14-recommended-next-steps)

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
./nexc.sh ir examples/add.nexs
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

The compiler currently implements a checked Core v0 frontend and a first typed
IR dump:

```text
source text
  -> SourceFile
  -> Lexer
  -> tokens
  -> Parser
  -> AST
  -> SemanticAnalyzer
  -> typed IR
```

MLIR generation, LLVM lowering, and native code generation are not implemented
yet.

Each stage has a narrow job:

- **SourceFile:** owns the file path and source text.
- **Lexer:** turns characters into tokens.
- **Parser:** turns tokens into a tree-shaped AST.
- **Semantic analysis:** decides whether the tree means something valid.
- **Typed IR builder:** turns the checked AST into a backend-facing, typed,
resolved representation.

That separation is important. For example, this can parse successfully:

```nex
fn f() -> i32 {
    return true;
}
```

The parser only knows that `return expression;` is valid syntax. A later semantic
pass must reject returning `bool` from an `i32` function.

## 2. Build Shape

The root [CMakeLists.txt](CMakeLists.txt) defines three main targets:

- `nexc_frontend`: reusable compiler frontend library
- `nexc_ir`: typed IR model, builder, and dumper
- `nexc`: command-line executable

The frontend library lives under:

```text
include/nexc/frontend/
src/frontend/
```

The typed IR library lives under:

```text
include/nexc/ir/
src/ir/
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

## 10. Typed IR

Files:

```text
include/nexc/ir/ir.h
include/nexc/ir/builder.h
include/nexc/ir/dump.h
src/ir/ir.cpp
src/ir/builder.cpp
src/ir/dump.cpp
```

The typed IR is the first backend-facing representation in `nexc`. It sits after
semantic analysis:

```text
AST + successful semantic analysis -> typed IR
```

The AST is source-shaped. It remembers syntax such as parentheses, statement
nesting, and source names. That is exactly what parsing and diagnostics need,
but it is awkward for backend work. A backend wants to know:

- what type every produced value has
- whether a name refers to a local, a module constant, a function, or a built-in
- which operations happen in what order
- where structured control flow begins and ends
- what source span produced each operation, so later diagnostics can still point
back to user code

The typed IR starts answering those questions without committing to MLIR, LLVM
IR, native code generation, or SSA form.

### 10.1 IR Modules

The root IR object is `ir::Module`:

```cpp
struct Module {
    std::vector<Const> constants;
    std::vector<Function> functions;
};
```

This mirrors the current Core v0 top level: a source file contains module-level
`const` items and `fn` items. Later module/import work can expand this boundary,
but one file to one IR module is enough for the current compiler.

### 10.2 Types

The IR type wrapper currently reuses the frontend's `BuiltinTypeKind`:

```cpp
struct Type {
    BuiltinTypeKind kind = BuiltinTypeKind::Invalid;
};
```

That keeps the first implementation small. It is still valuable because IR
values are explicitly typed:

```text
%2: i32 = Binary Plus %0, %1
```

The important difference from the AST is that the type is no longer just syntax
attached to declarations. The IR operation result itself says, "this instruction
produces an `i32` value."

### 10.3 Values and Locals

The IR has two small reference types:

```cpp
struct ValueRef {
    std::size_t id;
    Type type;
};

struct LocalRef {
    std::size_t id;
};
```

A `ValueRef` names a temporary result such as `%0` or `%1`. These are values
computed by operations.

A `LocalRef` names a storage binding such as `$0`. These come from function
parameters and `let` declarations.

For example:

```text
Function @add($0 a: i32, $1 b: i32) -> i32
  Locals
    $0 a: i32 parameter
    $1 b: i32 parameter
  Block
    %0: i32 = LoadLocal $0
    %1: i32 = LoadLocal $1
    %2: i32 = Binary Plus %0, %1
    ReturnValue %2
```

`$0` and `$1` are stable local slots. `%0`, `%1`, and `%2` are temporary values
created while evaluating the return expression.

### 10.4 Operations

IR operations live inside `Block` objects. The first operation set is deliberately
small:

```text
IntegerLiteral
BoolLiteral
StringLiteral
LoadLocal
LoadConst
Unary
Binary
Call
DeclareLocal
StoreLocal
If
While
```

This is not machine code. It is still close to nex source semantics. For
example, `while` remains a structured operation with a condition block and a body
block. That keeps the first IR readable and makes it a reasonable future input
for MLIR `scf` lowering.

### 10.5 Terminators

Blocks end with a `Terminator` when they produce control-flow meaning:

```text
Return
ReturnValue %n
ConditionValue %n
InitValue %n
```

`Return` and `ReturnValue` are function control flow. `ConditionValue` is used by
structured `while` condition blocks. `InitValue` is used by module-level constant
initializers.

Some blocks currently have no terminator. For example, the body of a `while`
loop can be a sequence of stores that falls through to the next iteration. This
is fine at the structured IR level; a later lower-level CFG or MLIR lowering pass
can make the control-flow edges explicit.

### 10.6 Name Resolution Boundary

The AST contains source names:

```text
NameExpr N
NameExpr a
CallExpr add(...)
```

The typed IR contains resolved operations:

```text
%0: i32 = LoadConst @N
%1: i32 = LoadLocal $0
%2: i32 = Call @add(%0, %1)
Call @builtin.println(%0)
```

This is a major compiler boundary. Once IR construction starts, ordinary
undefined-name and wrong-type errors should already have been reported by
semantic analysis. The IR builder is allowed to assume the frontend accepted the
program; if that invariant is false, it throws an internal `std::logic_error`
rather than trying to produce user-facing diagnostics.

### 10.7 Builder Organization

`src/ir/builder.cpp` owns the AST-to-IR translation.

It first rebuilds the resolved top-level tables it needs:

- built-ins: `print(str) -> void`, `println(str) -> void`
- module constants and their types
- functions, parameter types, and return types

Then it walks each `ConstDecl` and `FunctionDecl`.

Inside functions, it keeps a stack of scopes just like semantic analysis. When it
sees:

```nex
let mut x: i32 = 0;
```

it emits:

```text
%0: i32 = IntegerLiteral 0
DeclareLocal $0 mutable = %0
```

When it sees:

```nex
x = x + 1;
```

it emits:

```text
%4: i32 = LoadLocal $0
%5: i32 = IntegerLiteral 1
%6: i32 = Binary Plus %4, %5
StoreLocal $0 = %6
```

That separation matters. Loading a local produces a value. Storing to a local is
a side-effecting operation that does not produce a value.

### 10.8 Dumper and Golden Tests

`src/ir/dump.cpp` turns the IR into stable text. This is intentionally similar to
the AST dumper: the text format is a learning/debugging view and a golden-test
surface, not a serialized object format.

The current golden IR tests live under:

```text
tests/golden/dump_ir/
```

They cover:

- constants, function calls, and `main` returning `i32`
- mutable locals, assignment, and `while`
- `println(str)` as a resolved built-in call
- `if` / `else` where both branches return

When IR shape changes intentionally, update the matching golden file in the same
change as the implementation.

### 10.9 What This Is Not Yet

The typed IR is not yet:

- SSA
- MLIR
- LLVM IR
- executable code
- a runtime ABI
- an optimization pipeline

It is the small explicit bridge between "the frontend understands this program"
and "a future backend can lower this program."

## 11. CLI Inspection Modes

File:

```text
src/tools/nexc/main.cpp
```

The CLI currently supports five inspection/checking modes:

```sh
build/nexc --dump-tokens examples/minimal.nexs
build/nexc --dump-ast examples/add.nexs
build/nexc --dump-ast-dot examples/add.nexs
build/nexc --dump-ir examples/add.nexs
build/nexc --check examples/add.nexs
```

All modes lex the file first. `--dump-tokens` prints the token stream and stops.
`--dump-ast` and `--dump-ast-dot` pass the token stream into the parser and print
the resulting tree as either plain text or Graphviz DOT. `--check` parses the
file and then runs semantic analysis without dumping the tree. `--dump-ir` runs
the same parse and semantic checks, then builds and prints typed IR only if there
were no diagnostics.

These modes are intentionally early because they let us inspect every compiler
stage while building it.

## 12. Tests, Golden Files, and CI

CTest is the local test runner:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The current tests cover:

- smoke checks for `--dump-tokens` and `--dump-ast`
- golden output checks for token, AST, Graphviz DOT, and typed IR dumps
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
tests/golden/dump_ir/add.ir.txt
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

## 13. Current Limitations

The current compiler does not yet implement:

- type coercions or integer promotions
- full constant-expression evaluation across named `const` values
- precise signed negative constant values
- formatting/interpolation for strings
- runtime implementation for `print` / `println`
- inter-file/module resolution
- MLIR generation
- LLVM lowering
- native code generation

Those are later stages. The current project state is a checked Core v0 frontend
plus typed IR dumps, not a full compiler.

## 14. Recommended Next Steps

The safest next steps are:

1. Keep typed IR golden tests growing as new Core v0 forms are added.
2. Decide whether the next lowering experiment targets MLIR `func`/`arith`/`scf`
  or a tiny textual LLVM IR subset.
3. Keep semantic tests growing as new frontend behavior appears.
4. Consider a minimal `print_i32` runtime helper once backend lowering can represent
  simple calls.
5. Then lower a tiny valid program such as `fn main() -> i32 { return 42; }`.

Lowering should stay boring at first. The goal is to prove the frontend can feed
a backend with checked Core v0 programs before adding richer language features.