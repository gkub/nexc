# Core v0 Typed IR

## Purpose

This document sketches the first typed intermediate representation for `nexc`.
The initial dump path is implemented; this note remains the design reference for
growing that layer.

The current compiler has a checked Core v0 frontend:

```text
source -> lexer -> parser -> AST -> semantic analysis
```

The next backend-facing step should introduce a tiny nex-owned typed IR between
semantic analysis and any MLIR/LLVM lowering:

```text
checked AST -> typed IR -> MLIR -> LLVM/native lowering
```

The goal is not to compete with MLIR or LLVM. The goal is to make the boundary
between "nex meaning" and "backend mechanics" explicit before choosing a
lowering strategy.

## Why Not Lower Directly From The AST?

The AST preserves source syntax. That is useful for diagnostics and debugging,
but it is not the best shape for lowering:

- parentheses and source-oriented expression nesting should not matter
- names should already be resolved
- every value should have a known type
- statement-level control flow should be explicit
- module constants and built-ins should have stable references
- future effect, allocation, copy, and target-profile information needs a home

A small typed IR lets the frontend finish its job before backend code starts.

## Initial Design Rules

The first IR should be:

- **typed**: every value-producing instruction carries a Core v0 type
- **resolved**: references point to IR symbols, not source-name lookups
- **structured enough to stay simple**: keep functions, blocks, `if`, and
  `while` recognizable at first
- **boring to lower**: prefer obvious instructions over clever compression
- **diagnostic-friendly**: retain source spans on functions, blocks,
  instructions, and terminators where practical
- **nex-owned**: represent nex-specific semantics before translating to MLIR or
  LLVM concepts

The IR should not perform serious optimization at first. Early lowering should
favor correctness and readability over compactness.

## Non-Goals For The First Version

The first typed IR does not need:

- SSA form
- register allocation
- machine-level layout decisions
- interprocedural optimization
- arbitrary control-flow graphs
- MLIR dialect design
- LLVM IR text emission
- runtime ABI finalization
- arrays, modules, user-defined types, effects, regions, channels, or tasks

Those can layer on later once Core v0 scalar lowering works.

## Core v0 Type Model

The IR can reuse the frontend's scalar type model initially:

```text
void
bool
str
i8  i16  i32  i64
u8  u16  u32  u64
```

`void` is only valid for function returns and no-value operations. It should not
be assigned to ordinary value IDs.

String values are accepted by semantic analysis today because `print(str)` and
`println(str)` are semantic built-ins. Runtime string representation can remain
unspecified until the first runtime/lowering slice needs it.

## Proposed Shape

The IR can start as an owned module:

```text
Module
  Const*
  Function*

Function
  name
  parameters
  return_type
  body

Block
  Operation*
  Terminator
```

The first version may keep structured control flow:

```text
If
  condition: Value
  then_block: Block
  else_block: Block?

While
  condition: Block or Value
  body: Block
```

This avoids forcing a CFG/SSA decision before the compiler needs one. A later
lowering pass can translate structured IR into MLIR `scf` operations, LLVM basic
blocks, or another lower-level form.

## Values And Operations

Each value-producing operation should return a typed value ID local to its
function body.

Initial operations:

```text
IntegerLiteral(value, type)
BoolLiteral(value)
StringLiteral(value)
LoadLocal(local)
LoadConst(const)
Unary(op, operand, result_type)
Binary(op, left, right, result_type)
Call(function, arguments, result_type)
```

Initial side-effecting operations:

```text
DeclareLocal(local, type, mutable, initializer)
StoreLocal(local, value)
ExprEffect(value-or-call)
```

Initial terminators:

```text
Return()
ReturnValue(value)
```

`ExprEffect` should be used sparingly. In Core v0 it mainly represents valid
discarded `void` calls such as `println("hello");`.

## Name Resolution Boundary

The AST contains source names. The typed IR should contain resolved references:

```text
NameExpr "x"      -> LoadLocal %x
NameExpr "N"      -> LoadConst @N
CallExpr "add"    -> Call @add
CallExpr "print"  -> Call @builtin.print
```

If lowering reaches typed IR construction, name lookup and type checking should
already have succeeded. IR construction may assert frontend invariants rather
than re-diagnosing ordinary semantic errors.

## Integer Semantics

Core v0 integer semantics should follow the language specification:

