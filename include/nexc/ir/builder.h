#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/ir/ir.h"

namespace nexc::ir {

// Build typed IR from a syntactically and semantically valid nex AST.
//
// This is the first boundary after semantic analysis. The AST is source-shaped:
// it stores names, parentheses, and statement nesting exactly as the parser saw
// them. Typed IR is backend-shaped: names are resolved into locals, constants,
// functions, and built-ins; expression results become typed temporary values.
//
// Logical `&&` and `||` are lowered differently depending on context:
//
// - Inside **functions**, the builder emits `ShortCircuitAnd` / `ShortCircuitOr`
//   so backends can preserve short-circuit evaluation: each arm is a child block
//   whose terminator yields a boolean to an enclosing `scf.if`, not a function
//   return. See `Operation::Kind` in `ir.h` for the payload contract.
//
// - Inside **module `const` initializers**, the builder stays linear: both sides
//   are fully evaluated in the IR stream, each coerced to `bool` with the same
//   “integer zero is false” rule as unary `!`, then combined with `Binary` using
//   the same `AmpAmp` / `PipePipe` token kind. Const initializers are required
//   to be compile-time-evaluable anyway, so eager representation is simpler for
//   MLIR replay and does not change observable behavior for valid programs.
//
// The builder assumes semantic analysis has already accepted the translation
// unit. It may throw std::logic_error if that frontend invariant is violated.
// That is intentional: user-facing errors belong to semantic analysis, while an
// IR builder failure means an internal compiler assumption was broken.
Module buildTypedIr(const TranslationUnit& unit);

} // namespace nexc::ir
