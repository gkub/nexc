# nex — Language & Compiler Project Overview

## High-Level Vision

nex is a lightweight systems programming language focused on:

- explicit concurrency
- predictable execution
- visible runtime costs
- realtime-safe execution regions
- native code generation
- low runtime overhead
- mathematical clarity without hidden computational cost
- edge/embedded-oriented systems programming

Core philosophy:

> Nothing expensive is implicit.

The language is intentionally designed so that:

- allocations are explicit
- blocking operations are explicit
- concurrency boundaries are explicit
- copies are explicit
- expensive operations are visible in source and IR
- resource usage can be made observable when explicitly requested
- high-level math abstractions remain compatible with low-level performance reasoning

The project is intended both as:

1. a serious educational compiler project
2. a portfolio-grade MLIR/LLVM systems compiler
3. a long-term exploration of explicit, optimizable systems programming

---

# Language Identity

## Language Name

nex

## Compiler Name

```txt
nexc
```

## Philosophy

nex is designed around:

- predictable behavior
- explicit execution semantics
- constrained/realtime-safe regions
- concurrency visibility
- low-level systems clarity
- compiler-visible effects
- optional resource observability
- math abstractions with visible costs

It is NOT intended to:

- replace Rust
- be fully memory safe
- provide garbage collection
- abstract away performance costs
- hide concurrency or blocking behavior
- silently introduce expensive temporaries or allocations

Instead, it aims to provide:

- transparent execution
- explicit costs
- lightweight concurrency
- strong compiler reasoning about runtime behavior
- meaningful diagnostics around effects, resources, and performance-sensitive operations

---

# File Extensions

## Source Files

```txt
.nexs
```

Examples:

```txt
main.nexs
runtime.nexs
```

## Interface/Header Files

```txt
.nexh
```

Examples:

```txt
channels.nexh
math.nexh
```

These act similarly to C/C++ headers, but without textual preprocessing.

---

# Toolchain Naming

## Compiler

```txt
nexc
```

## Future Tooling

```txt
nexfmt    formatter
nexls     language server
libnexrt  runtime library
libnexmath math/runtime support library
```

---

# Repository Strategy

The initial repository is the compiler/toolchain repository:

```txt
nexc/
```

The repository contains both the implementation and the living language/project documentation.

Recommended layout:

```txt
nexc/
  README.md
  nex.md
  docs/
    IMPLEMENTATION_BACKLOG.md
    language/
    design/
  examples/
  src/
  include/
  runtime/
  tests/
```

`README.md` is the concise public entry point.

`nex.md` is the living project overview and AI/context document.

`docs/language/` is for the formal user-facing language definition.

`docs/design/` is for rationale, architecture notes, optimization plans, and implementation strategy.

`docs/IMPLEMENTATION_BACKLOG.md` is the **ordered near-term implementation checklist**
(compiler milestones and documentation tasks); see the section *Ordered
implementation backlog* above.

---

# Documentation Strategy

nex is docs-first.

Features should be defined in writing before implementation. A feature should generally enter the compiler only after the project has defined:

1. purpose
2. syntax
3. semantics
4. constraints
5. valid examples
6. invalid examples
7. expected diagnostics
8. rough compiler representation

The language definition should not accidentally emerge from whatever the compiler happens to implement first.

The living language reference is **[`docs/reference/`](./docs/reference/README.md)**.
The compiler has grown past early milestone labels; backlog and reference docs
track what ships next.

---

# Ordered implementation backlog (near term)

Long-horizon phases are listed under **Planned Language Features** below. For the
**next few concrete milestones** (what to build first, doc checklist), maintain
a single file:

- **[docs/IMPLEMENTATION_BACKLOG.md](./docs/IMPLEMENTATION_BACKLOG.md)**

Update that backlog when sequencing changes; update **`docs/reference/`** when
user-visible behavior ships so new learners are not misled by stale pages.

---

# Intended Language Characteristics

## C-like Syntax

The language intentionally resembles:

