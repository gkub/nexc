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
- [11. MLIR Lowering](#11-mlir-lowering)
- [12. LLVM IR Lowering](#12-llvm-ir-lowering)
- [13. Runtime Library And Language I/O (Current Status)](#13-runtime-library-and-language-io-current-status)
- [14. Native Executable Driver](#14-native-executable-driver)
  - [End-to-end steps](#end-to-end-steps-what-actually-runs)
  - [What `llc` is](#what-llc-is)
  - [What `ld.lld` is](#what-ldlld-is)
  - [CRT, libc, and linking today](#crt-libc-and-linking-today)
  - [How this compares to `gcc`](#how-this-compares-to-gcc)
- [15. CLI Inspection Modes](#15-cli-inspection-modes)
- [16. Tests, Golden Files, and CI](#16-tests-golden-files-and-ci)
- [17. Current Limitations](#17-current-limitations)
- [18. Recommended Next Steps](#18-recommended-next-steps)

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
./nexc.sh mlir examples/return_42.nexs
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

Before the stages, keep one distinction clear:

```text
building nexc != compiling a nex program
```

When you run:

```sh
./nexc.sh build
```

you are compiling the C++ implementation of the compiler. That produces the tool
we are writing:

```text
C++ source files
  -> CMake configures build/
  -> C++ compiler builds nexc_frontend, nexc_ir, nexc_mlir
  -> linker produces build/nexc
```

No user nex program has been compiled at that point. We have only built the
compiler executable.

When you run:

```sh
./nexc.sh ir examples/pipeline_walkthrough.nexs
```

or:

```sh
./nexc.sh mlir examples/comparison.nexs
```

you are running `build/nexc` on a `.nexs` input file. That is the compiler
pipeline.

The compiler currently implements a checked Core v0 frontend, a first typed IR
dump, and a small MLIR lowering slice:

```text
.nexs source file
  -> SourceFile
  -> Lexer
  -> tokens
  -> Parser
  -> AST
  -> SemanticAnalyzer
  -> typed IR
  -> MLIR
```

LLVM IR dumping and host executable generation are implemented for the current
scalar walkthrough slice.

Each stage has a narrow job:

- **SourceFile:** owns the file path and source text.
- **Lexer:** turns characters into tokens.
- **Parser:** turns tokens into a tree-shaped AST.
- **Semantic analysis:** decides whether the tree means something valid.
- **Typed IR builder:** turns the checked AST into a backend-facing, typed,
resolved representation.
- **MLIR emitter:** lowers the first tiny typed IR slice into a real MLIR module
and prints stable MLIR text.

That separation is important. For example, this can parse successfully:

```nex
fn f() -> i32 {
    return true;
}
```

The parser only knows that `return expression;` is valid syntax. A later semantic
pass must reject returning `bool` from an `i32` function.

### 1.1 Dumps Are Pipeline Stop Points

The `--dump-*` commands are not separate compilers. They run the same pipeline
and stop at different points to print what exists there:

```text
--dump-tokens
  source -> SourceFile -> Lexer -> tokens

--dump-ast / --dump-ast-dot
  source -> SourceFile -> Lexer -> tokens -> Parser -> AST

--check
  source -> SourceFile -> Lexer -> tokens -> Parser -> AST -> SemanticAnalyzer

--dump-ir
  source -> SourceFile -> Lexer -> tokens -> Parser -> AST
         -> SemanticAnalyzer -> typed IR

--dump-mlir
  source -> SourceFile -> Lexer -> tokens -> Parser -> AST
         -> SemanticAnalyzer -> typed IR -> MLIR
```

That means building usually happens before dumping only because `build/nexc`
must exist before you can run it. Once the compiler executable exists, each dump
command is just "run this `.nexs` file through the pipeline until this stage."

### 1.2 Walkthrough Input

Use [examples/pipeline_walkthrough.nexs](examples/pipeline_walkthrough.nexs) as a
small-but-real frontend walkthrough program:

```nex
fn sum_even_to(limit: i32) -> i32 {
    let mut i: i32 = 0;
    let mut total: i32 = 0;

    while (i <= limit) {
        if (i % 2 == 0) {
            total = total + i;
        } else {
            total = total;
        }

        i = i + 1;
    }

    return total;
}

fn main() -> i32 {
    return sum_even_to(10);
}
```

This program uses function calls, parameters, mutable locals, assignment, a
`while` loop, an `if` / `else`, arithmetic, remainder, comparison, and equality.
It is intentionally more useful for learning the pipeline than `return 42;`.

At each current stage:

- **SourceFile** owns exactly this file text and records line starts for
diagnostics.
- **Lexer** turns the text into tokens such as `KwFn`, `Identifier`, `KwWhile`,
`LessEqual`, `Percent`, and `IntegerLiteral`.
- **Parser** turns the flat token stream into an AST with a `FunctionDecl`, a
`WhileStmt`, an `IfStmt`, `AssignStmt` nodes, and nested `BinaryExpr` nodes.
- **SemanticAnalyzer** checks that `i` and `total` are defined and mutable, that
conditions are bool/integer-compatible, that `%`, `+`, `<=`, and `==` operate on
valid types, and that both functions return `i32` correctly.
- **Typed IR** resolves source names into local slots like `$0`, `$1`, and `$2`,
turns expression results into typed temporaries like `%0`, `%1`, and `%2`, and
keeps the loop/if structure explicit.
- **MLIR lowering** now supports this full walkthrough. Mutable locals lower
through `memref`, the loop lowers through `scf.while`, the inner fallthrough
`if` lowers through `scf.if`, and `%` lowers through `arith.remsi`.

To inspect the current backend milestone, run:

```sh
./nexc.sh mlir examples/pipeline_walkthrough.nexs
```

### 1.3 Future Full Compilation Pipeline

The intended full compile path is:

```text
.nexs source file
  -> SourceFile
  -> Lexer
  -> tokens
  -> Parser
  -> AST
  -> SemanticAnalyzer
  -> typed IR
  -> MLIR
  -> lower MLIR dialects
  -> LLVM dialect / LLVM IR
  -> object file
  -> linker
  -> native executable
```

The pipeline now reaches textual LLVM IR:

```sh
./nexc.sh llvm examples/pipeline_walkthrough.nexs
```

It can also ask the real `nexc` binary to produce a host executable:

```sh
build/nexc examples/pipeline_walkthrough.nexs -o build/pipeline_walkthrough
```

That direct command is the important mental model. `nexc.sh` is only a developer
helper for building and testing the compiler itself. The compiler executable is
`build/nexc`; once it exists, it should be able to compile nex programs without
the helper script.

## 2. Build Shape

The root [CMakeLists.txt](CMakeLists.txt) defines four main targets:

- `nexc_frontend`: reusable compiler frontend library
- `nexc_ir`: typed IR model, builder, and dumper
- `nexc_mlir`: first MLIR lowering layer
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

The MLIR lowering layer lives under:

```text
include/nexc/mlir/
src/mlir/
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
- built-in `print(str) -> void`, `println(str) -> void`, and `readln() -> str`
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

- built-ins: `print(str) -> void`, `println(str) -> void`, `readln() -> str`
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
- `println(str)` and `readln()` as resolved built-in calls
- `if` / `else` where both branches return

When IR shape changes intentionally, update the matching golden file in the same
change as the implementation.

### 10.9 What This Is Not Yet

The typed IR is not yet:

- SSA
- complete MLIR lowering beyond the first tiny slice
- LLVM IR
- executable code
- a runtime ABI
- an optimization pipeline

It is the small explicit bridge between "the frontend understands this program"
and "a future backend can lower this program."

### 10.10 Relationship To Lowering

Typed IR is the boundary between nex frontend meaning and backend mechanics. It
is not meant to become a full optimizer immediately. The current lowering path is:

```text
checked AST -> typed IR -> MLIR
```

That keeps the frontend independent from MLIR details while still letting the
backend consume a resolved, typed representation.

## 11. MLIR Lowering

MLIR means Multi-Level Intermediate Representation. It is part of the LLVM
project, but it sits above LLVM IR. LLVM IR is close to machine-level code; MLIR
is a framework for building and transforming higher-level compiler IRs before
eventually lowering toward LLVM or another target.

MLIR is organized around **dialects**. A dialect is a family of operations and
types for one abstraction level. The current nex slice uses:

- `builtin`: module containers and core MLIR infrastructure
- `func`: function definitions, entry-block arguments, calls, and returns
- `arith`: integer constants and arithmetic operations
- `scf`: structured control flow such as `if` / `else`
- `memref`: explicit local storage slots for `let` / `let mut`

This small nex program:

```nex
fn main() -> i32 {
    return 42;
}
```

lowers to:

```mlir
module {
  func.func @main() -> i32 {
    %c42_i32 = arith.constant 42 : i32
    return %c42_i32 : i32
  }
}
```

The lowering also handles straight-line scalar arithmetic and direct calls
between nex functions:

```nex
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    return add(40, 2);
}
```

The newest slice lowers integer comparisons and returning `if` / `else`
statements:

```nex
fn less_than(a: i32, b: i32) -> bool {
    return a < b;
}

fn main() -> i32 {
    if (less_than(40, 42)) {
        return 1;
    } else {
        return 0;
    }
}
```

That produces:

```mlir
module {
  func.func @less_than(%arg0: i32, %arg1: i32) -> i1 {
    %0 = arith.cmpi slt, %arg0, %arg1 : i32
    return %0 : i1
  }
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %c42_i32 = arith.constant 42 : i32
    %0 = call @less_than(%c40_i32, %c42_i32) : (i32, i32) -> i1
    %1 = scf.if %0 -> (i32) {
      %c1_i32 = arith.constant 1 : i32
      scf.yield %c1_i32 : i32
    } else {
      %c0_i32 = arith.constant 0 : i32
      scf.yield %c0_i32 : i32
    }
    return %1 : i32
  }
}
```

There are two important MLIR ideas packed into that output:

- MLIR uses `i1` for a one-bit boolean value. nex calls the source type `bool`;
  this lowering represents it as MLIR `i1`.
- `scf.if` is an expression-like structured operation when it has a result. Each
  branch computes a value and hands it back with `scf.yield`; the outer function
  then returns the result of the whole `scf.if`.

The lowering now also handles function-local storage:

```nex
fn main() -> i32 {
    let x: i32 = 40;
    let mut y: i32 = 2;

    y = y + x;

    return y;
}
```

That produces MLIR shaped like this:

```mlir
module {
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %alloca = memref.alloca() : memref<i32>
    memref.store %c40_i32, %alloca[] : memref<i32>
    %c2_i32 = arith.constant 2 : i32
    %alloca_0 = memref.alloca() : memref<i32>
    memref.store %c2_i32, %alloca_0[] : memref<i32>
    %0 = memref.load %alloca_0[] : memref<i32>
    %1 = memref.load %alloca[] : memref<i32>
    %2 = arith.addi %0, %1 : i32
    memref.store %2, %alloca_0[] : memref<i32>
    %3 = memref.load %alloca_0[] : memref<i32>
    return %3 : i32
  }
}
```

This is deliberately not optimized. Both `let` and `let mut` become explicit
storage slots in this first slice. Later MLIR/LLVM passes can promote obvious
single-assignment locals away, but the initial lowering stays easy to inspect:
allocation, store initializer, load when read, store when assigned.

That produces:

```mlir
module {
  func.func @add(%arg0: i32, %arg1: i32) -> i32 {
    %0 = arith.addi %arg0, %arg1 : i32
    return %0 : i32
  }
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %c2_i32 = arith.constant 2 : i32
    %0 = call @add(%c40_i32, %c2_i32) : (i32, i32) -> i32
    return %0 : i32
  }
}
```

### 11.1 Why MLIR Exists Here

nex eventually wants native code, RISC-V support, shape-aware math lowering,
effect/resource analysis, and LLVM integration. MLIR is useful because it can
represent programs at several abstraction levels:

```text
nex typed IR
  -> MLIR func/arith/scf/memref
  -> lower-level MLIR dialects
  -> LLVM dialect
  -> LLVM IR
  -> native code
```

The current compiler does not do all of that yet. It only proves the first
boundary: typed IR can build a real MLIR module.

### 11.2 SSA In MLIR

SSA means static single assignment: each value name is defined once. MLIR values
are SSA-like. In the output:

```mlir
%c42_i32 = arith.constant 42 : i32
return %c42_i32 : i32
```

`%c42_i32` is produced once and then used by the return operation.

In the function-call example, `%arg0` and `%arg1` are also SSA values. They are
defined by the function entry block rather than by an operation printed inside
the body. This is why lowering a nex parameter read does not emit a memory load
yet: the current typed IR says `LoadLocal $0`, but the MLIR lowering maps that
parameter local directly to `%arg0`.

This does not require nex to build a custom SSA/CFG IR immediately. The current
policy is:

- keep typed IR nex-shaped, semantic, and structured
- lower simple expression results naturally to MLIR SSA values
- let MLIR/LLVM handle generic canonicalization, CSE, control-flow lowering, and
later SSA promotion where appropriate
- add a nex-owned SSA/CFG layer only when a concrete nex-specific optimization or
analysis needs it

Good future reasons for nex-owned SSA/CFG may include effect-aware optimization,
explicit copy/allocation diagnostics, region/resource analysis, shape-aware math
fusion, bounds-check elimination, and realtime/concurrency analysis. Until one
of those becomes concrete, using MLIR first avoids reinventing a large compiler
middle end prematurely.

### 11.3 Boolean And Comparison Lowering

Core v0 source code uses `bool`, `true`, `false`, and operators such as `<` and
`==`. MLIR does not have a nex-specific boolean type. In this slice:

```text
nex bool -> MLIR i1
```

`i1` means an integer type with one bit. That one bit is enough to represent
false (`0`) and true (`1`).

Integer comparisons lower through `arith.cmpi`:

```mlir
%0 = arith.cmpi slt, %arg0, %arg1 : i32
```

Read that as:

```text
compare two i32 values using signed-less-than, producing an i1 result
```

The predicate names are MLIR spellings:

- `eq`: equal
- `ne`: not equal
- `slt`: signed less than
- `sle`: signed less than or equal
- `sgt`: signed greater than
- `sge`: signed greater than or equal

For now, lowering uses signed comparison predicates because the implemented MLIR
slice only accepts `i32`. When unsigned integer lowering is expanded, the
lowerer will need to choose unsigned predicates (`ult`, `ule`, `ugt`, `uge`) for
`u*` source types.

Unary `!` lowers as a comparison against false:

```text
!x  ->  x == false
```

That is intentionally boring and explicit. It keeps the first lowering easy to
inspect before adding more clever canonicalization.

### 11.4 Local Storage Lowering

Core v0 has named local bindings:

```nex
let x: i32 = 40;
let mut y: i32 = 2;
y = y + x;
```

The typed IR represents those as:

```text
DeclareLocal $0 = %0
DeclareLocal $1 mutable = %1
%2: i32 = LoadLocal $1
%3: i32 = LoadLocal $0
%4: i32 = Binary Plus %2, %3
StoreLocal $1 = %4
```

MLIR values are SSA, which means a value like `%2` cannot be reassigned. A source
local, especially `let mut`, is different: it is a place that can be read and
written over time. The current lowering models that place as a zero-dimensional
memref:

```mlir
%alloca = memref.alloca() : memref<i32>
memref.store %c40_i32, %alloca[] : memref<i32>
%0 = memref.load %alloca[] : memref<i32>
```

Read that as:

```text
allocate one i32 slot
store the initializer into it
later, load the current value from that slot
```

Parameters are still different. Function parameters arrive as MLIR block
arguments such as `%arg0`, and Core v0 parameters are immutable, so loading a
parameter can stay a direct SSA alias instead of allocating memory.

This storage-first strategy is intentionally conservative. It gives every local a
clear place to live before we implement optimization. Later passes can promote
simple slots back into SSA values when it is safe.

### 11.5 Structured `if` Lowering

The typed IR keeps control flow structured:

```text
If %condition
  Then
    Block
      ...
      ReturnValue %then_value
  Else
    Block
      ...
      ReturnValue %else_value
```

That maps naturally to MLIR's `scf.if` operation:

```mlir
%1 = scf.if %0 -> (i32) {
  %c1_i32 = arith.constant 1 : i32
  scf.yield %c1_i32 : i32
} else {
  %c0_i32 = arith.constant 0 : i32
  scf.yield %c0_i32 : i32
}
return %1 : i32
```

This is not the same shape as low-level LLVM IR yet. There are no explicit
branch labels in this text. `scf.if` preserves the high-level fact that the
program has an `if` with two regions. Later MLIR passes can lower that structured
operation into lower-level control flow.

Why use `scf.yield` instead of returning directly inside each branch? Because an
`scf.if` with a result behaves like an expression in MLIR: each branch must
produce the value for the whole operation. The function-level `return` happens
after the `scf.if` result exists.

The current implementation lowers both the value-returning shape above and the
fallthrough shape used inside loops:

```nex
if (i % 2 == 0) {
    total = total + i;
} else {
    total = total;
}
```

The fallthrough form produces a no-result `scf.if`. Each branch performs its
stores and then control continues after the operation. Branches that return from
inside a larger non-returning region are still a later control-flow slice because
they require more careful region/terminator handling.

### 11.6 Structured `while` Lowering

The typed IR keeps a while loop as two blocks:

```text
While
  Condition
    Block
      ...
      ConditionValue %cond
  Body
    Block
      ...
```

That maps to MLIR `scf.while`:

```mlir
scf.while : () -> () {
  %cond = ...
  scf.condition(%cond)
} do {
  ...
  scf.yield
}
```

The current lowering does not use loop-carried SSA values yet. That is possible
because Core v0 locals are lowered through explicit `memref` slots: the loop body
updates the slots with `memref.store`, and the next condition evaluation reloads
the current values with `memref.load`.

### 11.7 Implementation Location

The MLIR lowering layer lives under:

```text
include/nexc/mlir/
src/mlir/
```

`src/mlir/textual.cpp` currently builds an MLIR module using the MLIR C++ API,
verifies it, and prints it as text. Internally, it walks typed IR operations and
maintains a map from nex typed IR value IDs to MLIR SSA values. That map is the
core bridge between the two representations:

```text
nex typed IR value `%2`
  -> concrete MLIR Value produced by arith.addi, memref.load, call, or a block argument
```

The public wrapper remains small:

```cpp
void dumpTextualMlir(std::ostream& out, const ir::Module& module);
```

The name still says `Textual` because the user-visible mode dumps MLIR text. The
important implementation detail is that the text is produced by a real MLIR
module, not by hand-concatenating strings.

### 11.8 Installation And Tooling

#### Ubuntu 24.04

Install LLVM/MLIR 18 development packages:

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential clang graphviz
sudo apt-get install -y libmlir-18-dev mlir-18-tools
```

The MLIR package installs headers, CMake config files, libraries, and tools under
`/usr/lib/llvm-18`.

Useful verification commands:

```sh
ls /usr/lib/llvm-18/include/mlir
ls /usr/lib/llvm-18/lib/cmake/mlir
/usr/lib/llvm-18/bin/mlir-opt --version
```

Adding LLVM tools to the shell `PATH` is convenient:

```sh
echo 'export PATH=/usr/lib/llvm-18/bin:$PATH' >> ~/.zshrc
source ~/.zshrc
mlir-opt --version
mlir-translate --version
```

#### macOS (Homebrew)

Apple’s Xcode Command Line Tools supply a host **`clang`** (used as the Mach-O
link driver for `nexc … -o …`). They do **not** ship **`mlir-opt`**, **`llvm-as`**,
or **`llc`** on a typical `PATH`; those come from a full LLVM install.

1. Install the Command Line Tools if needed: `xcode-select --install`.
2. Install [Homebrew](https://brew.sh/) if needed, then:

```sh
brew install cmake ninja llvm graphviz
```

3. Point CMake at Homebrew’s MLIR package. `brew --prefix llvm` resolves the
   install root on both Apple silicon (`/opt/homebrew/opt/llvm` is typical) and
   Intel (`/usr/local/opt/llvm` is typical):

```sh
export PATH="$(brew --prefix llvm)/bin:$PATH"
cmake -S . -B build -DMLIR_DIR="$(brew --prefix llvm)/lib/cmake/mlir"
cmake --build build
```

4. Confirm the tools **`nexc`** and tests invoke exist:

```sh
command -v mlir-opt llc llvm-as
mlir-opt --version
llc --version
```

Native linking on macOS uses **`llc`** from this LLVM prefix plus **`clang`** from
the C toolchain CMake selected (Apple Clang is fine). Keeping
`$(brew --prefix llvm)/bin` ahead of `/usr/bin` avoids picking up a different
`llc` if multiple LLVM builds are installed.

Homebrew’s **`llvm`** formula tracks upstream closely; the major version may differ
from Ubuntu’s LLVM 18 packages. If `find_package(MLIR)` fails, install a matching
LLVM/MLIR or build from source (next paragraph).

Official setup references:

- [MLIR Getting Started](https://mlir.llvm.org/getting_started/)
- [LLVM Getting Started](https://llvm.org/docs/GettingStarted.html)
- [Building LLVM with CMake](https://llvm.org/docs/CMake.html)

On platforms without matching distro packages, building LLVM from source with
`-DLLVM_ENABLE_PROJECTS=mlir` is the standard route documented by upstream LLVM.

### 11.9 Current Command

Current command:

```sh
./nexc.sh mlir examples/comparison.nexs
```

Current output:

```mlir
module {
  func.func @less_than(%arg0: i32, %arg1: i32) -> i1 {
    %0 = arith.cmpi slt, %arg0, %arg1 : i32
    return %0 : i1
  }
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %c42_i32 = arith.constant 42 : i32
    %0 = call @less_than(%c40_i32, %c42_i32) : (i32, i32) -> i1
    %1 = scf.if %0 -> (i32) {
      %c1_i32 = arith.constant 1 : i32
      scf.yield %c1_i32 : i32
    } else {
      %c0_i32 = arith.constant 0 : i32
      scf.yield %c0_i32 : i32
    }
    return %1 : i32
  }
}
```

This proves the next lowering boundary:

```text
checked AST -> typed IR -> MLIR
```

The generated MLIR can be checked by MLIR tooling:

```sh
./nexc.sh mlir examples/comparison.nexs | mlir-opt --verify-diagnostics
```

CTest now does this automatically for selected MLIR examples when `mlir-opt` is
available. The CMake configuration looks for the tool through the MLIR install
metadata and the normal shell `PATH`; if it cannot find the tool, the validation
tests are skipped rather than breaking frontend-only development machines.

The current MLIR lowering now covers the Core v0 backend surface: fixed-width
integer literals, `bool`, string literals, module constants, `+`, `-`, `*`, `/`,
`%`, integer comparisons, eager `&&` / `||`, unary `!`, function parameters,
direct function calls, built-in `print` / `println` calls, function returns,
local declarations, local loads/stores, assignment, returning `if`/`else`,
fallthrough `if`/`else`, and `while`.

String literals are the first place MLIR lowering has to care about runtime
layout. The compiler decodes the source spelling, emits immutable LLVM-dialect
global bytes, and remembers a pointer plus byte length for the typed IR `str`
value. That pointer/length pair is what the printing runtime receives.

## 12. LLVM IR Lowering

LLVM IR is the next representation below the current MLIR slice. It is much
closer to machine code than nex typed IR or structured MLIR:

```text
nex typed IR
  -> MLIR func/arith/scf/memref
  -> MLIR cf/LLVM dialect
  -> textual LLVM IR
```

The current implementation still uses MLIR as the lowering engine. That means
nex does not hand-write LLVM IR directly. Instead:

1. nex builds the same high-level MLIR module used by `--dump-mlir`.
2. MLIR passes lower `scf` into branch-based `cf`.
3. MLIR passes lower `memref`, `func`, `arith`, and `cf` into the LLVM dialect.
4. MLIR translates the LLVM dialect module into real LLVM IR.

The first tiny example:

```nex
fn main() -> i32 {
    return 42;
}
```

prints:

```llvm
; ModuleID = 'nex_module'
source_filename = "nex_module"

define i32 @main() {
  ret i32 42
}
```

The pipeline walkthrough also lowers to LLVM IR now. Its `scf.while` becomes
plain LLVM-style basic blocks and branches, and its memref-backed locals become
LLVM `alloca`, `load`, and `store` operations. That is why the LLVM output is
less beginner-friendly than MLIR: the high-level source structure has been
flattened into lower-level control flow.

This inspection mode is still not native execution by itself. LLVM IR is the
input to later LLVM tools and code generation. **§13** documents what Nex programs can
do at the language/runtime boundary (built-ins, strings, I/O). **§14** documents the
host toolchain driver (`llc`, `ld.lld`) that turns LLVM IR into an executable file.

## 13. Runtime Library And Language I/O (Current Status)

**Scope:** this section is about **what the language and `libnexrt.a` offer today**—not
about how `llc` or `ld.lld` are invoked (that is **§14**).

### libnexrt.a and built-ins

**`libnexrt.a`** is a small static archive linked into every Nex executable today.
It exports stable symbol names that LLVM IR calls for built-ins: printing, stdin
line reading, parse helpers, and `input_ok()` bookkeeping.

### `str`, `readln()`, and ownership

In the language, **`str`** is still lowered as **pointer + byte length**; there is
no general heap string, growable buffer, or **slice** type in Core v0 yet.
**`readln()`** stores the last line in a **process-local scratch buffer**; it is
meant for experiments and small CLIs, not a final ownership or threading story.

#### Integer (and boolean) input from the user: where things stand

**You can turn stdin text into typed values today**, but only through an explicit
two-step pattern the language supports:

1. Read one line as **`str`**: **`readln()`**.
2. Parse with **`parse_i32`**, **`parse_u64`**, or **`parse_bool`**.
3. Check **`input_ok()`** after **`readln`** / parse when you care about failure.

There is **no** single built-in that behaves like C **`scanf`** (“read an `i32`
directly from stdin”) yet—by design so far: parsing stays visible and composes
with future **Result**-style APIs.

**Unsigned / signed coverage:** **`parse_i32`** and **`parse_u64`** exist;
**`parse_i64`** / other widths are not implemented yet if you need them.

#### Formatted strings (“variables in the string”): where things stand

**Not implemented.** Core v0 has:

- **string literals** and **`print` / `println` taking one `str`**, so output is
  either static text or whatever you can assemble into a **`str`** by hand.

There is **no** printf-style format API, **no** Python-style f-string or
interpolation, and **no** standard library helper for “append an integer to a
string” yet—because honest formatting usually implies **allocation**, conversion
rules, and error behavior the language has not finalized.

**Near-term direction** (design-first, then implementation): specify how Nex builds
strings (concatenation, conversion `i32 -> str`, formatting with explicit cost),
then add lexer/parser support if the syntax is not plain function calls. Until
then, interactive programs are limited to patterns like printing literals,
printing **`readln()`** results as-is, or printing **parse results** with separate
`println` calls rather than one interpolated message.

#### Relation to C / `gcc`

**`gcc`** compiles **C** and links objects against **C’s** library and conventions.
That gives C programmers `printf`, `sprintf`, `stdin`, etc. **automatically for C
source**. Nex does not inherit those APIs by virtue of linking libc at the ELF
level; Nex still needs **its own** definitions for formatting, dynamic strings, and
I/O surfaces. Linking libc today only means the **process** can use the same OS
machinery as other hosted binaries; it does not replace Nex language design work.

Built-ins today include **`print`**, **`println`**, **`readln`**, **`parse_i32`**,
**`parse_u64`**, **`parse_bool`**, and **`input_ok()`**; see
[docs/reference/language/builtins_and_io.md](docs/reference/language/builtins_and_io.md)
for behavior details.

## 14. Native Executable Driver

Use the compiler executable directly (not `nexc.sh`) when you want a binary:

```sh
build/nexc examples/return_42.nexs -o build/return_42
```

Sections **1–12** describe the compiler pipeline ending in LLVM IR as text.
**§13** summarizes built-ins, **`str`**, and I/O behavior at the language/runtime
layer. **This section** is strictly the **native driver**: **turn LLVM IR into
machine code with `llc` and link** an ELF executable the Linux kernel can run.

### End-to-end steps (what actually runs)

```text
parse and semantic-check source
  -> typed IR
  -> MLIR lowerings
  -> LLVM IR (written to a temporary .ll file)
  -> llc      : LLVM IR -> relocatable object file (.o)
  -> link     : Linux: ld.lld + ELF CRT + libc + libnexrt.a; Darwin: clang + libnexrt.a -> executable
```

At configure time, CMake records absolute tool paths in `build/generated/nexc_link_config.h`.
**Linux:** the host C compiler is queried for glibc **startup objects** and **`libc`**
(`-print-file-name=Scrt1.o`, `crti.o`, `crtn.o`, `libc.so.6`); **`llc`** and **`ld.lld`**
are discovered under the LLVM install or common distro paths. **Darwin:** **`llc`**
is discovered the same way (often Homebrew LLVM; Xcode may not ship `llc` on
`PATH`); the final link uses **`clang`** from the active C toolchain so Mach-O,
SDK, and **libSystem** match the host.

The archive `build/runtime/libnexrt.a` contains small implementations for built-in
calls (`print`, `readln`, parse helpers, …). `nexc` locates it next to itself, or
you set **`NEXC_RUNTIME_LIBRARY`** to a full path. If required tools were not found
at configure time (Linux: `llc`/`ld.lld`; Darwin: `llc`/`clang`), **`nexc … -o …`**
fails with an error instead of producing a half-linked binary.

### What `llc` is

**LLVM IR is not machine code.** It is a portable text (or bitcode) description of
functions, types, and operations. **`llc`** ("LLVM static compiler") reads that IR
and emits a **relocatable object file** (`.o`) for a chosen target (here: the
host CPU). That file contains real instructions and **unresolved symbol
references** (for example to libc or to `nex_runtime_*` functions) that the
**linker** fills in next.

### What `ld.lld` is

**`ld.lld`** is used only on **Linux** in this tree. On **macOS**, `nexc` links with
**`clang`** instead (same role: consume `.o` plus `libnexrt.a`, produce an executable).

**`ld.lld`** is LLVM's linker (GNU-compatible on Linux). It takes `.o` files and
**static archives** (`.a`), **matches symbol names** across them (`main`,
`nex_runtime_print_str`, …), assigns final layouts, and writes the **ELF
executable**. It also follows the link line we give it: which startup objects to
include, which directories to search for **`-lc`**, and where to write the
output file.

### CRT, libc, and linking today

This subsection is reference material you can come back to when reading link
lines, CI setup, or discussions of “why libc.”

#### What “CRT” stands for

**CRT** usually expands to **C Run-Time** (read “**C runtime**”). People use the
term in two related ways:

1. **Narrow sense:** the **startup and teardown glue** linked into a normal
   executable—the code that runs **before and after** your `main` so the process
   can start and shut down in a way the OS and dynamic linker expect. On Linux
   this shows up as small **object files** such as `Scrt1.o`, `crti.o`, and
   `crtn.o` (exact names vary by toolchain and PIE vs non-PIE). Our CMake queries
   locate these via `-print-file-name=...` so `ld.lld` receives the same family of
   objects a typical C toolchain would use.

2. **Loose sense:** some writers say “CRT” when they mean **everything that is not
   your program’s object code**—startup objects **plus** the C library and related
   conventions. That overlap is why “CRT” sounds vague; when precision matters,
   separate **startup objects**, **libc**, and **your `.o`**.

Neither meaning implies that **Nex source code is C**. These are **link-time**
artifacts for producing a normal ELF executable on Linux.

#### Startup objects (`crt*.o`) vs **libc**

- **`crt*.o` files** are **statically linked** snippets of machine code bundled
  into your executable. They provide the **entry path** from the kernel/dynamic
  linker into hosted user code (eventually reaching `main`).

- **`-lc`** asks the linker to resolve symbols against the **C standard library**
  (`libc.so.6` on typical glibc Linux). That shared library contains a huge set of
  POSIX/C APIs: `write`, `malloc`, most of `printf`, thread helpers, locale
  machinery, and more.

**Nex today:** the native driver links **both**: crt-style startup objects **and**
`-lc`, plus **`libnexrt.a`** (our small archive for Nex built-ins). So the final
binary is in the same **hosted Linux / glibc ecosystem** as programs built with
`gcc`, even though the compiler front half is entirely Nex → LLVM IR.

#### Syscalls vs libc (still independent of Nex syntax)

A **syscall** is a direct kernel interface (`read`, `write`, …). **libc**
implements higher-level behavior **on top of** syscalls (including buffering for
`stdio`, error handling, allocation). The Nex runtime can call syscalls **inside**
`libnexrt.a` while the executable **still** links against libc for the normal
process model—those facts do not contradict each other.

#### libc and linking: stance for this project (explicit)

**Through the current milestone, producing ordinary Linux executables that link
against glibc (`-lc`) and standard startup objects is an acceptable and
intentional baseline.** It keeps debugging, tooling, and OS interaction boringly
familiar.

That choice is about **the hosted executable boundary**, not about defining Nex’s
semantics in terms of C. Language rules, types, and diagnostics remain Nex-owned.
When Nex grows **real** string and I/O APIs, they should be specified in Nex terms;
the implementation may continue to use syscalls, libc helpers, or both **under**
that API—exactly like any systems runtime.

#### Other link modes (reference only)

Toolchains can instead link with **`-nostdlib`** (omit default crt/libc), use an
alternate libc such as **musl**, or target **freestanding** environments. That
usually means supplying your own entry (`_start`), avoiding most libc, and relying
on syscalls or a tiny support library. **That is not the default Nex driver
today**; it remains a future option when the project deliberately targets minimal
or embedded link layouts.

### How this compares to `gcc`

When you run `gcc main.c -o main`, `gcc` is a **driver**: it runs the
preprocessor, the actual compiler (`cc1`), the assembler (`as`), and finally
invokes the **linker** (`ld` or `ld.lld`) with hidden paths to **crt\*.o** and
**`-lc`**. One command hides many steps.

Our pipeline is analogous **after** LLVM IR exists: **`llc`** plays the role of
“compile to object code,” and **`ld.lld`** plays the role of “link into an
executable.” Nex owns everything **up to** LLVM IR; LLVM’s tools own the **machine
code and link** steps in the current driver.

### Executable tests (what CI exercises)

CTest compiles and runs selected programs. Representative commands:

```sh
build/nexc examples/return_42.nexs -o <temp>
build/nexc examples/pipeline_walkthrough.nexs -o <temp>
build/nexc examples/core_v0_backend_coverage.nexs -o <temp>
build/nexc examples/hello.nexs -o <temp>
build/nexc examples/stdin_echo.nexs -o <temp>
```

Expectations: exit code `42`; walkthrough returns `30` (`sum_even_to(10)`);
backend coverage returns `33`; hello prints `Hello, world!`; stdin tests pipe
bytes into `readln()`.

## 15. CLI Inspection Modes

File:

```text
src/tools/nexc/main.cpp
```

The CLI currently supports seven inspection/checking modes:

```sh
build/nexc --dump-tokens examples/minimal.nexs
build/nexc --dump-ast examples/add.nexs
build/nexc --dump-ast-dot examples/add.nexs
build/nexc --dump-ir examples/add.nexs
build/nexc --dump-mlir examples/comparison.nexs
build/nexc --dump-llvm examples/return_42.nexs
build/nexc --check examples/add.nexs
build/nexc examples/return_42.nexs -o build/return_42
build/nexc examples/multifile_lib.nexs examples/multifile_main.nexs -o build/multifile
```

Native compile may list multiple `.nexs` paths before `-o`; the driver reads each file,
concatenates them with newlines, and parses the result once (no separate modules yet).

All modes lex the file first. `--dump-tokens` prints the token stream and stops.
`--dump-ast` and `--dump-ast-dot` pass the token stream into the parser and print
the resulting tree as either plain text or Graphviz DOT. `--check` parses the
file and then runs semantic analysis without dumping the tree. `--dump-ir`,
`--dump-mlir`, and `--dump-llvm` run the same parse and semantic checks, then
build typed IR only if there were no diagnostics. `--dump-ir` prints the
nex-owned typed IR; `--dump-mlir` lowers that IR into the current MLIR slice;
`--dump-llvm` lowers through MLIR's LLVM dialect and prints LLVM IR.

These modes are intentionally early because they let us inspect every compiler
stage while building it.

## 16. Tests, Golden Files, and CI

CTest is the local test runner:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The current tests cover:

- smoke checks for `--dump-tokens` and `--dump-ast`
- golden output checks for token, AST, Graphviz DOT, typed IR, and MLIR
dumps
- conditional MLIR verifier checks for selected `--dump-mlir` outputs when
`mlir-opt` is available
- golden output checks for LLVM IR dumps
- conditional LLVM IR assembler checks for selected `--dump-llvm` outputs when
`llvm-as` is available
- native executable compile/run checks for selected programs when `llc` and
  `ld.lld` were discovered at CMake configure time, including stdout checks for
  `hello.nexs`, stdin/stdout checks for `stdin_echo.nexs`, and a two-file merge
  compile/run using `examples/multifile_lib.nexs` + `examples/multifile_main.nexs`
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
tests/golden/dump_mlir/return_42.mlir
tests/golden/dump_mlir/scalar_expr.mlir
tests/golden/dump_mlir/function_call.mlir
tests/golden/dump_mlir/if_else_returns.mlir
tests/golden/dump_mlir/comparison.mlir
tests/golden/dump_mlir/mutable_locals.mlir
tests/golden/dump_mlir/pipeline_walkthrough.mlir
tests/golden/dump_llvm/return_42.ll
tests/golden/dump_llvm/pipeline_walkthrough.ll
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

MLIR validation uses a separate helper:

```text
cmake/RunMlirVerify.cmake
```

and a separate CMake function:

```cmake
add_nexc_mlir_validation_test(name input)
```

This deliberately checks a different property from the golden tests. A golden
file answers "did the printed text change?" The MLIR verifier answers "does MLIR
accept this generated module as structurally valid?" Both matter: text stability
is useful for review, while verifier acceptance catches broken lowering even if
the output happens to look plausible.

LLVM validation follows the same pattern with:

```text
cmake/RunLlvmVerify.cmake
```

and `llvm-as`. It answers "does LLVM accept this generated `.ll` file?" before we
try to create object files or executables.

Executable validation uses:

```text
cmake/RunExecutable.cmake
```

That helper compiles a nex file with `build/nexc input.nexs -o output`, runs the
resulting executable, checks its exit status, and can also check exact stdout. It
is CTest coverage for the compiler as a producer of runnable programs rather
than only text dumps.

GitHub Actions runs the same build and CTest loop on pushes and pull requests:

```text
.github/workflows/ci.yml
```

The goal is one local command and one CI command path. As more frontend and
semantic tests appear, they should become CTest entries so CI picks them up
automatically.

## 17. Current Limitations

The current compiler does not yet implement:

- type coercions or integer promotions
- precise signed negative constant values
- formatting/interpolation for strings
- short-circuit `&&` / `||` (Core v0 currently documents eager boolean logic)
- general I/O beyond stdout `print` / `println` and the tiny `readln()` slice
- nested returning control flow beyond the currently tested shapes
- array parameters, array returns, and array module constants (locals support
  `[T; N]`; see language reference)
- inter-file/module resolution beyond source concatenation for `nexc … -o`
- embedding LLVM codegen inside `nexc` so object files are emitted without running the external `llc` subprocess (possible future refinement)

Those are later stages. The current project state is a checked Core v0 compiler
path with typed IR, MLIR, LLVM IR dumps, and native executable generation,
including runtime-backed stdout printing and one-line stdin input.

## 18. Recommended Next Steps

This section is the **handoff list** when switching machines or opening a new
chat: it tracks intent and ordering, not every open bug.

**Recently landed (keep docs/tests aligned when you touch nearby code):**

- Multi-file **compile** driver form: `nexc a.nexs b.nexs -o out` concatenates
  sources and parses once (no separate `.nexh` / modules yet). See
  [§15 CLI Inspection Modes](#15-cli-inspection-modes) and
  `docs/reference/toolchain/compiler_cli.md`.

**Fixed arrays (`[T; N]`) — locals landed:**

- Compiler accepts `[T; N]` types, array literals, `expr[i]` loads, and `arr[i] =`
  for `let mut` locals. Parameters, returns, and `const` arrays are still
  rejected with diagnostics.
- Example: `examples/array_fixed.nexs`; language reference:
  `docs/reference/language/types.md`, `docs/reference/language/expressions.md`.

**Still intentionally later:**

1. `.nexh` interface files and real **module/import** resolution (named in
   `nex.md`; not implemented).
2. Formatting/interpolation and the full **Nex I/O** surface (see I/O design
   notes under `docs/design/`).
3. **`&&` / `||` short-circuit** semantics vs today’s eager lowering — decide in
   a dedicated language-design pass.
4. **In-process LLVM object emission** instead of shelling out to `llc` (nice to
   have, not required for language features).

**Standing rule:** keep typed IR, MLIR, LLVM IR, runtime, and executable tests
growing with each language feature; update `docs/reference/language/` when user-
visible behavior stabilizes.

Lowering should stay boring at first. The goal is to prove the frontend can feed
a backend with checked Core v0 programs before adding richer language features.