#include "nexc/mlir/textual.h"

#ifdef NEXC_HAS_REAL_MLIR
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_os_ostream.h"
#endif

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nexc::mlir {

namespace {

std::int64_t parseIntegerLiteral(std::string_view text) {
    // The lexer preserves the original spelling of integer literals. MLIR wants
    // an integer attribute value, not the source text, so lowering normalizes the
    // spelling here. Base 0 accepts both decimal (`42`) and prefixed hex
    // (`0x2a`), matching the Core v0 lexer contract.
    std::size_t parsed = 0;
    const std::int64_t value = std::stoll(std::string(text), &parsed, 0);
    if (parsed != text.size()) {
        throw std::logic_error("integer literal was not fully parsed during MLIR lowering");
    }
    return value;
}

ir::ValueRef requiredValue(const std::optional<ir::ValueRef>& value,
                           std::string_view context) {
    if (!value) {
        throw std::logic_error(std::string(context) + " expected a typed IR value");
    }
    return *value;
}

#ifdef NEXC_HAS_REAL_MLIR
::mlir::Type mlirType(::mlir::OpBuilder& builder, ir::Type type) {
    switch (type.kind) {
    case BuiltinTypeKind::I32:
        return builder.getI32Type();
    case BuiltinTypeKind::Void:
        return {};
    default:
        throw std::logic_error("MLIR lowering only supports i32 and void in this slice");
    }
}

class FunctionLowerer {
public:
    FunctionLowerer(::mlir::OpBuilder& builder, const ir::Function& function)
        : builder_(builder),
          function_(function),
          loc_(builder.getUnknownLoc()) {}

    void lower() {
        // A Core v0 function becomes one MLIR `func.func`. The body is still a
        // single straight-line block in this slice. Structured control flow will
        // later need nested `scf` operations or explicit block/branch lowering.
        llvm::SmallVector<::mlir::Type> parameterTypes;
        for (const ir::Parameter& parameter : function_.parameters) {
            parameterTypes.push_back(mlirType(builder_, parameter.type));
        }

        llvm::SmallVector<::mlir::Type> resultTypes;
        if (!function_.returnType.isVoid()) {
            resultTypes.push_back(mlirType(builder_, function_.returnType));
        }

        const ::mlir::FunctionType type =
            builder_.getFunctionType(parameterTypes, resultTypes);
        ::mlir::func::FuncOp func =
            builder_.create<::mlir::func::FuncOp>(loc_, function_.name, type);

        ::mlir::Block* entry = func.addEntryBlock();
        builder_.setInsertionPointToStart(entry);

        // Parameters are already MLIR SSA values: each block argument is defined
        // once by the function entry block and can be used directly by later
        // operations. The nex typed IR still represents parameter reads as
        // `LoadLocal` operations, so we map each parameter local slot to its
        // corresponding MLIR block argument here.
        for (std::size_t i = 0; i < function_.parameters.size(); ++i) {
            const ir::Parameter& parameter = function_.parameters[i];
            locals_.emplace(parameter.local.id, entry->getArgument(i));
        }

        lowerBlock(function_.body);
    }

private:
    void lowerBlock(const ir::Block& block) {
        for (const ir::Operation& operation : block.operations) {
            lowerOperation(operation);
        }
        lowerTerminator(block.terminator);
    }

    void lowerOperation(const ir::Operation& operation) {
        // Each typed IR operation either creates a new MLIR operation, or aliases
        // an existing MLIR value. The `values_` map is the bridge between the two
        // worlds: typed IR refers to values by stable numeric IDs, while MLIR
        // APIs pass around concrete SSA Value handles.
        switch (operation.kind) {
        case ir::Operation::Kind::IntegerLiteral:
            lowerIntegerLiteral(operation);
            return;
        case ir::Operation::Kind::LoadLocal:
            lowerLoadLocal(operation);
            return;
        case ir::Operation::Kind::Binary:
            lowerBinary(operation);
            return;
        case ir::Operation::Kind::Call:
            lowerCall(operation);
            return;
        default:
            throw std::logic_error("MLIR lowering encountered an unsupported operation");
        }
    }

    void lowerIntegerLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "integer literal");
        if (result.type.kind != BuiltinTypeKind::I32) {
            throw std::logic_error("MLIR integer literal lowering only supports i32");
        }

        auto constant = builder_.create<::mlir::arith::ConstantIntOp>(
            loc_, parseIntegerLiteral(operation.text), 32);
        bindValue(result, constant.getResult());
    }

    void lowerLoadLocal(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "local load");
        const auto local = locals_.find(operation.local.id);
        if (local == locals_.end()) {
            throw std::logic_error("MLIR lowering only supports loading parameters so far");
        }

        // A parameter load does not need a new MLIR operation. It simply gives the
        // typed IR result ID another name for the existing block argument.
        bindValue(result, local->second);
    }

    void lowerBinary(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "binary operation");
        const ::mlir::Value left =
            lookupValue(requiredValue(operation.left, "binary left operand"));
        const ::mlir::Value right =
            lookupValue(requiredValue(operation.right, "binary right operand"));

        ::mlir::Value lowered;
        switch (operation.op) {
        case TokenKind::Plus:
            lowered = builder_.create<::mlir::arith::AddIOp>(loc_, left, right);
            break;
        case TokenKind::Minus:
            lowered = builder_.create<::mlir::arith::SubIOp>(loc_, left, right);
            break;
        case TokenKind::Star:
            lowered = builder_.create<::mlir::arith::MulIOp>(loc_, left, right);
            break;
        default:
            throw std::logic_error("MLIR lowering only supports integer +, -, and * for now");
        }

        bindValue(result, lowered);
    }

    void lowerCall(const ir::Operation& operation) {
        if (operation.isBuiltin) {
            throw std::logic_error("MLIR lowering does not support built-in calls yet");
        }

        llvm::SmallVector<::mlir::Value> arguments;
        for (const ir::ValueRef argument : operation.arguments) {
            arguments.push_back(lookupValue(argument));
        }

        llvm::SmallVector<::mlir::Type> resultTypes;
        if (operation.result) {
            resultTypes.push_back(mlirType(builder_, operation.result->type));
        }

        auto call = builder_.create<::mlir::func::CallOp>(
            loc_, operation.text, resultTypes, arguments);
        if (operation.result) {
            bindValue(*operation.result, call.getResult(0));
        }
    }

    void lowerTerminator(const ir::Terminator& terminator) {
        switch (terminator.kind) {
        case ir::Terminator::Kind::Return:
            builder_.create<::mlir::func::ReturnOp>(loc_);
            return;
        case ir::Terminator::Kind::ReturnValue: {
            const ::mlir::Value value =
                lookupValue(requiredValue(terminator.value, "return terminator"));
            builder_.create<::mlir::func::ReturnOp>(loc_, value);
            return;
        }
        case ir::Terminator::Kind::None:
        case ir::Terminator::Kind::ConditionValue:
        case ir::Terminator::Kind::InitValue:
            throw std::logic_error("MLIR lowering expected a function return terminator");
        }
    }

    void bindValue(ir::ValueRef ref, ::mlir::Value value) {
        const auto [_, inserted] = values_.emplace(ref.id, value);
        if (!inserted) {
            throw std::logic_error("typed IR value was defined more than once during MLIR lowering");
        }
    }

    ::mlir::Value lookupValue(ir::ValueRef ref) const {
        const auto found = values_.find(ref.id);
        if (found == values_.end()) {
            throw std::logic_error("MLIR lowering used a typed IR value before it was defined");
        }
        return found->second;
    }

    ::mlir::OpBuilder& builder_;
    const ir::Function& function_;
    ::mlir::Location loc_;
    std::unordered_map<std::size_t, ::mlir::Value> values_;
    std::unordered_map<std::size_t, ::mlir::Value> locals_;
};
#else
std::string textualType(ir::Type type) {
    switch (type.kind) {
    case BuiltinTypeKind::I32:
        return "i32";
    case BuiltinTypeKind::Void:
        return "";
    default:
        throw std::logic_error("textual MLIR lowering only supports i32 and void in this slice");
    }
}