- C
- lightweight C++
- Rust-style function syntax

Example:

```c
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

---

# Core Design Pillars

## 1. Explicit Concurrency

Concurrency is visible in source.

Example:

```c
channel<i32> ch;

spawn producer(ch);
spawn consumer(ch);
```

Key operations:

- `spawn`
- `send`
- `recv`
- channels
- tasks

Goal:

- avoid hidden threading/runtime behavior
- encourage message-passing over shared mutable state
- make blocking and scheduling-relevant operations visible

---

## 2. No Hidden Work

nex avoids:

- hidden allocations
- hidden copies
- hidden async suspension
- hidden thread creation
- hidden blocking behavior
- hidden expensive math temporaries

Examples:

```c
copy(buffer);
alloc<i32>(128);
spawn worker();
recv(ch);
```

All expensive work should be visible at source level or reportable by compiler diagnostics/tooling.

---

## 3. Predictable / Realtime-Safe Execution

nex intends to support compiler-verified constrained execution regions.

Example:

```c
realtime fn control_loop() {
    ...
}
```

Potential restrictions:

- no heap allocation
- no blocking calls
- no unknown foreign calls
- no unbounded loops
- no thread spawning
- restricted copies
- restricted instrumentation
- restricted math operations if they allocate, block, or call unknown runtime code

Compiler should track effects through semantic analysis.

---

## 4. Mathematical Clarity Without Hidden Computational Cost

nex should support expressive numerical code while preserving low-level performance visibility.

Target areas:

- fixed-size vectors
- fixed-size matrices
- shape-aware type checking
- matrix/vector multiplication
- dot products
- cross products
- transposition
- output-buffer APIs for expensive operations
- future automatic differentiation and numerical integration experiments

Example direction:

```c
let a: mat<4, 4, f32>;
let b: mat<4, 4, f32>;
let c: mat<4, 4, f32> = a * b;
```

When operations may allocate or create temporaries, the compiler should either make that visible or provide explicit alternatives such as:

```c
matmul_into(out, a, b);
```

nex math should feel like writing optimized C manually, but with the compiler checking dimensions, preventing avoidable mistakes, and eventually lowering fixed-size operations efficiently.

---

## 5. Resource Usage Should Be Observable When Requested

nex should support optional resource observability.

Resource observability means the compiler and runtime can cooperate to report information such as:

- current memory usage per task
- peak memory usage per task
- memory usage per region
- allocation counts
- channel queue depths
- blocking points
- realtime-region violations
- source-attributed allocation sites

Tracking itself has cost, so tracking must be explicit and opt-in.

Potential tracking tiers:

```txt
Level 0: off
Level 1: counters only
Level 2: traced allocation/blocking events
Level 3: full debug/resource timeline
```

Normal builds should have minimal overhead. Debug/profile builds may enable instrumentation.

---

# Language evolution (long horizon)

**Near-term sequencing** (what the team builds next, with doc checklists) lives
only in **[docs/IMPLEMENTATION_BACKLOG.md](./docs/IMPLEMENTATION_BACKLOG.md)**.

The bullets below are **capability buckets**, not versioned releases. What the
compiler accepts today is described under
[`docs/reference/`](./docs/reference/README.md).

**Already in active use** (non-exhaustive): functions; integer/bool/`str`/`void`;
`let` / `let mut`; `if` / `else`; `while` and C-style **`for`**; fixed-size local
arrays with literals and indexing; module-level `const`; formatted `print` /
`println`; stdin helpers (`readln`, parse builtins).

**Concurrency (future):** task functions, channels, spawn/send/recv, bounded
queues, blocking-effect tracking.

**Realtime / effects (future):** `realtime fn`, richer effect tracking,
restricted operations; compiler-visible allocation, blocking, spawning, large
copies, unknown calls, unbounded loops.

**Math / linalg (future):** floating-point scalars, fixed vectors/matrices,
shape-aware checking, dot/matmul/transpose, buffer-oriented APIs, MLIR vector
paths; longer-term research toward AD, integration, accelerators.

**Resource observability (future):** explicit regions, tracked tasks, optional
counters and traces, source-attributed reports, near-zero cost when disabled.

Example direction:

```txt
nexc run --track-resources examples/app.nexs
```

---

# Compiler Architecture

The compiler is intentionally designed to teach:

- compiler fundamentals
- lexical analysis
- parsing
- AST design
- semantic analysis
- effect tracking
- MLIR lowering
- LLVM lowering
- modern compiler architecture
- target-profile-aware code generation

---

# Pipeline

```txt
Source Code
    ↓
