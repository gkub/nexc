#pragma once

#include "nexc/ir/ir.h"

#include <ostream>

namespace nexc::llvm {

// Emit textual LLVM IR from nex typed IR.
//
// This is the first native-backend inspection hook. It does not create an object
// file or executable yet; it prints the LLVM IR so we can review, golden-test,
// and validate the next compiler boundary just like we did for tokens, AST, IR,
// and MLIR.
void dumpTextualLlvmIr(std::ostream& out, const ir::Module& module);

} // namespace nexc::llvm
