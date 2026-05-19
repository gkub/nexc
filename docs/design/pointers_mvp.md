# Pointer MVP

This note records the first pointer slice. The goal is to teach the compiler
mechanics of addresses and dereference without pretending nex has a full C memory
model yet.

## Implemented surface

```nex
fn main() -> i32 {
    let mut x = 41;
    let p = &x;
    *p = *p + 1;
    return x;
}
```

- `*T` type syntax for one-level pointers to scalar integer, float, or `bool`
  types.
- `&name` address-of for mutable scalar local bindings.
- `*ptr` dereference as an expression.
- `*ptr = value;` dereference assignment.
- Local type inference can infer pointer locals from `&name`.

## Deliberate limits

- No `null`.
- No pointer arithmetic.
- No pointer parameters or return values.
- No module `const` pointers.
- No mutable pointer variables yet (`let mut p = &x;`).
- No pointers to arrays, `str`, `void`, or other pointers yet.
- No heap allocation or ownership model.

The first lowering represents a pointer as the addressable storage slot it
points to. In MLIR today that means a scalar `memref<T>` handle. Address-of
returns the local slot; dereference load/store become `memref.load` /
`memref.store` through that slot.

That is not a permanent ABI promise. It is the smallest useful bridge from source
syntax to a backend value that behaves like an address.

## Next design questions

- Should nex have nullable raw pointers, or reserve `null` for a later explicit
  `Option<*T>` shape?
- How should pointer mutability be spelled: C-style all raw pointers are mutable,
  or Rust-like `*const T` / `*mut T`?
- Should `&x` work for immutable locals as a read-only pointer?
- When arrays and strings get pointer interaction, do we expose element pointers,
  fat pointers/slices, or both?
- What operations become unsafe once the language has pointer arithmetic, casts,
  or heap allocation?
