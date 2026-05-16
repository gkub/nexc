#include "nexc/llvm/textual.h"

#ifdef NEXC_HAS_REAL_MLIR
#include "nexc/mlir/module.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/Transforms/Passes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_os_ostream.h"
#endif

#include <stdexcept>

namespace nexc::llvm {

#ifdef NEXC_HAS_REAL_MLIR
namespace {

// Register the extra dialects needed after the high-level nex MLIR exists.
//
// The first MLIR module uses func/arith/scf/memref. LLVM lowering introduces the
// cf dialect as an intermediate "basic block branch" form and the LLVM dialect
// as the final MLIR representation before exporting to real LLVM IR.
void loadLlvmLoweringDialects(::mlir::MLIRContext& context) {
    nexc::mlir::loadCoreMlirDialects(context);
    context.getOrLoadDialect<::mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<::mlir::LLVM::LLVMDialect>();
}

// Run MLIR's standard lowering passes from structured MLIR to LLVM dialect.
//
// Think of this as a staircase:
//
// 1. `scf` structured loops/ifs become `cf` branch-based control flow.
// 2. `memref` stack slots become LLVM-compatible memory operations.
// 3. `func` functions/calls/returns become LLVM dialect functions/calls/returns.
// 4. `arith` integer operations become LLVM dialect arithmetic/comparisons.
// 5. remaining `cf` branches become LLVM dialect branches.
//
// The final reconcile pass removes temporary conversion glue that MLIR may insert
// while different dialects are changing types.
void lowerToLlvmDialect(::mlir::ModuleOp module) {
    ::mlir::PassManager passes(module.getContext());
#ifdef NEXC_MLIR_HAS_SCF_TO_CONTROL_FLOW_PASS
    passes.addPass(::mlir::createSCFToControlFlowPass());
#else
    passes.addPass(::mlir::createConvertSCFToCFPass());
#endif
    passes.addPass(::mlir::memref::createExpandStridedMetadataPass());
    passes.addPass(::mlir::createFinalizeMemRefToLLVMConversionPass());
    passes.addPass(::mlir::createConvertFuncToLLVMPass());
    passes.addPass(::mlir::createArithToLLVMConversionPass());
    passes.addPass(::mlir::createConvertControlFlowToLLVMPass());
    passes.addPass(::mlir::createReconcileUnrealizedCastsPass());

    if (failed(passes.run(module))) {
        throw std::logic_error("failed to lower MLIR module to LLVM dialect");
    }

    if (failed(::mlir::verify(module))) {
        throw std::logic_error("lowered LLVM dialect module failed verification");
    }
}

} // namespace
#endif

void dumpTextualLlvmIr(std::ostream& out, const ir::Module& module) {
#ifdef NEXC_HAS_REAL_MLIR
    ::mlir::MLIRContext mlirContext;
    loadLlvmLoweringDialects(mlirContext);

    // Start from the exact same high-level MLIR module that `--dump-mlir` prints.
    // This keeps the LLVM path honest: if frontend-to-MLIR behavior changes, the
    // LLVM path sees that change instead of maintaining a second lowering.
    ::mlir::OwningOpRef<::mlir::ModuleOp> mlirModule =
        nexc::mlir::buildMlirModule(mlirContext, module);

    lowerToLlvmDialect(*mlirModule);

    // Translation from MLIR LLVM dialect to real LLVM IR is interface-based.
    // Registering these translations teaches MLIR how to export the builtin
    // module wrapper and the LLVM dialect operations inside it.
    ::mlir::registerBuiltinDialectTranslation(mlirContext);
    ::mlir::registerLLVMDialectTranslation(mlirContext);

    ::llvm::LLVMContext llvmContext;
    std::unique_ptr<::llvm::Module> llvmModule =
        ::mlir::translateModuleToLLVMIR(*mlirModule, llvmContext, "nex_module");
    if (!llvmModule) {
        throw std::logic_error("failed to translate MLIR LLVM dialect to LLVM IR");
    }

    ::llvm::raw_os_ostream rawOut(out);
    llvmModule->print(rawOut, nullptr);
#else
    (void)out;
    throw std::logic_error("LLVM dumping requires MLIR/LLVM development packages");
#endif
}

} // namespace nexc::llvm
