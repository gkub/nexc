# Arrays, Vectors, And Future Linear Algebra

This note exists to prevent a rushed array design from boxing Nex into a bad
linear algebra story later.

## Why This Needs Care

Arrays are not just a convenience feature for Nex. They are the foundation for:

- buffers used by I/O
- owned strings and slices
- fixed-size math vectors
- matrices and tensors
- shape-aware linear algebra
- realtime-safe stack storage

If Core v1 adds only a vague growable list, the compiler may later struggle to
reason about layout, shape, allocation, and copies.

## Separate Concepts

Nex should probably distinguish these concepts instead of overloading one type:

- Fixed array: known length, inline storage, e.g. `[T; N]`.
- Slice/view: pointer plus length, borrowed storage, e.g. `[]T` or `slice<T>`.
- Vector: growable owned heap buffer, e.g. `Vec<T>`.
- Math vector: fixed-shape numeric value, e.g. `vec3<f32>` or `Vector<f32, 3>`.
- Matrix/tensor: shape-aware numeric storage, e.g. `Matrix<f32, 4, 4>`.

The spelling is not final. The important design choice is that these are not all
the same semantic object.

### What is a “slice” here?

Roughly: **borrowed, contiguous range** — a **pointer (or reference) plus a
length** (and sometimes a capacity is *not* part of a slice; capacity belongs to
an **owning** buffer). A slice does not own storage; it **views** array elements,
`str` bytes, file buffers, or the live portion of a growable vector. Operations
are **index / sub-range / copy-out** — not `push`/`pop`, which change an owning
allocation story.

### Operations like `push` / `pop` / “dynamic length”

Those are **growable-vector** (`Vec<T>`-style) concerns: heap allocation,
reallocation, capacity vs length, and an error/abort policy when allocation
fails. They do **not** belong on **`[T; N]`** without lying about the type
(length is in the type). When vectors exist, **good diagnostics** can steer
learners: “fixed array `xs` has length `N` known at compile time; to append
elements you need …” with a doc link — same spirit as definite-assignment hints.

## Design Staging Rationale

This rationale explains why the backlog starts with fixed arrays before slices
or vectors. Sequencing itself lives in
[`docs/IMPLEMENTATION_BACKLOG.md`](../IMPLEMENTATION_BACKLOG.md).

1. Fixed-size arrays:
   - They need no allocator.
   - They fit stack/realtime constraints.
   - They teach indexing, length, and aggregate layout.

2. Slices/views:
   - They are the natural bridge from arrays to strings and file buffers.
   - They make pointer/length explicit in the language instead of only in MLIR.

3. Growable vectors:
   - `Vec<T>` needs heap allocation, capacity, reallocation, and failure policy.
   - It should not appear before the runtime has an allocation story.

4. Math vectors/matrices:
   - A math `vec4<f32>` should be optimizable as a numeric value.
   - It should not inherit all behavior from a growable `Vec<T>`.

## Open Questions

- Should fixed arrays use `[T; N]`, `[N]T`, or another spelling?
- Are slices written `[]T`, `slice<T>`, or something more Nex-specific?
- Should shape parameters be compile-time constants only?
- How do mutable slices interact with aliasing and realtime regions?
- What is the explicit cost model for copying an array versus borrowing a slice?

## Current Fixed-Array Status

The implemented fixed-array surface is documented normatively in
[`types.md`](../reference/language/types.md). Small example:

```nex
let xs: [i32; 4] = [1, 2, 3, 4];
return xs[0];
```

## What `[T; N]` Means

`[T; N]` is the **type** of a fixed-length array: `N` identical slots of type `T`,
where `N` is a **compile-time** integer literal (and eventually other `const`
expressions). It is **not** a growable buffer and **not** a slice; those are
separate concepts (see [Design Staging Rationale](#design-staging-rationale)).

Surface syntax:

- **Array type:** `[` element-type `;` length `]` — example `[i32; 4]`.
- **Array literal:** `[` expr `,` expr `,` … `]` — length must match the context
  type when one is known.
- **Index:** `primary `[` index-expr `]`** — loads one element.

**Fixed arrays are implemented** for locals, function parameters, function
returns, and module `const` values (`examples/array_fixed.nexs`,
`examples/array_abi.nexs`). Nested fixed arrays use ranked `memref` shapes
(`examples/array_nested.nexs`). **`str`** arrays remain future work.
Uninitialized `let mut` fixed arrays are accepted with per-element definite
assignment.

## Implementation Handoff Checklist (Arrays)

**Fixed arrays (locals + `fn` ABI + `const` + nesting + per-element DA)** are implemented in `nexc`. Keep this list when extending the surface:

1. **Language reference:** `docs/reference/language/types.md`,
   `docs/reference/language/expressions.md`, `docs/reference/language/statements.md`.
2. **Future:** **`str`** arrays, slices, vectors, and explicit copy/borrow
   semantics when those concepts exist.
3. **Tests:** `examples/array_fixed.nexs`, `examples/array_abi.nexs`,
   `examples/array_nested.nexs`, CTest `run_executable_array_fixed`,
   `run_executable_array_abi`, `run_executable_array_nested`,
   `semantic_check_array_*`, `validate_mlir_array_abi`,
   `validate_mlir_array_nested`.
4. **This file:** update open questions when slices/vectors split from `[T; N]`.
