#pragma once

#include "nexc/ir/ir.h"

#ifdef NEXC_HAS_REAL_MLIR
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#endif

namespace nexc::mlir {

#ifdef NEXC_HAS_REAL_MLIR
// Register the MLIR dialects used by the first nex lowering stage.
//
// The high-level MLIR that nex currently emits is intentionally small:
//
// - func: functions, calls, and returns
// - arith: integer constants and arithmetic/comparison operations
// - scf: structured if/while control flow
// - memref: explicit local storage slots
//
// Keeping this helper next to the module builder means `--dump-mlir` and the new
// LLVM lowering path agree on exactly which dialects a nex module may use.
void loadCoreMlirDialects(::mlir::MLIRContext& context);

// Build and verify the high-level MLIR module for a checked typed IR module.
//
// This function is the reusable bridge from the nex-owned typed IR to MLIR. The
// textual MLIR dumper prints the returned module directly; the LLVM path runs
// conversion passes over the same module before translating it to LLVM IR.
::mlir::OwningOpRef<::mlir::ModuleOp>
buildMlirModule(::mlir::MLIRContext& context, const ir::Module& module);
#endif

} // namespace nexc::mlir