std::string textualConstantName(const ir::Operation& operation) {
    const ir::ValueRef result = requiredValue(operation.result, "integer literal");
    return "%c" + std::to_string(parseIntegerLiteral(operation.text)) + "_" +
           textualType(result.type);
}

std::string textualBinaryOp(TokenKind op) {
    switch (op) {
    case TokenKind::Plus:
        return "arith.addi";
    case TokenKind::Minus:
        return "arith.subi";
    case TokenKind::Star:
        return "arith.muli";
    default:
        throw std::logic_error("textual MLIR lowering only supports integer +, -, and * for now");
    }
}

class TextualFunctionDumper {
public:
    TextualFunctionDumper(std::ostream& out, const ir::Function& function)
        : out_(out),
          function_(function) {}

    void dump() {
        out_ << "  func.func @" << function_.name << '(';
        for (std::size_t i = 0; i < function_.parameters.size(); ++i) {
            if (i != 0) {
                out_ << ", ";
            }

            const ir::Parameter& parameter = function_.parameters[i];
            const std::string argumentName = "%arg" + std::to_string(i);
            localValues_.emplace(parameter.local.id, argumentName);
            out_ << argumentName << ": " << textualType(parameter.type);
        }
        out_ << ')';

        if (!function_.returnType.isVoid()) {
            out_ << " -> " << textualType(function_.returnType);
        }
        out_ << " {\n";

        dumpBlock(function_.body);

        out_ << "  }\n";
    }

private:
    void dumpBlock(const ir::Block& block) {
        for (const ir::Operation& operation : block.operations) {
            dumpOperation(operation);
        }
        dumpTerminator(block.terminator);
    }