Lexer
    ↓
Parser
    ↓
AST
    ↓
Semantic Analysis
    ↓
nex-specific analyses and optimizations
    ↓
MLIR Generation
    ↓
MLIR Lowering Passes
    ↓
LLVM Dialect
    ↓
LLVM IR
    ↓
Target Code
```

---

# Lexer

Planned:

- hand-written lexer
- source locations tracked from day one
- no lexer generators

Responsibilities:

- tokenization
- comments
- keywords
- literals
- operators
- punctuation
- source spans

---

# Parser

Planned:

- hand-written recursive descent parser
- Pratt parsing or precedence climbing for expressions

Goal:

- deeply understand parsing mechanics
- avoid parser-generator abstraction
- produce clear ASTs and diagnostics

---

# AST

Explicit AST node hierarchy.

Examples:

- FunctionDecl
- BlockStmt
- ReturnStmt
- BinaryExpr
- CallExpr
- VariableExpr
- LetStmt
- IfStmt
- WhileStmt
- TypeExpr
- MatrixTypeExpr
- ChannelTypeExpr

AST dumping should exist early for debugging: a textual tree from the compiler, and optionally **Graphviz** (`dot`) output to render expression and statement structure as a graph.

Example:

```txt
FunctionDecl main
  ReturnStmt
    IntegerLiteral 42
