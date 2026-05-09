# LLVM / Native First Slice

This note captures the first LLVM backend milestone after the Core v0 walkthrough
can dump verifier-valid MLIR.

The goal was not to build a full optimizing native compiler in one jump. The goal
was to prove one boring path from checked nex source to LLVM IR that LLVM's own
tools accept, then use `clang` to produce a first host executable.

## Starting Point

Use the current walkthrough as the first serious input:

```sh
./nexc.sh mlir examples/pipeline_walkthrough.nexs
```

That program exercises:

- function definitions and calls
- `i32` values
- mutable locals through `memref`
- `while`
- fallthrough `if` / `else`
- arithmetic, comparison, division, and remainder
- `main() -> i32`

This is a good first LLVM target because it already avoids strings, runtime
printing, heap allocation, imports, and user-defined types.

## Proposed Lowering Path

The first LLVM slice reuses MLIR's existing lowering pipeline:

```text
nex typed IR
  -> MLIR func/arith/scf/memref
  -> lower scf/memref toward cf/llvm-compatible forms
  -> MLIR LLVM dialect
  -> LLVM IR
  -> clang
  -> host executable
```

The compiler now keeps this as an inspection artifact:

```sh
./nexc.sh llvm examples/return_42.nexs
```

That is more useful for learning than immediately hiding all lowering behind
`--emit-exe`.

The direct executable form is now:

```sh
build/nexc examples/pipeline_walkthrough.nexs -o build/pipeline_walkthrough
```

The driver writes LLVM IR to a temporary file and delegates object generation and
linking to `clang`. That keeps platform linker details out of `nexc` while the
language runtime is still tiny.

## First Implementation Target

The first LLVM target is:

```sh
./nexc.sh llvm examples/return_42.nexs
```

or the equivalent direct compiler flag:

```sh
build/nexc --dump-llvm examples/return_42.nexs
```

The implemented golden coverage grows in this order:

1. `examples/scalar_expr.nexs`
2. `examples/function_call.nexs`
3. `examples/mutable_locals.nexs`
4. `examples/pipeline_walkthrough.nexs`

That order kept failures small. Each step introduced one more backend concept
instead of debugging calls, memory, loops, and native tooling all at once.

## Validation

LLVM IR golden files live under:

```text
tests/golden/dump_llvm/
```

When `llvm-as` is available, CTest also assembles generated LLVM IR to bitcode.
That catches malformed LLVM IR even if the text looks plausible in review.

Executable tests use `cmake/RunExecutable.cmake`. They compile selected nex
programs, run the result, and compare exit codes. Current coverage includes
`return_42` and `pipeline_walkthrough`.

## Non-Goals For The First Slice

The first LLVM milestone still does not include:

- runtime `print` / `println`
- strings
- module constants
- unsigned integer polish
- direct object emission without delegating to `clang`
- optimization passes beyond what MLIR/LLVM require structurally
- RISC-V-specific output

Those are important, but they should come after the compiler can already show a
plain LLVM IR path for scalar Core v0 programs.
