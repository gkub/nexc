#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/ir/ir.h"

namespace nexc::ir {

// Build typed IR from a syntactically and semantically valid Core v0 AST.
//
// This is the first boundary after semantic analysis. The AST is source-shaped:
// it stores names, parentheses, and statement nesting exactly as the parser saw
// them. Typed IR is backend-shaped: names are resolved into locals, constants,
// functions, and built-ins; expression results become typed temporary values.
//
// The builder assumes semantic analysis has already accepted the translation
// unit. It may throw std::logic_error if that frontend invariant is violated.
// That is intentional: user-facing errors belong to semantic analysis, while an
// IR builder failure means an internal compiler assumption was broken.
Module buildTypedIr(const TranslationUnit& unit);

} // namespace nexc::ir
