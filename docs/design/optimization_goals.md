# nex Optimization Goals

## Purpose

This document defines the optimization and static-analysis goals for the nex compiler.

nex should not attempt to reimplement every generic compiler optimization from scratch. The compiler should use MLIR and LLVM for mature general-purpose optimization where possible, while implementing nex-specific analyses and transformations where the language provides additional semantic information.

Guiding principle:

> Let LLVM optimize generic machine code. Let nex optimize and diagnose what only nex semantics can know.

---

# Optimization Philosophy

nex is built around explicit runtime costs.

Optimization in nex should preserve that philosophy:

- expensive operations should remain visible
- allocation and copying should not be silently introduced
- blocking and concurrency effects should be tracked
- realtime constraints should be enforced
- instrumentation should be opt-in
- math abstractions should lower efficiently without hiding major costs
- embedded targets should be able to reject unsupported or expensive features

The compiler should optimize aggressively only when doing so does not contradict the source-level cost model.

---

# Optimization Categories

nex optimization work falls into several categories:

1. general frontend simplification
2. LLVM/MLIR-based optimization
3. nex-specific semantic analysis
4. memory/resource analysis
5. math/linear algebra optimization
6. concurrency/realtime analysis
7. embedded/RISC-V target-profile optimization

---

# Stage 0 — Frontend Correctness

Optimization depends on a correct frontend.

Before serious optimization, the compiler needs:

- source locations
- diagnostics
- tokenization
- parsing
- AST construction
- AST dumping (textual tree; optional Graphviz `dot` output for visualization)
- basic type checking
- scoped symbol tables
- mutability checking
- return checking

**Integer semantics** for constant folding and literal handling follow the Core language definition: fixed-width two’s-complement types, **defined wrapping** at runtime for both signed and unsigned operations, and **compile-time diagnostics** for overflow in constant expressions. See [docs/language/core_v0.md](../language/core_v0.md) §5.

Early optimization should not obscure correctness.

---

# Stage 1 — Basic Frontend Optimizations

These are useful early compiler exercises and provide visible wins.

## Constant Folding

Evaluate compile-time constant expressions.

Example:

```c
let x: i32 = 2 + 3 * 4;
```

can be treated as:

```c
let x: i32 = 14;
```

This requires type-aware literal handling; overflow rules are defined in [docs/language/core_v0.md](../language/core_v0.md) §5 (no undefined overflow; wrapping arithmetic; compile-time overflow diagnostics for constants).

## Constant Propagation

Replace uses of known immutable values with their known value.

Example:

```c
let x: i32 = 10;
let y: i32 = x + 5;
```

can become:

```c
let y: i32 = 15;
```

## Basic Dead Code Elimination

Remove unused computations when they have no observable effects.

Example:

```c
let x: i32 = 1 + 2;
return 42;
```

The unused binding can be removed if it has no effects.

## Control-Flow Simplification

Simplify obviously known branches.

Example:

```c
if (true) {
    return 1;
} else {
    return 2;
}
```

can become:

```c
return 1;
```

---

# Stage 2 — LLVM/MLIR Baseline Optimization

The compiler should initially generate simple, correct IR and allow MLIR/LLVM passes to improve it.

Important baseline optimizations include:

- SSA promotion
- instruction combining
- dead code elimination
- control-flow simplification
- canonicalization
- common subexpression elimination
- loop simplification
- loop-invariant code motion
- basic inlining where appropriate

The early compiler should prioritize clean lowering over clever code generation.

---

# Stage 3 — nex-Specific Semantic Analyses

nex should implement analyses that depend on language semantics.

## Effect Tracking

Functions and expressions may carry effects such as:

```txt
pure
reads memory
writes memory
allocates
copies
blocks
spawns
performs I/O
calls unknown foreign code
```

Effect tracking enables:

- realtime-region verification
- dead-code elimination of pure unused computations
- diagnostics for blocking calls
- diagnostics for allocation/copying
- safer optimization around user-defined functions
- better reasoning about concurrency boundaries

## Realtime-Region Verification

`realtime fn` should reject operations that violate realtime constraints.

Potentially forbidden operations:

- heap allocation
- blocking calls
- spawning tasks
- unknown foreign calls
- unbounded loops
- large or implicit copies
- heavy instrumentation

Example diagnostic:

```txt
error: allocation is not allowed inside realtime function 'control_loop'
```

## Explicit Copy Diagnostics

nex should make copies visible.

If a source construct would introduce a copy or temporary, the compiler should either require an explicit operation or produce a diagnostic.

Example diagnostic:

```txt
warning: matrix expression creates a temporary buffer
note: use matmul_into(out, a, b) to avoid the temporary
```

## Allocation Diagnostics

Allocation sites should be tracked by the compiler.

Diagnostics may report:

- allocation size if statically known
- allocation region
- source location
- whether allocation is allowed in the current context
- whether allocation is compatible with the target profile

---

# Stage 4 — Memory and Resource Optimization

nex should support explicit memory/resource reasoning.

## Region-Based Memory Analysis

If the language supports explicit regions, the compiler should analyze region usage.

Potential reports:

- per-function region usage
- per-task region usage
- peak statically known usage
- illegal region use
- region capacity checks
- allocation lifetime diagnostics

## Stack/Arena Lowering

Where possible, region allocations may lower to:

- stack allocation
- arena allocation
- static buffers
- target-specific memory sections

The compiler should preserve the explicit allocation model while choosing efficient implementations.

## Optional Resource Instrumentation

Instrumentation must be opt-in.

Normal builds should avoid resource-tracking overhead.

Tracked builds may lower operations differently.

Untracked build:

```txt
alloc(region, size)
```

Tracked build:

```txt
nexrt_alloc_tracked(region, size, task_id, source_location_id)
```

Potential tracking levels:

```txt
Level 0: off
Level 1: counters only
Level 2: allocation/blocking event trace
Level 3: full debug/resource timeline
```

## Resource Reports

The compiler/runtime should eventually support reports such as:

```txt
Regions:
  scratch      used=384B peak=768B cap=2048B
  frame_pool   used=8MiB peak=12MiB cap=16MiB

Tasks:
  camera_0     frame_pool=8MiB blocked_on=send
  inference    scratch_peak=768B running
```

---

# Stage 5 — Math and Linear Algebra Optimization

nex should support mathematical clarity without hidden computational cost.

## Shape-Aware Type Checking

Vectors and matrices should carry shape information.

Examples:

```c
let v: vec<3, f32>;
let m: mat<4, 4, f32>;
```

Invalid matrix multiplication should be rejected at compile time.

Example:

```c
let a: mat<2, 3, f32>;
let b: mat<4, 2, f32>;
let c = a * b;
```

Expected diagnostic:

```txt
error: cannot multiply mat<2,3,f32> by mat<4,2,f32>
note: left columns must match right rows
```

## Fixed-Size Loop Unrolling

Small fixed-size vectors and matrices may lower to straight-line code.

Example:

```c
let c: vec<3, f32> = a + b;
```

can lower to elementwise operations without a dynamic loop.

## Matrix/Vector Expression Fusion

Expressions such as:

```c
let y = A * x + b;
```

may otherwise create temporaries.

A fused lowering can compute:

```txt
y[i] = dot(A.row(i), x) + b[i]
```

This reduces memory traffic and avoids temporary buffers.

## Output-Buffer APIs

Expensive operations should have explicit output-buffer forms.

Examples:

```c
matmul_into(out, a, b);
solve_into(x, A, b);
```

These APIs make allocation and ownership visible.

## Bounds-Check Elimination

For fixed-size arrays and provably bounded loops, bounds checks may be removed.

Example:

```c
let mut arr: [4]i32;
let mut i: i32 = 0;

while (i < 4) {
    arr[i] = i;
    i = i + 1;
}
```

The compiler can eventually prove that `i` remains within bounds.

## Vector/MLIR Lowering

Fixed-size vector and matrix operations may lower through MLIR vector, affine, scf, and LLVM dialects where appropriate.

Initial goal:

- generate correct loops or straight-line code

Later goal:

- use MLIR vectorization/lowering facilities
- target SIMD/vector-capable architectures where available
- retain predictable behavior on embedded targets

---

# Stage 6 — Concurrency and Channel Optimization

nex concurrency is explicit, so the compiler can reason about it.

## Channel Specialization

If channel element type and capacity are known, the compiler/runtime can use specialized bounded storage.

Example:

```c
let ch: channel<i32, 8>;
```

Potential implementation:

- fixed-size ring buffer
- known element size
- known capacity
- no heap allocation required
- optional channel-depth counters

## Blocking-Effect Diagnostics

Blocking operations such as `recv` should be visible in the effect system.

Potential diagnostic:

```txt
note: function 'consumer' may block because it calls recv
```

## Task-Local Resource Analysis

Tasks may eventually have known resource usage.

Potential report:

```txt
task worker:
  stack estimate: 736 bytes
  scratch region: 4096 bytes
  heap: unused
  blocking points: 1
```

---

# Stage 7 — Embedded and RISC-V Optimization

nex should support target profiles.

A target profile may define:

- architecture
- pointer width
- integer ABI
- floating-point support
- heap availability
- task/channel support
- runtime services
- instrumentation support
- realtime restrictions

## RISC-V Direction

RISC-V is the preferred first serious non-host ISA target.

The compiler should primarily lower through:

```txt
nex
→ MLIR
→ LLVM dialect
→ LLVM IR
→ LLVM target backend
→ native code
```

C emission is not the primary strategy.

## Embedded Profiles

Embedded profiles may restrict language features.

Potential restrictions:

- no implicit heap
- static regions preferred
- bounded channels only
- limited standard library
- optional lightweight counters only
- no full tracing by default
- no unsupported runtime features
- predictable memory layout

## ESP32-Class Direction

ESP32-class support is most realistic on RISC-V ESP32 variants such as ESP32-C3/C6-style hardware.

Classic Xtensa ESP32 support is not the initial target.

---

# Flagship nex-Specific Optimization Problems

The most important nex-specific optimization and analysis goals are:

1. effect-aware semantic analysis
2. realtime-region verification
3. explicit copy and allocation diagnostics
4. optional resource instrumentation
5. region-based memory analysis
6. shape-aware linear algebra
7. matrix/vector expression fusion
8. bounds-check elimination for fixed-size buffers
9. task/channel/resource diagnostics
10. RISC-V target-profile support

These are more central to nex than simply running generic optimization passes.

---

# Non-Goals

nex should not initially attempt to implement:

- a custom register allocator
- a custom instruction selector
- a custom RISC-V backend
- a full symbolic algebra system
- full symbolic integration
- full automatic differentiation before the core compiler is stable
- speculative optimization that hides costs from the programmer
- mandatory runtime profiling or tracing

---

# Roadmap Summary

```txt
Stage 0: frontend correctness
Stage 1: basic frontend optimizations
Stage 2: LLVM/MLIR baseline optimization
Stage 3: nex-specific semantic analyses
Stage 4: memory/resource optimization
Stage 5: math and linear algebra optimization
Stage 6: concurrency/channel optimization
Stage 7: embedded/RISC-V target-profile optimization
```

---

# Design Rule

Optimization should make nex programs faster, clearer, or more diagnosable without violating the language's central promise:

> Nothing expensive is implicit.