    void dumpOperation(const ir::Operation& operation) {
        // This fallback is intentionally small, but it follows the same
        // operation-by-operation lowering model as the real MLIR API path. That
        // keeps development machines without MLIR packages useful for reading
        // output and running the compiler's early tests.
        switch (operation.kind) {
        case ir::Operation::Kind::IntegerLiteral:
            dumpIntegerLiteral(operation);
            return;
        case ir::Operation::Kind::LoadLocal:
            dumpLoadLocal(operation);
            return;
        case ir::Operation::Kind::Binary:
            dumpBinary(operation);
            return;
        case ir::Operation::Kind::Call:
            dumpCall(operation);
            return;
        default:
            throw std::logic_error("textual MLIR lowering encountered an unsupported operation");
        }
    }

    void dumpIntegerLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "integer literal");
        if (result.type.kind != BuiltinTypeKind::I32) {
            throw std::logic_error("textual MLIR integer literal lowering only supports i32");
        }

        const std::string name = textualConstantName(operation);
        out_ << "    " << name << " = arith.constant "
             << parseIntegerLiteral(operation.text) << " : "
             << textualType(result.type) << '\n';
        bindValue(result, name);
    }

    void dumpLoadLocal(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "local load");
        const auto local = localValues_.find(operation.local.id);
        if (local == localValues_.end()) {
            throw std::logic_error("textual MLIR lowering only supports loading parameters so far");
        }

        bindValue(result, local->second);
    }

    void dumpBinary(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "binary operation");
        const std::string left =
            lookupValue(requiredValue(operation.left, "binary left operand"));
        const std::string right =
            lookupValue(requiredValue(operation.right, "binary right operand"));
        const std::string name = ir::valueName(result);

        out_ << "    " << name << " = " << textualBinaryOp(operation.op) << ' '
             << left << ", " << right << " : " << textualType(result.type)
             << '\n';
        bindValue(result, name);
    }

    void dumpCall(const ir::Operation& operation) {
        if (operation.isBuiltin) {
            throw std::logic_error("textual MLIR lowering does not support built-in calls yet");
        }

        if (!operation.result) {
            throw std::logic_error("textual MLIR lowering only supports value-returning calls so far");
        }

        const std::string name = ir::valueName(*operation.result);
        out_ << "    " << name << " = func.call @" << operation.text << '(';
        for (std::size_t i = 0; i < operation.arguments.size(); ++i) {
            if (i != 0) {
                out_ << ", ";
            }
            out_ << lookupValue(operation.arguments[i]);
        }
        out_ << ") : (";
        for (std::size_t i = 0; i < operation.arguments.size(); ++i) {
            if (i != 0) {
                out_ << ", ";
            }
            out_ << textualType(operation.arguments[i].type);
        }
        out_ << ") -> " << textualType(operation.result->type) << '\n';
        bindValue(*operation.result, name);
    }

    void dumpTerminator(const ir::Terminator& terminator) {
        switch (terminator.kind) {
        case ir::Terminator::Kind::Return:
            out_ << "    return\n";
            return;
        case ir::Terminator::Kind::ReturnValue: {
            const ir::ValueRef value =
                requiredValue(terminator.value, "return terminator");
            out_ << "    return " << lookupValue(value) << " : "
                 << textualType(value.type) << '\n';
            return;
        }
        case ir::Terminator::Kind::None:
        case ir::Terminator::Kind::ConditionValue:
        case ir::Terminator::Kind::InitValue:
            throw std::logic_error("textual MLIR lowering expected a function return terminator");
        }
    }

    void bindValue(ir::ValueRef ref, std::string name) {
        const auto [_, inserted] = values_.emplace(ref.id, std::move(name));
        if (!inserted) {
            throw std::logic_error("typed IR value was defined more than once during textual MLIR lowering");
        }
    }

    std::string lookupValue(ir::ValueRef ref) const {
        const auto found = values_.find(ref.id);
        if (found == values_.end()) {
            throw std::logic_error("textual MLIR lowering used a typed IR value before it was defined");
        }
        return found->second;
    }

    std::ostream& out_;
    const ir::Function& function_;
    std::unordered_map<std::size_t, std::string> values_;
    std::unordered_map<std::size_t, std::string> localValues_;
};
#endif

} // namespace