- fixed-width integer types
- runtime arithmetic wraps for signed and unsigned integer operations
- constant-expression overflow remains a compile-time diagnostic

The IR should preserve enough type information for lowering to choose the
correct integer width and signedness. It should not silently widen arithmetic
unless the language later adds explicit promotion rules.

## Built-Ins

`print(str)` and `println(str)` are semantic built-ins today. In typed IR they
should appear as resolved function references with known signatures:

```text
@builtin.print(str) -> void
@builtin.println(str) -> void
```

They do not need real runtime lowering in the first IR milestone. They can be
accepted in IR dumps while native execution remains unsupported.

## First Implementation Slice

The first useful implementation is an IR dump, not native execution.

Command:

```sh
./nexc.sh ir examples/add.nexs
```

Minimum supported input:

```nex
fn main() -> i32 {
    return 42;
}
```

Next inputs:

```nex
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

```nex
fn main() -> void {
    println("hello");
    return;
}
```

The first tests should be golden IR dumps for small valid programs. Invalid
programs should continue to be covered by semantic diagnostics before IR
construction begins.

## Lowering Direction

After typed IR dumps are stable, there are three reasonable lowering paths:

1. Lower structured typed IR to MLIR `func`, `arith`, and `scf` dialects.
2. Lower a tiny scalar subset directly to textual LLVM IR as a learning step.
3. Lower to a lower-level nex IR first, then choose MLIR/LLVM.

The chosen first slice is MLIR-first. The implementation builds a tiny MLIR
module with the MLIR C++ API and prints stable MLIR text for inspection and
golden tests.

Initial command:

```sh
./nexc.sh mlir examples/return_42.nexs
```

Initial output:

```mlir
module {
  func.func @main() -> i32 {
    %c42_i32 = arith.constant 42 : i32
    return %c42_i32 : i32
  }
}
```

The typed IR keeps the lowering choice reversible while the frontend is still
small.

The current scalar/control-flow slice has grown beyond the first literal return.
It can also lower straight-line `i32` arithmetic, integer comparisons, direct
calls between nex functions, and returning `if`/`else`:

```sh
./nexc.sh mlir examples/comparison.nexs
```

That example demonstrates two important MLIR ideas. Function parameters become
entry-block arguments such as `%arg0`; no memory load is needed to read them yet.
Expression results become SSA values produced by MLIR operations such as
`arith.addi`, `arith.cmpi`, and `call`. A returning `if`/`else` becomes
`scf.if`, with each branch using `scf.yield` to produce the value returned by the
whole operation.

## SSA Policy

LLVM IR is SSA, and MLIR values are also SSA-like: a value such as `%0` is
defined once and then used by later operations. That does not mean the nex
compiler needs a custom SSA/CFG IR immediately.

Current policy:

- Keep nex typed IR semantic and structured.
- Let MLIR/LLVM own generic SSA machinery and generic optimization first.
- Lower immutable expression results naturally as MLIR SSA values.
- Treat current function parameters as MLIR block arguments when lowering
  `LoadLocal` operations.
- Lower mutable locals through explicit MLIR `memref` storage until a later pass
  can promote/simplify them.
- Revisit a nex-owned SSA/CFG layer only when a concrete nex-specific analysis
  needs it.

Potential future reasons for nex-owned SSA/CFG:

- effect-aware optimization
- explicit copy/allocation diagnostics
- region/resource analysis
- shape-aware math fusion
- bounds-check elimination using nex array semantics
- realtime/concurrency analyses that need control-flow facts before MLIR

## Open Questions

- Should the first typed IR stay structured, or should it become basic-block CFG
  form immediately?
- Should constants be represented as module-level IR values or inlined at use
  sites after semantic checking?
- How much source span data should the IR retain once diagnostics have already
  happened?
- Should built-ins be modeled as ordinary functions, intrinsic operations, or a
  distinct callable symbol kind?
- Where should future effect and resource annotations attach: functions,
  operations, values, or all three?

## Recommended Next Step

Grow the read-only IR dump path:

```text
parse -> semantic analyze -> build typed IR -> dump typed IR
```

Do not emit LLVM IR or native code until the typed IR and MLIR lowering continue
to represent Core v0 scalar examples clearly and have golden tests plus MLIR
verifier coverage for the relevant shape. The walkthrough example now meets that
bar for the current structured-control-flow slice, so the next backend design
step can be the first LLVM/native lowering plan.
