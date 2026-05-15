# Module `const` array lowering: `memref.global` vs repeated materialization

This note compares two ways to lower immutable module constants whose type is a fixed (possibly nested) array.

## Option A — `memref.global` (or equivalent rodata)

**Pros**

- **Code size and IR size**: one definition of the initializer; users of the const refer to a symbol instead of replaying stores.
- **Runtime work**: no stack traffic or repeated memcpy from a serialized literal stream at each use site.
- **Deduplication**: identical `const` bodies can be merged by the linker or by a compiler CSE pass.
- **Read-only placement**: naturally maps to `.rodata` / constant address space, which helps caches and safety reasoning.

**Cons**

- **Pipeline complexity**: you need correct handling of `memref.get_global`, alignment, optional `memref.copy` into a stack slot when the rest of the pipeline expects an alloca, and symbol visibility across translation units.
- **Initializer expressiveness**: globals want dense static representations; very large or dynamically shaped consts do not fit.
- **Debugging / introspection**: slightly more indirection than “a straight line of stores” when reading MLIR dumps.

## Option B — Repeated materialization (e.g. alloca + stores, or inlining the literal stream)

**Pros**

- **Uniform pipeline**: every const use looks like a normal local array literal; fewer special cases in lowering and in tools that only understand stack-like memrefs.
- **Flexibility**: easier to attach debug metadata or to vary lowering per call site without a global symbol table.

**Cons**

- **Duplication**: each reference can replay the same store sequence → larger MLIR/LLVM and more compile time.
- **Runtime cost**: more stores (or memcpys) at startup or on each path that materializes the const, unless later passes deduplicate aggressively.

## Recommendation for nex

- Use **`memref.global` + `memref.get_global`** (or LLVM `global constant`) for **small, statically sized, immutable** `const` values whose initializer is already a constexpr blob (including nested fixed arrays), after verifying the rest of the pipeline accepts the resulting type (ranked memref, same ABI as today’s `memref<Nx…xT>`).
- Keep **materialization** as the fallback when: the initializer is not constexpr, the object is huge, you need a mutable scratch copy, or a backend pass cannot yet promote a global to the stack where required.

This split keeps the common case (small rodata consts) **clean and optimal** without blocking progress on harder initializer forms.