void dumpTextualMlir(std::ostream& out, const ir::Module& module) {
    if (!module.constants.empty()) {
        throw std::logic_error("textual MLIR lowering does not support constants yet");
    }

#ifdef NEXC_HAS_REAL_MLIR
    ::mlir::MLIRContext context;
    context.getOrLoadDialect<::mlir::func::FuncDialect>();
    context.getOrLoadDialect<::mlir::arith::ArithDialect>();

    ::mlir::OpBuilder builder(&context);
    const ::mlir::Location loc = builder.getUnknownLoc();
    ::mlir::OwningOpRef<::mlir::ModuleOp> mlirModule =
        ::mlir::ModuleOp::create(loc);

    builder.setInsertionPointToStart(mlirModule->getBody());
    for (const ir::Function& function : module.functions) {
        FunctionLowerer(builder, function).lower();
        builder.setInsertionPointToEnd(mlirModule->getBody());
    }

    if (failed(::mlir::verify(*mlirModule))) {
        throw std::logic_error("generated MLIR module failed verification");
    }

    llvm::raw_os_ostream rawOut(out);
    mlirModule->print(rawOut);
#else
    out << "module {\n";
    for (const ir::Function& function : module.functions) {
        TextualFunctionDumper(out, function).dump();
    }
    out << "}\n";
#endif
}

} // namespace nexc::mlir
