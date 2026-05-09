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

## Recommended Order

1. Add fixed-size arrays first.
   - They need no allocator.
   - They fit stack/realtime constraints.
   - They teach indexing, length, and aggregate layout.

2. Add slices/views second.
   - They are the natural bridge from arrays to strings and file buffers.
   - They make pointer/length explicit in the language instead of only in MLIR.

3. Add growable vectors after allocation exists.
   - `Vec<T>` needs heap allocation, capacity, reallocation, and failure policy.
   - It should not appear before the runtime has an allocation story.

4. Add math vectors/matrices as distinct shape-aware types.
   - A math `vec4<f32>` should be optimizable as a numeric value.
   - It should not inherit all behavior from a growable `Vec<T>`.

## Open Questions

- Should fixed arrays use `[T; N]`, `[N]T`, or another spelling?
- Are slices written `[]T`, `slice<T>`, or something more Nex-specific?
- Should shape parameters be compile-time constants only?
- How do mutable slices interact with aliasing and realtime regions?
- What is the explicit cost model for copying an array versus borrowing a slice?

## Working Recommendation

For the next implementation slice, prefer fixed-size arrays over vectors:

```nex
let xs: [i32; 4] = [1, 2, 3, 4];
return xs[0];
```

That gives the compiler a concrete layout problem without dragging in heap
allocation. Growable vectors can wait until the language has ownership,
allocation, and error handling.
