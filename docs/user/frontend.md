# Frontend User Guide

This guide explains how to use and read the nex frontend tools. It is the
practical companion to the current language reference in
[`docs/reference/language/`](../reference/language/README.md) and the design-era
implementation contract in [`docs/design/frontend_contract.md`](../design/frontend_contract.md).

For a code-level explanation of the implementation, see
`NEXC_HOLY_BOOK.md`.

## Build

For normal development, use the root helper script:

```sh
./nexc.sh check
```

That command configures CMake, builds the compiler, and runs all tests.

To only build:

```sh
./nexc.sh build
```

The raw CMake commands are:

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
./nexc.sh tokens examples/minimal.nexs
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
./nexc.sh ast examples/add.nexs
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

## Dump AST as Graphviz

The parser can also emit a Graphviz DOT graph and render it to SVG:

```sh
./nexc.sh ast-graph examples/add.nexs
```

That writes:

```text
ast.dot
ast.svg
```

Use `./nexc.sh ast-dot examples/add.nexs > ast.dot` if you only want the raw DOT
text. The DOT/SVG output is useful when the textual tree is hard to visually
scan. The textual `--dump-ast` output remains the canonical compact debug view
and golden test format.

## Check Semantics

The semantic analyzer checks whether parsed syntax means a valid nex
program:

```sh
./nexc.sh check-file examples/add.nexs
```

For invalid programs, diagnostics point at the source:

```text
tests/semantic_return_type_mismatch.nexs:2:12: error: cannot return value of type `bool` from function returning `i32`
  |     return true;
  |            ^~~~
```

This pass checks names, function calls, scalar types, mutability, returns,
`main` shape, module-level `const` initializers, and integer literal ranges.

## Dump Typed IR

After parsing and semantic analysis succeed, the compiler can build a small typed
IR for backend-facing inspection:

```sh
./nexc.sh ir examples/add.nexs
```

Example shape:

```text
Module
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

The typed IR is not executable yet. It is the first explicit bridge from checked
checked nex programs toward MLIR/LLVM lowering.

## Dump MLIR

The first lowering slice builds a tiny MLIR module from typed IR and prints it:

```sh
./nexc.sh mlir examples/pipeline_walkthrough.nexs
```

For a smaller comparison example, the output looks like:

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

This is intentionally still educational, but it now covers the current backend
surface: fixed-width integers, booleans, module constants, string literals,
direct calls, built-in printing calls, mutable local storage, assignment,
returning and fallthrough `if`/`else`, `while`, `/`, `%`, unsigned-specific
integer operations, and short-circuiting `&&` / `||` (module `const`
initializers keep a greedy IR encoding for lowering; see
`docs/reference/language/expressions.md`).

When `mlir-opt` is available, CTest also validates selected `--dump-mlir`
outputs with MLIR's verifier. Golden tests catch text drift; verifier tests catch
structurally invalid MLIR.

## Dump LLVM IR

LLVM IR is the next lower inspection stage after MLIR:

```sh
./nexc.sh llvm examples/return_42.nexs
```

That prints:

```llvm
; ModuleID = 'nex_module'
source_filename = "nex_module"

define i32 @main() {
  ret i32 42
}
```

The same mode also works for the current pipeline walkthrough:

```sh
./nexc.sh llvm examples/pipeline_walkthrough.nexs
```

This mode still does not build or run an executable. It lowers MLIR through the
LLVM dialect, translates that dialect to textual LLVM IR, and prints the result
for inspection. When `llvm-as` is available, CTest validates selected
`--dump-llvm` outputs by assembling them to bitcode.

## Compile An Executable

Once `build/nexc` exists, you can use it directly without `nexc.sh`:

```sh
build/nexc examples/return_42.nexs -o build/return_42
```

That path works for current nex programs, including runtime-backed stdout printing
and the first `readln()` stdin slice.
Internally the compiler emits LLVM IR to temporary files, runs `llc` to emit a
relocatable object, then runs `ld.lld` with the usual Linux CRT objects, `-lc`,
and `libnexrt.a`:

```text
nex source -> typed IR -> MLIR -> LLVM IR -> llc -> .o -> ld.lld -> executable
```

The pipeline walkthrough is also supported:

```sh
build/nexc examples/pipeline_walkthrough.nexs -o build/pipeline_walkthrough
```

`print` / `println` now produce observable stdout:

```sh
build/nexc examples/hello.nexs -o build/hello
./build/hello
```

The stdin echo example is also supported:

```sh
build/nexc examples/stdin_echo.nexs -o build/stdin_echo
printf 'typed input\n' | build/stdin_echo
```

## What Exists Now

The current frontend supports this parser surface:

- module-level `const`
- function definitions
- scalar type syntax
- blocks, `let`, `let mut`, assignment, `return`, `if`, `else`, and `while`
- integer and boolean literals
- function calls
- unary operators `-` and `!`
- binary arithmetic, comparison, equality, `&&`, and `||`
- textual and Graphviz AST dumps
- semantic checking with `--check`
- typed IR dumping with `--dump-ir`
- MLIR dumping with `--dump-mlir` for current backend features including strings,
  runtime printing, module constants, integer widths, and unsigned operations
- LLVM IR dumping with `--dump-llvm` for the same backend surface
- native executable compilation with `build/nexc <file.nexs> -o <output>` for
  nex programs
- experimental `readln() -> str` stdin input for direct use with printing
- conditional `mlir-opt` validation for generated MLIR in the test suite
- conditional `llvm-as` validation for generated LLVM IR in the test suite

Semantic analysis intentionally remains small. It does not yet implement
coercions/promotions, definite assignment analysis, arbitrary constant-expression
overflow evaluation, or inter-file/module resolution.

## Mental Model

The frontend is split into stages:

```text
source text -> lexer -> tokens -> parser -> AST -> semantic analysis -> typed IR -> MLIR -> LLVM IR -> executable
```

Each stage should answer only one kind of question:

- Lexer: what are the words and symbols?
- Parser: do those words and symbols form valid syntax?
- Semantic analysis: does that syntax mean something valid?

Keeping these boundaries clear makes the compiler easier to explain, test, and
extend.

## Language Tutorial

This page explains how to use the current frontend tools. It is not meant to be
the long-term user reference for writing nex programs.

For that, use [docs/user/tutorial.md](./tutorial.md). It is
focused on writing nex code: functions, variables, types, conditions, loops,
calls, constants, and common diagnostics.
