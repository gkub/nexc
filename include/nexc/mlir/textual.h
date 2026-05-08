#pragma once

#include "nexc/ir/ir.h"

#include <ostream>

namespace nexc::mlir {

// Emit a textual MLIR module from nex typed IR.
//
// The public API intentionally describes the user-visible result, not the
// implementation technique: callers ask for MLIR text that they can inspect,
// compare in golden tests, or feed to `mlir-opt`. When MLIR development packages
// are available, the implementation constructs a real MLIR module with the C++
// API, verifies it, and then prints it. Otherwise, the same API falls back to a
// small handwritten emitter so early development environments can still inspect
// the first lowering slices.
void dumpTextualMlir(std::ostream& out, const ir::Module& module);

} // namespace nexc::mlir