```

---

# Semantic Analysis

Planned responsibilities:

- symbol resolution
- scoped symbol tables
- type checking
- function signature validation
- assignment checking
- return checking
- mutability checking
- shape checking for arrays/vectors/matrices
- effect tracking
- realtime-region validation
- copy/allocation/blocking diagnostics

---

# Optimization Strategy

nex should use existing LLVM/MLIR optimization machinery for generic compiler optimizations and implement nex-specific analyses where the language has extra semantic information.

General compiler optimizations:

- constant folding
- constant propagation
- dead code elimination
- control-flow simplification
- SSA promotion through LLVM
- loop-invariant code motion where appropriate

nex-specific optimization and analysis goals:

- effect-aware optimization
- realtime-region verification
- explicit copy/allocation diagnostics
- optional resource instrumentation
- shape-aware linear algebra lowering
- matrix/vector expression fusion
- bounds-check elimination for fixed-size buffers
- region-based memory analysis
- task/channel/resource diagnostics
- RISC-V target-profile support

Detailed optimization goals live in:

```txt
docs/design/optimization_goals.md
```

---

# MLIR Strategy

Initially:

- use standard MLIR dialects

Examples:

- `func`
- `arith`
- `scf`
- `memref`
- `vector`
- `affine`
- `llvm`

Avoid custom dialects initially.

Goal:

- understand lowering pipelines first
- use existing MLIR structures where possible
- add custom nex dialect only if the language needs it

---

# Future MLIR Direction

Potential custom nex dialect later.

Possible operations:

- `nex.spawn`
- `nex.send`
- `nex.recv`
- `nex.alloc_region`
- `nex.copy`
- `nex.realtime_region`
- `nex.track_resource`
- `nex.matmul`
- `nex.matmul_into`

These would lower into:

- standard MLIR dialects
- runtime calls
- threading primitives
- LLVM dialect
- target-specific code

---

# Runtime Philosophy

Small runtime.

Likely implemented in:

- C
- or lightweight C++

Responsibilities:

- print functions
- task runtime
- channel implementation
- threading primitives
- synchronization primitives
- optional resource tracking
- region allocators
- math runtime helpers where compiler lowering is not enough

Avoid:

- garbage collection
- massive runtime complexity
- hidden allocation
- mandatory heavy profiling infrastructure

---

# Planned Runtime Model

Likely:

- pthread-backed tasks initially for hosted targets
- bounded channels
- explicit blocking semantics
- optional tracking counters
- optional traced instrumentation

Potential future:

- lock-free structures
- realtime-safe queues
- thread pools
- deterministic scheduling experiments
- static region allocators
- embedded/FreeRTOS-backed task integration

---

# Target Model

nex programs are compiled for a target profile.

A target profile may define:

- architecture
- pointer width
- integer ABI
- floating-point support
- available runtime services
- heap availability
- task/channel support
- resource-tracking support
- realtime restrictions
- platform intrinsics
- linker/runtime assumptions

Initial practical targets:

```txt
host-linux-x86_64
riscv32-baremetal
riscv32-embedded
```

RISC-V is the preferred first serious non-host ISA target.

C emission is not the primary strategy. nex should primarily lower through MLIR/LLVM to native code.

---

# ESP32 / Embedded Direction

nex should be designed with embedded profiles in mind.

ESP32-class support is most realistic on RISC-V variants such as ESP32-C3/C6-style hardware rather than classic Xtensa-first support.

Embedded profile direction:

- no implicit heap
- static regions preferred
- bounded channels only
- limited runtime
- optional lightweight counters
- no mandatory heavy tracing
- predictable codegen
- target-profile-aware restrictions

---

# Implementation Language

The initial compiler is implemented in modern C++.

Reasons:

- LLVM and MLIR are C++ ecosystems
- C++ is appropriate for systems/compiler work
- the project is intended to build practical C++ experience
- direct integration with LLVM APIs is valuable

---

# Bootstrapping / Self-Hosting Goal

A long-term goal of nex is partial or full self-hosting.

Initial stages:

```txt
Stage 0: nexc written in C++
Stage 1: small nex programs compile and run
Stage 2: runtime helpers or examples written in nex
Stage 3: developer tools written in nex
Stage 4: parts of the compiler frontend written in nex
Stage 5: nexc can compile substantial parts of itself
Stage 6: full self-hosting
```

Self-hosting is not a near-term milestone. It is a long-term validation target for the language's expressiveness, systems capability, and compiler maturity.

---

# Educational Goals

The project is intentionally structured to:

- relearn compiler fundamentals deeply
- become comfortable with MLIR/LLVM
- understand lowering stages
- understand modern IR design
- understand semantic analysis and effect systems
- understand optimization pipelines
- become interview/job-ready for compiler/systems roles

The emphasis is:

- understanding
- architecture
- correctness
- visibility
- maintainability

NOT:

- rushing features
- making a giant language
- reinventing all of Rust/C++
- hiding complexity behind tools too early

---

# Important Non-Goals

nex is NOT intended to initially support:

- classes
- inheritance
- templates
- generics
- macros
- textual preprocessing
- garbage collection
- async/await
- exceptions
- advanced type inference
- closures
- complex metaprogramming

The project intentionally prioritizes:

- simplicity
- coherence
- compiler quality
- execution transparency

---

# Long-Term Interesting Ideas

Potential future research directions:

- compiler-enforced realtime regions
- explicit vectorization/SIMD constructs
- edge/accelerator-aware lowering
- effect systems
- allocation visibility tooling
- execution-cost diagnostics
- deterministic concurrency verification
- shape-aware linear algebra optimization
- automatic differentiation
- numerical integration
- resource timelines and live observability
- RISC-V/embedded target profiles
- partial/full self-hosting

---

# Core Slogans

Primary:

> Nothing expensive is implicit.

Additional concepts:

- explicit concurrency
- visible execution
- predictable systems programming
- compiler-verifiable execution constraints
- mathematical clarity without hidden computational cost
- resource usage should be visible, attributable, and optional to observe
