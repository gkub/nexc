#include "nexc/mlir/module.h"
#include "nexc/mlir/textual.h"

#ifdef NEXC_HAS_REAL_MLIR
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_os_ostream.h"
#endif

#include "nexc/frontend/string_literal_decode.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nexc::mlir {

namespace {

// Convert the source spelling of an integer literal into the numeric value MLIR
// APIs expect for an integer attribute.
//
// The lexer intentionally preserves the original text (`42`, `0x2a`, etc.) so
// token/AST/IR dumps can show what the user wrote. MLIR construction needs the
// actual integer value instead. Base 0 accepts both decimal and prefixed hex,
// matching the Core v0 lexer contract.
[[maybe_unused]] std::int64_t parseIntegerLiteral(std::string_view text) {
    std::size_t parsed = 0;
    const std::int64_t value = std::stoll(std::string(text), &parsed, 0);
    if (parsed != text.size()) {
        throw std::logic_error("integer literal was not fully parsed during MLIR lowering");
    }
    return value;
}

// Unwrap an optional typed IR value that must be present for a lowering case.
//
// Operation payloads are union-like: the Operation::Kind tells us which optional
// fields are meaningful. If a required field is missing here, that is an
// internal builder/lowerer mismatch rather than a user-facing diagnostic.
ir::ValueRef requiredValue(const std::optional<ir::ValueRef>& value,
                           std::string_view context) {
    if (!value) {
        throw std::logic_error(std::string(context) + " expected a typed IR value");
    }
    return *value;
}

[[maybe_unused]] std::string decodeStringLiteralOrThrow(std::string_view raw) {
    std::string decoded;
    std::string err;
    if (!decodeStringLiteralContent(raw, decoded, &err)) {
        throw std::logic_error(err.empty() ? "invalid string literal" : err);
    }
    return decoded;
}

[[maybe_unused]] std::optional<std::string> findStringLiteralRaw(const ir::Function& fn,
                                                                 std::size_t valueId) {
    std::optional<std::string> found;
    const std::function<void(const ir::Block&)> scan = [&](const ir::Block& block) {
        if (found) {
            return;
        }
        for (const ir::Operation& op : block.operations) {
            if (op.kind == ir::Operation::Kind::StringLiteral && op.result &&
                op.result->id == valueId) {
                found = op.text;
                return;
            }
            if (op.kind == ir::Operation::Kind::If) {
                if (op.thenBlock) {
                    scan(*op.thenBlock);
                }
                if (found) {
                    return;
                }
                if (op.elseBlock) {
                    scan(*op.elseBlock);
                }
            }
            if (op.kind == ir::Operation::Kind::While) {
                if (op.conditionBlock) {
                    scan(*op.conditionBlock);
                }
                if (found) {
                    return;
                }
                if (op.bodyBlock) {
                    scan(*op.bodyBlock);
                }
                if (found) {
                    return;
                }
                if (op.stepBlock) {
                    scan(*op.stepBlock);
                }
            }
        }
    };
    scan(fn.body);
    return found;
}

#ifdef NEXC_HAS_REAL_MLIR
::llvm::APInt parseIntegerLiteralApInt(std::string_view text, unsigned width) {
    std::size_t parsed = 0;
    const unsigned long long value = std::stoull(std::string(text), &parsed, 0);
    if (parsed != text.size()) {
        throw std::logic_error("integer literal was not fully parsed during MLIR lowering");
    }
    return ::llvm::APInt(width, value);
}

struct StringValue {
    ::mlir::Value data;
    ::mlir::Value length;
};

unsigned integerBitWidth(ir::Type type) {
    if (type.isFixedArray()) {
        return integerBitWidth(type.elementType());
    }
    switch (type.kind) {
    case BuiltinTypeKind::I8:
    case BuiltinTypeKind::U8:
        return 8;
    case BuiltinTypeKind::I16:
    case BuiltinTypeKind::U16:
        return 16;
    case BuiltinTypeKind::I32:
    case BuiltinTypeKind::U32:
        return 32;
    case BuiltinTypeKind::I64:
    case BuiltinTypeKind::U64:
        return 64;
    case BuiltinTypeKind::Bool:
    case BuiltinTypeKind::Str:
    case BuiltinTypeKind::Void:
    case BuiltinTypeKind::Invalid:
        return 0;
    }
    return 0;
}

bool isUnsignedInteger(ir::Type type) {
    if (type.isFixedArray()) {
        return isUnsignedInteger(type.elementType());
    }
    switch (type.kind) {
    case BuiltinTypeKind::U8:
    case BuiltinTypeKind::U16:
    case BuiltinTypeKind::U32:
    case BuiltinTypeKind::U64:
        return true;
    case BuiltinTypeKind::I8:
    case BuiltinTypeKind::I16:
    case BuiltinTypeKind::I32:
    case BuiltinTypeKind::I64:
    case BuiltinTypeKind::Bool:
    case BuiltinTypeKind::Str:
    case BuiltinTypeKind::Void:
    case BuiltinTypeKind::Invalid:
        return false;
    }
    return false;
}

// Translate the tiny subset of nex IR types currently supported by MLIR lowering
// into concrete MLIR types.
//
// `void` returns an empty Type because MLIR function types represent "no result"
// by omitting the result type entirely, not by using a first-class void value.
::mlir::Type mlirType(::mlir::OpBuilder& builder, ir::Type type) {
    if (type.isFixedArray()) {
        (void)builder;
        throw std::logic_error("mlirType does not map fixed array types to a single scalar");
    }
    switch (type.kind) {
    case BuiltinTypeKind::Bool:
        return builder.getI1Type();
    case BuiltinTypeKind::I8:
    case BuiltinTypeKind::I16:
    case BuiltinTypeKind::I32:
    case BuiltinTypeKind::I64:
    case BuiltinTypeKind::U8:
    case BuiltinTypeKind::U16:
    case BuiltinTypeKind::U32:
    case BuiltinTypeKind::U64:
        return builder.getIntegerType(integerBitWidth(type));
    case BuiltinTypeKind::Void:
        return {};
    default:
        throw std::logic_error("MLIR lowering does not use this type as a single MLIR value");
    }
}

::mlir::MemRefType rankedArrayMemRefType(::mlir::OpBuilder& builder, ir::Type arrayType) {
    if (!arrayType.isFixedArray()) {
        throw std::logic_error("rankedArrayMemRefType expects a fixed array IR type");
    }
    llvm::SmallVector<int64_t, 1> shape;
    shape.push_back(static_cast<int64_t>(arrayType.arrayLength));
    const ::mlir::Type elemTy = mlirType(builder, arrayType.elementType());
    return ::mlir::MemRefType::get(shape, elemTy);
}

void copyRankedMemRef(::mlir::OpBuilder& builder, ::mlir::Location loc,
                      ::mlir::Value from, ::mlir::Value to, ir::Type arrayType) {
    const int64_t n = static_cast<int64_t>(arrayType.arrayLength);
    for (int64_t i = 0; i < n; ++i) {
        auto idx = builder.create<::mlir::arith::ConstantIndexOp>(loc, i);
        auto loaded =
            builder.create<::mlir::memref::LoadOp>(loc, from, idx.getResult());
        builder.create<::mlir::memref::StoreOp>(loc, loaded.getResult(), to,
                                                idx.getResult());
    }
}

::mlir::Value memrefIndexFromValue(::mlir::OpBuilder& builder, ::mlir::Location loc,
                                   ::mlir::Value rawIndex) {
    if (rawIndex.getType().isIndex()) {
        return rawIndex;
    }
    return builder.create<::mlir::arith::IndexCastOp>(loc, builder.getIndexType(),
                                                      rawIndex)
        .getResult();
}

// Map a nex comparison token to MLIR's integer comparison predicate enum.
//
// This first lowering slice only supports i32 comparisons, so relational
// operators use signed predicates (`slt`, `sle`, ...). Once unsigned integers are
// lowered, this helper will need the operand type as input too.
::mlir::arith::CmpIPredicate comparisonPredicate(TokenKind op, bool unsignedOperands) {
    switch (op) {
    case TokenKind::EqualEqual:
        return ::mlir::arith::CmpIPredicate::eq;
    case TokenKind::BangEqual:
        return ::mlir::arith::CmpIPredicate::ne;
    case TokenKind::Less:
        return unsignedOperands ? ::mlir::arith::CmpIPredicate::ult
                                : ::mlir::arith::CmpIPredicate::slt;
    case TokenKind::LessEqual:
        return unsignedOperands ? ::mlir::arith::CmpIPredicate::ule
                                : ::mlir::arith::CmpIPredicate::sle;
    case TokenKind::Greater:
        return unsignedOperands ? ::mlir::arith::CmpIPredicate::ugt
                                : ::mlir::arith::CmpIPredicate::sgt;
    case TokenKind::GreaterEqual:
        return unsignedOperands ? ::mlir::arith::CmpIPredicate::uge
                                : ::mlir::arith::CmpIPredicate::sge;
    default:
        throw std::logic_error("token is not an integer comparison operator");
    }
}

class FunctionLowerer {
public:
    // Lower exactly one typed IR function into the MLIR module currently being
    // built by the caller.
    //
    // The OpBuilder is shared across the whole module so each FunctionLowerer
    // receives it by reference. The IR Function is also borrowed; lowering reads
    // it but does not mutate it.
    // Borrow the module explicitly so module-scope helpers do not have to infer
    // it from the builder's current block. That inference is fragile while MLIR
    // is constructing nested `scf` regions.
    FunctionLowerer(::mlir::OpBuilder& builder,
                    ::mlir::ModuleOp module,
                    const ir::Function& function,
                    const std::unordered_map<std::string, const ir::Const*>& constants)
        : builder_(builder),
          module_(module),
          function_(function),
          constants_(constants),
          loc_(builder.getUnknownLoc()),
          formatLiteralCounter_(0) {}

    // Create the MLIR `func.func`, create its entry block, seed parameter locals,
    // and lower the function body into that entry block.
    void lower() {
        // A Core v0 function becomes one MLIR `func.func`. Straight-line
        // expression code lowers directly into the function entry block, while
        // structured Core control flow maps onto MLIR's structured `scf` dialect.
        llvm::SmallVector<::mlir::Type> parameterTypes;
        for (const ir::Parameter& parameter : function_.parameters) {
            parameterTypes.push_back(mlirType(builder_, parameter.type));
        }

        llvm::SmallVector<::mlir::Type> resultTypes;
        if (usesNativeMainResult()) {
            resultTypes.push_back(builder_.getI32Type());
        } else if (!function_.returnType.isVoid()) {
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
            directLocals_.emplace(parameter.local.id, entry->getArgument(i));
        }

        const bool returned = lowerBlock(function_.body);
        if (!returned && function_.returnType.isVoid()) {
            lowerVoidReturn();
        }
    }

private:
    // Lower a typed IR block.
    //
    // Returns true when lowering emitted a function-level return and no more
    // operations should be inserted in the current function body.
    bool lowerBlock(const ir::Block& block) {
        for (const ir::Operation& operation : block.operations) {
            if (lowerOperation(operation)) {
                return true;
            }
        }
        return lowerTerminator(block.terminator);
    }

    // Lower one typed IR operation.
    //
    // The boolean return mirrors lowerBlock(): false means lowering may continue;
    // true means this operation emitted a function return.
    bool lowerOperation(const ir::Operation& operation) {
        // Each typed IR operation either creates a new MLIR operation, or aliases
        // an existing MLIR value. The `values_` map is the bridge between the two
        // worlds: typed IR refers to values by stable numeric IDs, while MLIR
        // APIs pass around concrete SSA Value handles.
        switch (operation.kind) {
        case ir::Operation::Kind::IntegerLiteral:
            lowerIntegerLiteral(operation);
            return false;
        case ir::Operation::Kind::BoolLiteral:
            lowerBoolLiteral(operation);
            return false;
        case ir::Operation::Kind::StringLiteral:
            lowerStringLiteral(operation);
            return false;
        case ir::Operation::Kind::LoadLocal:
            lowerLoadLocal(operation);
            return false;
        case ir::Operation::Kind::LoadConst:
            lowerLoadConst(operation);
            return false;
        case ir::Operation::Kind::DeclareLocal:
            lowerDeclareLocal(operation);
            return false;
        case ir::Operation::Kind::StoreLocal:
            lowerStoreLocal(operation);
            return false;
        case ir::Operation::Kind::ArrayLiteral:
            lowerArrayLiteral(operation);
            return false;
        case ir::Operation::Kind::IndexLoad:
            lowerIndexLoad(operation);
            return false;
        case ir::Operation::Kind::IndexStore:
            lowerIndexStore(operation);
            return false;
        case ir::Operation::Kind::Unary:
            lowerUnary(operation);
            return false;
        case ir::Operation::Kind::Binary:
            lowerBinary(operation);
            return false;
        case ir::Operation::Kind::Call:
            lowerCall(operation);
            return false;
        case ir::Operation::Kind::If:
            // `lowerFallthroughRegionBlock` lowers fallthrough `if` ops itself so
            // `break`/`continue` inside a branch can end the surrounding loop region.
            return lowerIf(operation);
        case ir::Operation::Kind::While:
            lowerWhile(operation);
            return false;
        case ir::Operation::Kind::Break:
        case ir::Operation::Kind::Continue:
            throw std::logic_error(
                "`break`/`continue` must be lowered inside a loop region, not here");
        default:
            throw std::logic_error("MLIR lowering encountered an unsupported operation");
        }
    }

    // Lower an integer literal to `arith.constant`.
    //
    // MLIR integer types are fixed-width just like Nex integers, so the literal
    // is emitted at the exact bit width selected by semantic analysis. Unsigned
    // and signed integers both use signless MLIR integer types here; signedness
    // matters later when choosing comparison/division/remainder operations.
    void lowerIntegerLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "integer literal");
        const unsigned width = integerBitWidth(result.type);
        if (width == 0) {
            throw std::logic_error("MLIR integer literal lowering expected an integer type");
        }

        const ::mlir::Type mlirIntegerType = mlirType(builder_, result.type);
        const ::mlir::IntegerAttr value =
            builder_.getIntegerAttr(mlirIntegerType,
                                    parseIntegerLiteralApInt(operation.text, width));
        auto constant =
            builder_.create<::mlir::arith::ConstantOp>(loc_, mlirIntegerType, value);
        bindValue(result, constant.getResult());
    }

    // Lower a bool literal to a one-bit MLIR integer constant.
    //
    // nex has a source type named `bool`; this lowering slice represents it as
    // MLIR `i1`, where 0 is false and 1 is true.
    void lowerBoolLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "bool literal");
        auto constant = builder_.create<::mlir::arith::ConstantIntOp>(
            loc_, operation.boolValue ? 1 : 0, 1);
        bindValue(result, constant.getResult());
    }

    // Lower a string literal to the runtime representation used by Core v0.
    //
    // A Nex `str` is not a C string. We lower it as two values:
    //
    // - a pointer to immutable global bytes
    // - an i64 byte length
    //
    // Keeping the length explicit means the runtime does not need a trailing NUL
    // and can eventually support arbitrary byte strings.
    void lowerStringLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "string literal");
        const std::string bytes = decodeStringLiteralOrThrow(operation.text);
        const std::string symbol =
            "__nex_str_" + function_.name + "_" + std::to_string(result.id);

        const ::mlir::Type i8 = builder_.getI8Type();
        const ::mlir::Type arrayType =
            ::mlir::LLVM::LLVMArrayType::get(i8, static_cast<unsigned>(bytes.size()));

        // LLVM globals live at module scope, while this method is usually called
        // while the builder is inserting inside a function. The insertion guard
        // lets us temporarily jump to the module, emit the global, and then
        // return to the function body exactly where we were.
        {
            ::mlir::OpBuilder::InsertionGuard guard(builder_);
            builder_.setInsertionPointToStart(module_.getBody());
            if (!module_.lookupSymbol<::mlir::LLVM::GlobalOp>(symbol)) {
                builder_.create<::mlir::LLVM::GlobalOp>(
                    loc_, arrayType, true, ::mlir::LLVM::Linkage::Private,
                    symbol, builder_.getStringAttr(bytes), 0, 0);
            }
        }

        auto address = builder_.create<::mlir::LLVM::AddressOfOp>(
            loc_, ::mlir::LLVM::LLVMPointerType::get(builder_.getContext()),
            symbol);
        auto length = builder_.create<::mlir::arith::ConstantIntOp>(
            loc_, static_cast<std::int64_t>(bytes.size()), 64);
        bindString(result, StringValue{.data = address.getResult(),
                                       .length = length.getResult()});
    }

    // Lower a module-level constant use by replaying its checked initializer.
    //
    // Core v0 constants are compile-time values, not mutable storage. For the
    // first backend implementation we inline the initializer at each use site:
    // `const X: i32 = 40 + 2; return X;` lowers exactly like `return 40 + 2;`.
    // That keeps constants simple while preserving the source language rule that
    // a const has no address and cannot be assigned to.
    void lowerLoadConst(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "const load");
        const auto found = constants_.find(operation.text);
        if (found == constants_.end()) {
            throw std::logic_error("MLIR lowering loaded an unknown module constant");
        }
        const ::mlir::Value value = lowerConstInitializer(*found->second);
        bindValue(result, value);
    }

    // Lower a const initializer in its own temporary value namespace.
    //
    // Const initializer IR uses value IDs starting at %0, just like every
    // function. We therefore keep a local map for the initializer rather than
    // writing into values_, whose IDs belong to the enclosing function.
    ::mlir::Value lowerConstInitializer(const ir::Const& constant) {
        std::unordered_map<std::size_t, ::mlir::Value> constValues;
        auto lookupConstValue = [&](ir::ValueRef ref) -> ::mlir::Value {
            const auto found = constValues.find(ref.id);
            if (found == constValues.end()) {
                throw std::logic_error("const initializer used a value before definition");
            }
            return found->second;
        };

        for (const ir::Operation& op : constant.initializer.operations) {
            switch (op.kind) {
            case ir::Operation::Kind::IntegerLiteral: {
                const ir::ValueRef literal = requiredValue(op.result, "const integer");
                const unsigned width = integerBitWidth(literal.type);
                const ::mlir::Type type = mlirType(builder_, literal.type);
                auto value = builder_.create<::mlir::arith::ConstantOp>(
                    loc_, type,
                    builder_.getIntegerAttr(type,
                                            parseIntegerLiteralApInt(op.text, width)));
                constValues.emplace(literal.id, value.getResult());
                break;
            }
            case ir::Operation::Kind::BoolLiteral: {
                const ir::ValueRef literal = requiredValue(op.result, "const bool");
                auto value = builder_.create<::mlir::arith::ConstantIntOp>(
                    loc_, op.boolValue ? 1 : 0, 1);
                constValues.emplace(literal.id, value.getResult());
                break;
            }
            case ir::Operation::Kind::Unary: {
                const ir::ValueRef result = requiredValue(op.result, "const unary");
                const ir::ValueRef operandRef = requiredValue(op.value, "const unary operand");
                const ::mlir::Value operand = lookupConstValue(operandRef);
                if (op.op == TokenKind::Minus) {
                    auto zero = builder_.create<::mlir::arith::ConstantIntOp>(
                        loc_, 0, integerBitWidth(operandRef.type));
                    auto value = builder_.create<::mlir::arith::SubIOp>(
                        loc_, zero.getResult(), operand);
                    constValues.emplace(result.id, value.getResult());
                } else if (op.op == TokenKind::Bang) {
                    const unsigned width = operandRef.type.kind == BuiltinTypeKind::Bool
                                               ? 1
                                               : integerBitWidth(operandRef.type);
                    auto zero = builder_.create<::mlir::arith::ConstantIntOp>(loc_, 0, width);
                    auto value = builder_.create<::mlir::arith::CmpIOp>(
                        loc_, ::mlir::arith::CmpIPredicate::eq, operand, zero.getResult());
                    constValues.emplace(result.id, value.getResult());
                } else {
                    throw std::logic_error("unsupported unary operator in const lowering");
                }
                break;
            }
            case ir::Operation::Kind::Binary: {
                const ir::ValueRef result = requiredValue(op.result, "const binary");
                const ir::ValueRef leftRef = requiredValue(op.left, "const binary left");
                const ::mlir::Value left = lookupConstValue(leftRef);
                const ::mlir::Value right =
                    lookupConstValue(requiredValue(op.right, "const binary right"));
                ::mlir::Value value;
                switch (op.op) {
                case TokenKind::Plus:
                    value = builder_.create<::mlir::arith::AddIOp>(loc_, left, right);
                    break;
                case TokenKind::Minus:
                    value = builder_.create<::mlir::arith::SubIOp>(loc_, left, right);
                    break;
                case TokenKind::Star:
                    value = builder_.create<::mlir::arith::MulIOp>(loc_, left, right);
                    break;
                case TokenKind::Slash:
                    value = isUnsignedInteger(leftRef.type)
                                ? builder_.create<::mlir::arith::DivUIOp>(loc_, left, right)
                                      .getResult()
                                : builder_.create<::mlir::arith::DivSIOp>(loc_, left, right)
                                      .getResult();
                    break;
                case TokenKind::Percent:
                    value = isUnsignedInteger(leftRef.type)
                                ? builder_.create<::mlir::arith::RemUIOp>(loc_, left, right)
                                      .getResult()
                                : builder_.create<::mlir::arith::RemSIOp>(loc_, left, right)
                                      .getResult();
                    break;
                case TokenKind::EqualEqual:
                case TokenKind::BangEqual:
                case TokenKind::Less:
                case TokenKind::LessEqual:
                case TokenKind::Greater:
                case TokenKind::GreaterEqual:
                    value = builder_.create<::mlir::arith::CmpIOp>(
                        loc_, comparisonPredicate(op.op, isUnsignedInteger(leftRef.type)),
                        left, right);
                    break;
                default:
                    throw std::logic_error("unsupported binary operator in const lowering");
                }
                constValues.emplace(result.id, value);
                break;
            }
            case ir::Operation::Kind::LoadConst: {
                const ir::ValueRef result = requiredValue(op.result, "nested const load");
                const auto nested = constants_.find(op.text);
                if (nested == constants_.end()) {
                    throw std::logic_error("const initializer loaded an unknown const");
                }
                constValues.emplace(result.id, lowerConstInitializer(*nested->second));
                break;
            }
            default:
                throw std::logic_error("unsupported operation in module constant lowering");
            }
        }

        const ir::ValueRef init =
            requiredValue(constant.initializer.terminator.value, "const initializer");
        return lookupConstValue(init);
    }

    // Lower a local read.
    //
    // There are two local representations in this first lowering:
    //
    // - parameters are direct MLIR SSA block arguments
    // - `let` / `let mut` bindings are stack-like memref slots
    //
    // Reading a parameter is only an alias. Reading a local slot emits
    // `memref.load`, producing a fresh SSA value for the loaded contents.
    void lowerLoadLocal(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "local load");
        if (const auto direct = directLocals_.find(operation.local.id);
            direct != directLocals_.end()) {
            // A parameter load does not need a new MLIR operation. It simply gives the
            // typed IR result ID another name for the existing block argument.
            bindValue(result, direct->second);
            return;
        }

        const auto slot = localSlots_.find(operation.local.id);
        if (slot == localSlots_.end()) {
            throw std::logic_error("MLIR lowering loaded an unknown local slot");
        }

        if (result.type.isFixedArray()) {
            bindValue(result, slot->second);
            return;
        }

        auto load = builder_.create<::mlir::memref::LoadOp>(loc_, slot->second);
        bindValue(result, load.getResult());
    }

    // Lower a `let` or `let mut` declaration to a stack-like memref slot.
    //
    // This is the simplest correct lowering for mutable Core v0 locals: allocate
    // one zero-dimensional memref for the local, then store the initializer into
    // it. Later reads become memref.load and assignments become memref.store.
    void lowerDeclareLocal(const ir::Operation& operation) {
        const ir::ValueRef init = requiredValue(operation.value, "local initializer");
        const ::mlir::Value initValue = lookupValue(init);

        if (init.type.isFixedArray()) {
            const ::mlir::MemRefType slotType = rankedArrayMemRefType(builder_, init.type);
            auto slot = builder_.create<::mlir::memref::AllocaOp>(loc_, slotType);

            const auto [_, inserted] =
                localSlots_.emplace(operation.local.id, slot.getResult());
            if (!inserted) {
                throw std::logic_error("MLIR lowering declared a local slot twice");
            }

            copyRankedMemRef(builder_, loc_, initValue, slot.getResult(), init.type);
            return;
        }

        const ::mlir::MemRefType slotType =
            ::mlir::MemRefType::get({}, mlirType(builder_, init.type));
        auto slot = builder_.create<::mlir::memref::AllocaOp>(loc_, slotType);

        const auto [__, insertedScalar] =
            localSlots_.emplace(operation.local.id, slot.getResult());
        if (!insertedScalar) {
            throw std::logic_error("MLIR lowering declared a local slot twice");
        }

        builder_.create<::mlir::memref::StoreOp>(loc_, initValue, slot.getResult());
    }

    void lowerArrayLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "array literal");
        auto slot = builder_.create<::mlir::memref::AllocaOp>(
            loc_, rankedArrayMemRefType(builder_, result.type));

        for (std::size_t i = 0; i < operation.arguments.size(); ++i) {
            auto idx =
                builder_.create<::mlir::arith::ConstantIndexOp>(loc_, static_cast<int64_t>(i));
            const ::mlir::Value elem = lookupValue(operation.arguments[i]);
            builder_.create<::mlir::memref::StoreOp>(loc_, elem, slot.getResult(),
                                                       idx.getResult());
        }

        bindValue(result, slot.getResult());
    }

    void lowerIndexLoad(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "index load");
        const ir::ValueRef base =
            requiredValue(operation.left, "indexed load base");
        const ir::ValueRef index =
            requiredValue(operation.right, "indexed load index");

        const ::mlir::Value idx =
            memrefIndexFromValue(builder_, loc_, lookupValue(index));
        auto loaded = builder_.create<::mlir::memref::LoadOp>(
            loc_, lookupValue(base), idx);
        bindValue(result, loaded.getResult());
    }

    void lowerIndexStore(const ir::Operation& operation) {
        const ir::ValueRef stored =
            requiredValue(operation.value, "indexed store value");
        const ir::ValueRef base =
            requiredValue(operation.left, "indexed store base");
        const ir::ValueRef index =
            requiredValue(operation.right, "indexed store index");

        const ::mlir::Value idx =
            memrefIndexFromValue(builder_, loc_, lookupValue(index));
        builder_.create<::mlir::memref::StoreOp>(loc_, lookupValue(stored),
                                                  lookupValue(base), idx);
    }

    // Lower assignment to an existing local slot.
    //
    // Semantic analysis has already checked that the source binding is mutable
    // and that the assigned value has the local's type. The MLIR lowering only
    // needs to find the allocated slot and store the new SSA value into it.
    void lowerStoreLocal(const ir::Operation& operation) {
        const ir::ValueRef stored = requiredValue(operation.value, "local store value");
        const auto slot = localSlots_.find(operation.local.id);
        if (slot == localSlots_.end()) {
            throw std::logic_error("MLIR lowering stored to an unknown local slot");
        }
        builder_.create<::mlir::memref::StoreOp>(loc_, lookupValue(stored),
                                                 slot->second);
    }

    // Lower a unary operation.
    //
    // The current MLIR slice only supports logical not. It is emitted as a
    // comparison against zero/false, which is simple and works for both bool
    // (`i1`) and the current integer type (`i32`).
    void lowerUnary(const ir::Operation& operation) {
        if (operation.op != TokenKind::Bang) {
            throw std::logic_error("MLIR lowering only supports unary ! for now");
        }

        const ir::ValueRef result = requiredValue(operation.result, "unary operation");
        const ir::ValueRef operandRef = requiredValue(operation.value, "unary operand");
        const ::mlir::Value operand =
            lookupValue(operandRef);
        unsigned width = 0;
        if (operandRef.type.kind == BuiltinTypeKind::Bool) {
            width = 1;
        } else if (operandRef.type.isInteger()) {
            width = integerBitWidth(operandRef.type);
        } else {
            throw std::logic_error("MLIR lowering only supports ! for bool and integers");
        }
        auto falseValue = builder_.create<::mlir::arith::ConstantIntOp>(loc_, 0, width);
        auto lowered = builder_.create<::mlir::arith::CmpIOp>(
            loc_, ::mlir::arith::CmpIPredicate::eq, operand, falseValue);
        bindValue(result, lowered.getResult());
    }

    // Lower a binary expression operation.
    //
    // Arithmetic becomes the matching `arith.*i` operation. Equality and
    // relational comparisons become `arith.cmpi`, producing an MLIR `i1`.
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
        case TokenKind::Slash:
            if (isUnsignedInteger(requiredValue(operation.left, "binary left operand").type)) {
                lowered = builder_.create<::mlir::arith::DivUIOp>(loc_, left, right);
            } else {
                lowered = builder_.create<::mlir::arith::DivSIOp>(loc_, left, right);
            }
            break;
        case TokenKind::Percent:
            if (isUnsignedInteger(requiredValue(operation.left, "binary left operand").type)) {
                lowered = builder_.create<::mlir::arith::RemUIOp>(loc_, left, right);
            } else {
                lowered = builder_.create<::mlir::arith::RemSIOp>(loc_, left, right);
            }
            break;
        case TokenKind::AmpAmp:
            // Core v0 currently lowers logical operators as eager boolean
            // operations. The language reference documents this explicitly so
            // nobody expects C-style short-circuiting until the IR grows
            // condition blocks for the right-hand side.
            lowered = builder_.create<::mlir::arith::AndIOp>(loc_, left, right);
            break;
        case TokenKind::PipePipe:
            lowered = builder_.create<::mlir::arith::OrIOp>(loc_, left, right);
            break;
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            lowered = builder_.create<::mlir::arith::CmpIOp>(
                loc_,
                comparisonPredicate(
                    operation.op,
                    isUnsignedInteger(requiredValue(operation.left, "binary left operand").type)),
                left, right);
            break;
        default:
            throw std::logic_error("MLIR lowering only supports arithmetic and comparison operators for now");
        }

        bindValue(result, lowered);
    }

    // Lower a direct user-function call to `func.call`.
    //
    // Built-ins such as print/println are intentionally rejected here until a
    // runtime ABI exists. A call with no result is also possible in the IR, but
    // this current lowering only handles value-returning user calls.
    void lowerCall(const ir::Operation& operation) {
        if (operation.isBuiltin) {
            lowerBuiltinCall(operation);
            return;
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

    // Lower Core v0 printing built-ins to bootstrap runtime calls.
    //
    // `print` / `println` use Rust-style `"…{}…"` format strings (first argument
    // must be a string literal). Segments lower to `nex_runtime_print_str`; typed
    // holes lower to small `nex_runtime_print_*` helpers.
    StringValue emitGlobalFormatBytes(const std::string& bytes) {
        const std::string symbol =
            "__nex_fmt_" + function_.name + "_" + std::to_string(formatLiteralCounter_++);
        const ::mlir::Type i8 = builder_.getI8Type();
        const ::mlir::Type arrayType =
            ::mlir::LLVM::LLVMArrayType::get(i8, static_cast<unsigned>(bytes.size()));

        {
            ::mlir::OpBuilder::InsertionGuard guard(builder_);
            builder_.setInsertionPointToStart(module_.getBody());
            if (!module_.lookupSymbol<::mlir::LLVM::GlobalOp>(symbol)) {
                builder_.create<::mlir::LLVM::GlobalOp>(
                    loc_, arrayType, true, ::mlir::LLVM::Linkage::Private,
                    symbol, builder_.getStringAttr(bytes), 0, 0);
            }
        }

        auto address = builder_.create<::mlir::LLVM::AddressOfOp>(
            loc_, ::mlir::LLVM::LLVMPointerType::get(builder_.getContext()),
            symbol);
        auto length = builder_.create<::mlir::arith::ConstantIntOp>(
            loc_, static_cast<std::int64_t>(bytes.size()), 64);
        return StringValue{.data = address.getResult(), .length = length.getResult()};
    }

    void emitRuntimePrintStr(const StringValue& string) {
        ensureRuntimePrintDeclaration("nex_runtime_print_str");
        builder_.create<::mlir::func::CallOp>(
            loc_, "nex_runtime_print_str", ::mlir::TypeRange{},
            ::mlir::ValueRange{string.data, string.length});
    }

    void ensureRuntimePrintI64Declaration() {
        ensureRuntimeFunctionDeclaration("nex_runtime_print_i64", {builder_.getI64Type()},
                                         {});
    }

    void ensureRuntimePrintU64Declaration() {
        ensureRuntimeFunctionDeclaration("nex_runtime_print_u64", {builder_.getI64Type()},
                                         {});
    }

    void ensureRuntimePrintBoolDeclaration() {
        ensureRuntimeFunctionDeclaration("nex_runtime_print_bool", {builder_.getI1Type()},
                                         {});
    }

    ::mlir::Value widenIntegerArgumentToI64(ir::ValueRef ref) {
        ::mlir::Value value = lookupValue(ref);
        const ir::Type type = ref.type;
        const unsigned width = integerBitWidth(type);
        if (width == 0 || width > 64) {
            throw std::logic_error("integer format widening expects a fixed-width integer");
        }
        const auto i64Ty = builder_.getI64Type();
        if (width == 64) {
            return value;
        }
        if (isUnsignedInteger(type)) {
            return builder_.create<::mlir::arith::ExtUIOp>(loc_, i64Ty, value).getResult();
        }
        return builder_.create<::mlir::arith::ExtSIOp>(loc_, i64Ty, value).getResult();
    }

    void lowerOneFormatArgument(ir::ValueRef ref) {
        const ir::Type type = ref.type;
        if (type.kind == BuiltinTypeKind::Bool) {
            ensureRuntimePrintBoolDeclaration();
            builder_.create<::mlir::func::CallOp>(
                loc_, "nex_runtime_print_bool", ::mlir::TypeRange{},
                ::mlir::ValueRange{lookupValue(ref)});
            return;
        }
        if (type.kind == BuiltinTypeKind::Str) {
            emitRuntimePrintStr(lookupString(ref));
            return;
        }
        if (type.isInteger()) {
            ::mlir::Value wide = widenIntegerArgumentToI64(ref);
            if (isUnsignedInteger(type)) {
                ensureRuntimePrintU64Declaration();
                builder_.create<::mlir::func::CallOp>(
                    loc_, "nex_runtime_print_u64", ::mlir::TypeRange{},
                    ::mlir::ValueRange{wide});
            } else {
                ensureRuntimePrintI64Declaration();
                builder_.create<::mlir::func::CallOp>(
                    loc_, "nex_runtime_print_i64", ::mlir::TypeRange{},
                    ::mlir::ValueRange{wide});
            }
            return;
        }

        throw std::logic_error("unsupported format argument type in MLIR lowering");
    }

    void lowerFormattedPrintBuiltin(const ir::Operation& operation, bool newlineAtEnd) {
        if (operation.arguments.empty()) {
            throw std::logic_error("formatted print expects at least a format string");
        }

        const std::optional<std::string> rawFmt =
            findStringLiteralRaw(function_, operation.arguments[0].id);
        if (!rawFmt) {
            // Dynamic `str` with no placeholders (`println(readln())`): single
            // runtime print of pointer+length.
            if (operation.arguments.size() != 1) {
                throw std::logic_error(
                    "formatted print with multiple arguments requires a string literal "
                    "format operand");
            }
            emitRuntimePrintStr(lookupString(operation.arguments[0]));
            if (newlineAtEnd) {
                emitRuntimePrintStr(emitGlobalFormatBytes(std::string("\n")));
            }
            return;
        }

        const std::string decoded = decodeStringLiteralOrThrow(*rawFmt);
        std::vector<std::string> literals;
        std::string splitErr;
        if (!splitFormatString(decoded, literals, splitErr)) {
            throw std::logic_error(splitErr);
        }

        const std::size_t holes = literals.size() - 1;
        if (operation.arguments.size() != 1 + holes) {
            throw std::logic_error("internal error: format arity mismatch at lowering");
        }

        for (std::size_t i = 0; i < holes; ++i) {
            if (!literals[i].empty()) {
                emitRuntimePrintStr(emitGlobalFormatBytes(literals[i]));
            }
            lowerOneFormatArgument(operation.arguments[i + 1]);
        }

        if (!literals.empty() && !literals[holes].empty()) {
            emitRuntimePrintStr(emitGlobalFormatBytes(literals[holes]));
        }

        if (newlineAtEnd) {
            emitRuntimePrintStr(emitGlobalFormatBytes(std::string("\n")));
        }
    }

    void lowerBuiltinCall(const ir::Operation& operation) {
        if (operation.text == "readln") {
            lowerReadlnBuiltin(operation);
            return;
        }
        if (operation.text == "input_ok") {
            lowerInputOkBuiltin(operation);
            return;
        }
        if (operation.text == "parse_i32" || operation.text == "parse_u64" ||
            operation.text == "parse_bool") {
            lowerParseBuiltin(operation);
            return;
        }

        if (operation.text == "print" || operation.text == "println") {
            lowerFormattedPrintBuiltin(operation, operation.text == "println");
            return;
        }

        throw std::logic_error("unknown builtin in MLIR lowering");
    }

    // Lower `readln() -> str` to the tiny stdin runtime bridge.
    //
    // The runtime currently stores the last-read line in a process-global scratch
    // buffer. The compiler asks for the data pointer and length as two calls, then
    // binds the single typed IR `str` result to that pointer/length pair. This is
    // intentionally a first input slice, not the final owned-string design.
    void lowerReadlnBuiltin(const ir::Operation& operation) {
        if (!operation.result || !operation.arguments.empty()) {
            throw std::logic_error("readln lowering expected no arguments and one str result");
        }

        ensureRuntimeReadlnDeclarations();
        auto data = builder_.create<::mlir::func::CallOp>(
            loc_, "nex_runtime_readln_data",
            ::mlir::TypeRange{::mlir::LLVM::LLVMPointerType::get(builder_.getContext())},
            ::mlir::ValueRange{});
        auto length = builder_.create<::mlir::func::CallOp>(
            loc_, "nex_runtime_readln_len",
            ::mlir::TypeRange{builder_.getI64Type()},
            ::mlir::ValueRange{});
        bindString(*operation.result, StringValue{.data = data.getResult(0),
                                                  .length = length.getResult(0)});
    }

    // Lower parse built-ins that explicitly convert a string to a scalar type.
    //
    // This is intentionally explicit conversion (read string, then parse) rather
    // than implicit typed input. That keeps parsing behavior visible and avoids
    // scanf-style hidden tokenization rules in the first slice.
    void lowerParseBuiltin(const ir::Operation& operation) {
        if (!operation.result || operation.arguments.size() != 1) {
            throw std::logic_error("parse built-ins expect one str argument and one scalar result");
        }
        const StringValue input = lookupString(operation.arguments[0]);
        ensureRuntimeParseDeclarations();

        const std::string runtimeName =
            operation.text == "parse_i32"   ? "nex_runtime_parse_i32"
            : operation.text == "parse_u64" ? "nex_runtime_parse_u64"
                                            : "nex_runtime_parse_bool";
        auto call = builder_.create<::mlir::func::CallOp>(
            loc_, runtimeName, ::mlir::TypeRange{mlirType(builder_, operation.result->type)},
            ::mlir::ValueRange{input.data, input.length});
        bindValue(*operation.result, call.getResult(0));
    }

    // Lower `input_ok() -> bool` that reports success of last runtime input/parse op.
    //
    // Until the language has first-class Result types, this gives users an
    // explicit, checkable success bit for fallible input and parse operations.
    void lowerInputOkBuiltin(const ir::Operation& operation) {
        if (!operation.result || !operation.arguments.empty()) {
            throw std::logic_error("input_ok lowering expects no arguments and bool result");
        }
        ensureRuntimeParseDeclarations();
        auto call = builder_.create<::mlir::func::CallOp>(
            loc_, "nex_runtime_last_ok_flag",
            ::mlir::TypeRange{builder_.getI1Type()}, ::mlir::ValueRange{});
        bindValue(*operation.result, call.getResult(0));
    }

    // Lower a structured if statement to `scf.if`.
    //
    // There are two useful shapes in Core v0 today:
    //
    // - returning if/else: both branches end the function with `return`
    // - fallthrough if/else: branches perform side effects, then execution
    //   continues after the `if`
    //
    // Returning if/else is lowered as a value-producing `scf.if` followed by a
    // function return. Fallthrough if/else is lowered as a no-result `scf.if`
    // whose regions end with `scf.yield`.

    // Tracks how a fallthrough region ended so `lowerWhile` can append the right
    // tail (`for` step + `scf.yield`, yield only, or nothing).
    enum class FallthroughRegionEnd {
        Complete,
        // `break` / `continue` at this nesting level already finished the while
        // iteration (including `continue` running the `for` step when present).
        IterationClosed,
        // Loop control ran inside a nested `scf.if` branch: that branch yielded,
        // but the surrounding while body still needs the `for` step (if any) and a
        // final `scf.yield`.
        NestedLoopControlNeedStepAndYield,
    };

    static bool ifIsFallthroughShape(const ir::Operation& operation) {
        if (operation.kind != ir::Operation::Kind::If || !operation.thenBlock) {
            return false;
        }
        if (operation.elseBlock) {
            return operation.thenBlock->terminator.kind == ir::Terminator::Kind::None &&
                   operation.elseBlock->terminator.kind == ir::Terminator::Kind::None;
        }
        return operation.thenBlock->terminator.kind == ir::Terminator::Kind::None;
    }

    bool lowerIf(const ir::Operation& operation) {
        // Optional `else` is only lowered through the fallthrough `scf.if` path in
        // this slice (see `lowerFallthroughIf`).
        if (!operation.elseBlock) {
            if (lowerFallthroughIf(operation) != FallthroughRegionEnd::Complete) {
                throw std::logic_error(
                    "`break`/`continue` escaped a loop body in an invalid context");
            }
            return false;
        }

        if (ifIsFallthroughShape(operation)) {
            if (lowerFallthroughIf(operation) != FallthroughRegionEnd::Complete) {
                throw std::logic_error(
                    "`break`/`continue` escaped a loop body in an invalid context");
            }
            return false;
        }

        if (!operation.thenBlock) {
            throw std::logic_error("`if` operation is missing its then branch");
        }

        const ir::Terminator& thenTerminator = operation.thenBlock->terminator;
        const ir::Terminator& elseTerminator = operation.elseBlock->terminator;

        if (thenTerminator.kind != elseTerminator.kind) {
            throw std::logic_error("MLIR lowering requires matching if/else terminators");
        }

        const ::mlir::Value condition =
            lookupValue(requiredValue(operation.condition, "if condition"));
        llvm::SmallVector<::mlir::Type> resultTypes;
        if (thenTerminator.kind == ir::Terminator::Kind::ReturnValue) {
            const ir::ValueRef thenValue =
                requiredValue(thenTerminator.value, "then return");
            resultTypes.push_back(mlirType(builder_, thenValue.type));
        } else if (thenTerminator.kind != ir::Terminator::Kind::Return) {
            throw std::logic_error("MLIR lowering only supports returning if/else branches for now");
        }

        auto ifOp = builder_.create<::mlir::scf::IfOp>(loc_, resultTypes, condition,
                                                       true);

        builder_.setInsertionPointToStart(&ifOp.getThenRegion().front());
        lowerYieldingReturnBlock(*operation.thenBlock);

        builder_.setInsertionPointToStart(&ifOp.getElseRegion().front());
        lowerYieldingReturnBlock(*operation.elseBlock);

        builder_.setInsertionPointAfter(ifOp);
        if (thenTerminator.kind == ir::Terminator::Kind::ReturnValue) {
            builder_.create<::mlir::func::ReturnOp>(loc_, ifOp.getResult(0));
        } else {
            lowerVoidReturn();
        }
        return true;
    }

    // Lower an if statement that falls through after executing branch effects.
    //
    // This is the shape used inside loops by the walkthrough example. The
    // branches do not produce a value for the surrounding expression and they do
    // not return from the function; they simply run stores/calls/etc. and then
    // yield control back to the code after the `scf.if`.
    FallthroughRegionEnd lowerFallthroughIf(const ir::Operation& operation) {
        const ::mlir::Value condition =
            lookupValue(requiredValue(operation.condition, "if condition"));
        auto ifOp = builder_.create<::mlir::scf::IfOp>(
            loc_, ::mlir::TypeRange{}, condition, operation.elseBlock != nullptr);

        auto erasePlaceholderYield = [](::mlir::Block& block) {
            if (block.empty()) {
                return;
            }
            auto yield = ::llvm::dyn_cast<::mlir::scf::YieldOp>(block.getTerminator());
            if (yield && yield.getNumOperands() == 0) {
                yield.erase();
            }
        };

        ::mlir::Block& thenBlock = ifOp.getThenRegion().front();
        erasePlaceholderYield(thenBlock);
        builder_.setInsertionPointToStart(&thenBlock);
        const FallthroughRegionEnd thenEnd = lowerFallthroughRegionBlock(
            *operation.thenBlock, "if then branch", /*propagateLoopExit=*/false);
        if (thenEnd == FallthroughRegionEnd::Complete) {
            builder_.setInsertionPointToEnd(&thenBlock);
            builder_.create<::mlir::scf::YieldOp>(loc_);
        }

        FallthroughRegionEnd elseEnd = FallthroughRegionEnd::Complete;
        if (operation.elseBlock) {
            ::mlir::Block& elseBlock = ifOp.getElseRegion().front();
            erasePlaceholderYield(elseBlock);
            builder_.setInsertionPointToStart(&elseBlock);
            elseEnd = lowerFallthroughRegionBlock(
                *operation.elseBlock, "if else branch", /*propagateLoopExit=*/false);
            if (elseEnd == FallthroughRegionEnd::Complete) {
                builder_.setInsertionPointToEnd(&elseBlock);
                builder_.create<::mlir::scf::YieldOp>(loc_);
            }
        }

        builder_.setInsertionPointAfter(ifOp);
        const int nonComplete = (thenEnd != FallthroughRegionEnd::Complete ? 1 : 0) +
                                (elseEnd != FallthroughRegionEnd::Complete ? 1 : 0);
        if (nonComplete > 1) {
            throw std::logic_error(
                "both branches of `if` ended with loop control in this MLIR slice");
        }
        if (thenEnd != FallthroughRegionEnd::Complete) {
            return thenEnd;
        }
        return elseEnd;
    }

    // Lower a Core v0 while loop to MLIR `scf.while`.
    //
    // Because local variables currently live in memref slots, the loop does not
    // need loop-carried SSA values yet. The condition region recomputes the
    // condition and ends with `scf.condition`; the body region performs effects
    // and ends with `scf.yield` to jump back to the condition region.
    void lowerWhile(const ir::Operation& operation) {
        if (!operation.conditionBlock || !operation.bodyBlock) {
            throw std::logic_error("while operation is missing its condition or body block");
        }

        const ::mlir::MemRefType exitTy =
            ::mlir::MemRefType::get({}, builder_.getI1Type());
        auto exitAlloc = builder_.create<::mlir::memref::AllocaOp>(loc_, exitTy);
        const ::mlir::Value exitSlot = exitAlloc.getResult();
        auto breakClear = builder_.create<::mlir::arith::ConstantIntOp>(loc_, 0, 1);
        builder_.create<::mlir::memref::StoreOp>(loc_, breakClear.getResult(), exitSlot,
                                                 ::mlir::ValueRange{});

        activeLoops_.push_back(ActiveLoop{
            .breakExitSlot = exitSlot,
            .stepBlock = operation.stepBlock ? operation.stepBlock.get() : nullptr,
        });

        auto whileOp = builder_.create<::mlir::scf::WhileOp>(
            loc_, ::mlir::TypeRange{}, ::mlir::ValueRange{},
            [&](::mlir::OpBuilder& nestedBuilder, ::mlir::Location,
                ::mlir::ValueRange) {
                // MLIR invokes this callback with the insertion point already
                // inside the "before" region. The guard restores the outer
                // insertion point after we finish filling the region.
                ::mlir::OpBuilder::InsertionGuard guard(builder_);
                builder_.setInsertionPoint(nestedBuilder.getInsertionBlock(),
                                           nestedBuilder.getInsertionPoint());
                lowerConditionBlock(*operation.conditionBlock, exitSlot);
            },
            [&](::mlir::OpBuilder& nestedBuilder, ::mlir::Location,
                ::mlir::ValueRange) {
                // The "after" region is the loop body for a normal while loop.
                // It must yield back to the condition even though this v0 slice
                // has no loop-carried values to pass.
                ::mlir::OpBuilder::InsertionGuard guard(builder_);
                ::mlir::Block* const loopBodyBlock = nestedBuilder.getInsertionBlock();
                builder_.setInsertionPoint(loopBodyBlock,
                                           nestedBuilder.getInsertionPoint());
                const FallthroughRegionEnd bodyEnd =
                    lowerFallthroughRegionBlock(*operation.bodyBlock, "while body");
                auto appendWhileBodyYield = [&]() {
                    builder_.setInsertionPointToEnd(loopBodyBlock);
                    builder_.create<::mlir::scf::YieldOp>(loc_);
                };
                switch (bodyEnd) {
                case FallthroughRegionEnd::Complete:
                    if (operation.stepBlock) {
                        lowerFallthroughRegionBlock(*operation.stepBlock, "for step");
                    }
                    appendWhileBodyYield();
                    break;
                case FallthroughRegionEnd::IterationClosed:
                    break;
                case FallthroughRegionEnd::NestedLoopControlNeedStepAndYield:
                    if (operation.stepBlock) {
                        lowerFallthroughRegionBlock(*operation.stepBlock, "for step");
                    }
                    appendWhileBodyYield();
                    break;
                }
            });

        activeLoops_.pop_back();

        builder_.setInsertionPointAfter(whileOp);
    }

    // Lower an if/else branch block that will yield back to `scf.if`.
    //
    // Source-level `return value;` inside the branch cannot be emitted as
    // `func.return` here because that would return from the function before the
    // `scf.if` operation is complete. Instead, the branch yields its value to the
    // enclosing `scf.if`.
    void lowerYieldingReturnBlock(const ir::Block& block) {
        for (const ir::Operation& operation : block.operations) {
            if (lowerOperation(operation)) {
                throw std::logic_error("nested if/else returns are not lowered yet");
            }
        }

        switch (block.terminator.kind) {
        case ir::Terminator::Kind::Return:
            builder_.create<::mlir::scf::YieldOp>(loc_);
            return;
        case ir::Terminator::Kind::ReturnValue: {
            const ir::ValueRef value =
                requiredValue(block.terminator.value, "if/else return");
            builder_.create<::mlir::scf::YieldOp>(loc_, lookupValue(value));
            return;
        }
        default:
            throw std::logic_error("if/else branch must end in a return for this MLIR slice");
        }
    }

    // Lower a nested structured region whose block must fall through.
    //
    // `scf.if` and `scf.while` regions are not the top-level function body. For
    // this first lowering, a nested block may perform operations and then fall
    // through, but it may not directly emit a function return.
    FallthroughRegionEnd lowerFallthroughRegionBlock(
        const ir::Block& block,
        std::string_view context,
        bool propagateLoopExit = true) {
        for (const ir::Operation& operation : block.operations) {
            if (operation.kind == ir::Operation::Kind::If &&
                ifIsFallthroughShape(operation)) {
                const FallthroughRegionEnd branchEnd = lowerFallthroughIf(operation);
                if (branchEnd != FallthroughRegionEnd::Complete) {
                    return branchEnd;
                }
                continue;
            }
            if (operation.kind == ir::Operation::Kind::Break) {
                if (activeLoops_.empty()) {
                    throw std::logic_error(std::string(context) +
                                           " contains `break` outside any loop");
                }
                const ::mlir::Value slot = activeLoops_.back().breakExitSlot;
                auto flag =
                    builder_.create<::mlir::arith::ConstantIntOp>(loc_, 1, 1);
                builder_.create<::mlir::memref::StoreOp>(loc_, flag.getResult(), slot,
                                                         ::mlir::ValueRange{});
                builder_.create<::mlir::scf::YieldOp>(loc_);
                return propagateLoopExit ? FallthroughRegionEnd::IterationClosed
                                         : FallthroughRegionEnd::NestedLoopControlNeedStepAndYield;
            }
            if (operation.kind == ir::Operation::Kind::Continue) {
                if (activeLoops_.empty()) {
                    throw std::logic_error(std::string(context) +
                                           " contains `continue` outside any loop");
                }
                const ActiveLoop& loop = activeLoops_.back();
                if (propagateLoopExit && loop.stepBlock) {
                    if (lowerFallthroughRegionBlock(*loop.stepBlock,
                                                    "for step on continue") !=
                        FallthroughRegionEnd::Complete) {
                        throw std::logic_error(
                            "`continue` step block must complete without nested loop control");
                    }
                }
                builder_.create<::mlir::scf::YieldOp>(loc_);
                if (propagateLoopExit) {
                    return FallthroughRegionEnd::IterationClosed;
                }
                return FallthroughRegionEnd::NestedLoopControlNeedStepAndYield;
            }
            if (lowerOperation(operation)) {
                throw std::logic_error(std::string(context) +
                                       " cannot contain a function return in this MLIR slice");
            }
        }
        if (block.terminator.kind != ir::Terminator::Kind::None) {
            throw std::logic_error(std::string(context) +
                                   " must fall through in this MLIR slice");
        }
        return FallthroughRegionEnd::Complete;
    }

    // Lower the condition block of a structured while loop.
    //
    // The typed IR condition block is ordinary expression code followed by a
    // special `ConditionValue` terminator. MLIR spells that terminator as
    // `scf.condition`, whose first operand decides whether the loop body runs.
    void lowerConditionBlock(const ir::Block& block,
                             std::optional<::mlir::Value> breakExitSlot = std::nullopt) {
        for (const ir::Operation& operation : block.operations) {
            if (lowerOperation(operation)) {
                throw std::logic_error("while condition cannot return from the function");
            }
        }
        if (block.terminator.kind != ir::Terminator::Kind::ConditionValue) {
            throw std::logic_error("while condition block must end with ConditionValue");
        }

        ::mlir::Value condition =
            lookupValue(requiredValue(block.terminator.value, "while condition"));
        if (breakExitSlot) {
            auto one = builder_.create<::mlir::arith::ConstantIntOp>(loc_, 1, 1);
            auto brk = builder_.create<::mlir::memref::LoadOp>(loc_, *breakExitSlot,
                                                               ::mlir::ValueRange{});
            auto notBrk =
                builder_.create<::mlir::arith::XOrIOp>(loc_, brk, one.getResult());
            condition =
                builder_.create<::mlir::arith::AndIOp>(loc_, condition, notBrk);
        }
        builder_.create<::mlir::scf::ConditionOp>(loc_, condition,
                                                  ::mlir::ValueRange{});
    }

    // Lower a block terminator in normal function-body context.
    //
    // Return terminators become `func.return`. A missing terminator means the
    // block falls through, which is currently only okay for void functions where
    // lower() will synthesize a final return.
    bool lowerTerminator(const ir::Terminator& terminator) {
        switch (terminator.kind) {
        case ir::Terminator::Kind::Return:
            lowerVoidReturn();
            return true;
        case ir::Terminator::Kind::ReturnValue: {
            const ::mlir::Value value =
                lookupValue(requiredValue(terminator.value, "return terminator"));
            builder_.create<::mlir::func::ReturnOp>(loc_, value);
            return true;
        }
        case ir::Terminator::Kind::None:
            return false;
        case ir::Terminator::Kind::ConditionValue:
        case ir::Terminator::Kind::InitValue:
            throw std::logic_error("MLIR lowering expected a function return terminator");
        }
        throw std::logic_error("unknown terminator kind during MLIR lowering");
    }

    // Associate a typed IR temporary with the concrete MLIR SSA value that
    // implements it.
    //
    // This is the key bridge between the IR and MLIR worlds: later operations
    // mention `%2` as a ValueRef, while MLIR APIs require the actual Value handle.
    void bindValue(ir::ValueRef ref, ::mlir::Value value) {
        const auto [_, inserted] = values_.emplace(ref.id, value);
        if (!inserted) {
            throw std::logic_error("typed IR value was defined more than once during MLIR lowering");
        }
    }

    // Emit the backend form of a source-level `return;`.
    //
    // Nex allows `fn main() -> void`, but the platform executable entry point is
    // the C/LLVM symbol `main`, whose useful convention is returning an integer
    // process status. For native output, a void Nex main therefore returns 0 to
    // the OS. Other void functions still lower to a plain no-value return.
    void lowerVoidReturn() {
        if (usesNativeMainResult()) {
            auto success = builder_.create<::mlir::arith::ConstantIntOp>(loc_, 0, 32);
            builder_.create<::mlir::func::ReturnOp>(loc_, success.getResult());
            return;
        }
        builder_.create<::mlir::func::ReturnOp>(loc_);
    }

    bool usesNativeMainResult() const {
        return function_.name == "main" && function_.returnType.isVoid();
    }

    // Look up the MLIR value corresponding to a previously-lowered IR temporary.
    //
    // A miss means operations were lowered out of order or an operation forgot to
    // call bindValue after creating its result.
    ::mlir::Value lookupValue(ir::ValueRef ref) const {
        const auto found = values_.find(ref.id);
        if (found == values_.end()) {
            throw std::logic_error("MLIR lowering used a typed IR value before it was defined");
        }
        return found->second;
    }

    void bindString(ir::ValueRef ref, StringValue value) {
        const auto [_, inserted] = strings_.emplace(ref.id, value);
        if (!inserted) {
            throw std::logic_error("typed IR string value was defined more than once during MLIR lowering");
        }
    }

    StringValue lookupString(ir::ValueRef ref) const {
        const auto found = strings_.find(ref.id);
        if (found == strings_.end()) {
            throw std::logic_error("MLIR lowering used a string value before it was defined");
        }
        return found->second;
    }

    void ensureRuntimePrintDeclaration(std::string_view name) {
        if (module_.lookupSymbol<::mlir::func::FuncOp>(name)) {
            return;
        }

        ::mlir::OpBuilder::InsertionGuard guard(builder_);
        builder_.setInsertionPointToStart(module_.getBody());
        const ::mlir::FunctionType runtimeType = builder_.getFunctionType(
            {::mlir::LLVM::LLVMPointerType::get(builder_.getContext()),
             builder_.getI64Type()},
            {});
        ::mlir::func::FuncOp declaration =
            builder_.create<::mlir::func::FuncOp>(loc_, name, runtimeType);
        declaration.setPrivate();
    }

    void ensureRuntimeReadlnDeclarations() {
        ensureRuntimeFunctionDeclaration(
            "nex_runtime_readln_data", {},
            {::mlir::LLVM::LLVMPointerType::get(builder_.getContext())});
        ensureRuntimeFunctionDeclaration("nex_runtime_readln_len", {},
                                         {builder_.getI64Type()});
    }

    void ensureRuntimeParseDeclarations() {
        ensureRuntimeFunctionDeclaration(
            "nex_runtime_parse_i32",
            {::mlir::LLVM::LLVMPointerType::get(builder_.getContext()),
             builder_.getI64Type()},
            {builder_.getI32Type()});
        ensureRuntimeFunctionDeclaration(
            "nex_runtime_parse_u64",
            {::mlir::LLVM::LLVMPointerType::get(builder_.getContext()),
             builder_.getI64Type()},
            {builder_.getI64Type()});
        ensureRuntimeFunctionDeclaration(
            "nex_runtime_parse_bool",
            {::mlir::LLVM::LLVMPointerType::get(builder_.getContext()),
             builder_.getI64Type()},
            {builder_.getI1Type()});
        ensureRuntimeFunctionDeclaration("nex_runtime_last_ok_flag", {},
                                         {builder_.getI1Type()});
    }

    void ensureRuntimeFunctionDeclaration(std::string_view name,
                                          ::mlir::TypeRange parameterTypes,
                                          ::mlir::TypeRange resultTypes) {
        if (module_.lookupSymbol<::mlir::func::FuncOp>(name)) {
            return;
        }

        ::mlir::OpBuilder::InsertionGuard guard(builder_);
        builder_.setInsertionPointToStart(module_.getBody());
        ::mlir::func::FuncOp declaration = builder_.create<::mlir::func::FuncOp>(
            loc_, name, builder_.getFunctionType(parameterTypes, resultTypes));
        declaration.setPrivate();
    }

    struct ActiveLoop {
        ::mlir::Value breakExitSlot{};
        const ir::Block* stepBlock = nullptr;
    };

    ::mlir::OpBuilder& builder_;
    // Module-scope operations such as LLVM globals and runtime function
    // declarations cannot reliably discover the module by walking upward from the
    // current insertion point. During structured-region construction, MLIR may
    // invoke callbacks before the in-progress `scf` operation has a complete
    // parent chain. Holding the module explicitly keeps module-scope emission
    // independent from where the builder is currently inserting.
    ::mlir::ModuleOp module_;
    const ir::Function& function_;
    const std::unordered_map<std::string, const ir::Const*>& constants_;
    ::mlir::Location loc_;
    std::unordered_map<std::size_t, ::mlir::Value> values_;
    std::unordered_map<std::size_t, StringValue> strings_;
    std::size_t formatLiteralCounter_;
    std::vector<ActiveLoop> activeLoops_{};
    // directLocals_ maps immutable parameter locals to existing MLIR block
    // arguments. No memory is needed for them in the current Core v0 slice.
    std::unordered_map<std::size_t, ::mlir::Value> directLocals_;

    // localSlots_ maps `let` / `let mut` locals to zero-dimensional memrefs. This
    // is intentionally simple and readable before we add promotion/optimization.
    std::unordered_map<std::size_t, ::mlir::Value> localSlots_;
};

#else
// Fallback equivalent of mlirType(): return the textual MLIR spelling for each
// supported nex IR type.
//
// This path is compiled when MLIR development headers/libraries are not present.
// It keeps `--dump-mlir` useful on lightweight machines, but it is intentionally
// limited to the same small slice as the real MLIR path.
std::string textualType(ir::Type type) {
    switch (type.kind) {
    case BuiltinTypeKind::Bool:
        return "i1";
    case BuiltinTypeKind::I32:
        return "i32";
    case BuiltinTypeKind::Void:
        return "";
    default:
        throw std::logic_error("textual MLIR lowering only supports bool, i32, and void in this slice");
    }
}

// Choose a stable MLIR-looking name for integer constants in the fallback path.
//
// Real MLIR prints constants as names like `%c42_i32`; using the same convention
// keeps fallback output close to real MLIR output and avoids two mental models.
std::string textualConstantName(const ir::Operation& operation) {
    const ir::ValueRef result = requiredValue(operation.result, "integer literal");
    return "%c" + std::to_string(parseIntegerLiteral(operation.text)) + "_" +
           textualType(result.type);
}

// Convert a nex arithmetic token into the textual MLIR operation name.
//
// This fallback emits MLIR text by hand, so it needs string spellings such as
// `arith.addi` where the real path uses C++ operation classes.
std::string textualBinaryOp(TokenKind op) {
    switch (op) {
    case TokenKind::Plus:
        return "arith.addi";
    case TokenKind::Minus:
        return "arith.subi";
    case TokenKind::Star:
        return "arith.muli";
    case TokenKind::Slash:
        return "arith.divsi";
    case TokenKind::Percent:
        return "arith.remsi";
    default:
        throw std::logic_error("textual MLIR lowering only supports integer arithmetic operators for now");
    }
}

// Convert a nex comparison token into the textual `arith.cmpi` predicate.
//
// As in the real MLIR path, relational comparisons currently use signed
// predicates because this lowering slice only supports i32.
std::string textualComparisonPredicate(TokenKind op) {
    switch (op) {
    case TokenKind::EqualEqual:
        return "eq";
    case TokenKind::BangEqual:
        return "ne";
    case TokenKind::Less:
        return "slt";
    case TokenKind::LessEqual:
        return "sle";
    case TokenKind::Greater:
        return "sgt";
    case TokenKind::GreaterEqual:
        return "sge";
    default:
        throw std::logic_error("token is not an integer comparison operator");
    }
}

class TextualFunctionDumper {
public:
    // Create a fallback dumper for one function.
    //
    // The dumper writes directly to the module stream and borrows the IR
    // Function. It keeps its own maps from IR values/locals to textual MLIR names
    // because no real MLIR Value objects exist in this build configuration.
    TextualFunctionDumper(std::ostream& out, const ir::Function& function)
        : out_(out),
          function_(function) {}

    // Emit the textual `func.func` wrapper and then dump the function body.
    //
    // Parameter locals are seeded as `%argN` names, matching real MLIR's function
    // entry block arguments. Later LoadLocal operations can then alias to those
    // names without printing a fake load operation.
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

        const bool returned = dumpBlock(function_.body);
        if (!returned && function_.returnType.isVoid()) {
            out_ << indent() << "return\n";
        }

        out_ << "  }\n";
    }

private:
    // Dump a block and report whether it emitted a function-level return.
    //
    // This mirrors the real FunctionLowerer's lowerBlock() contract so both
    // paths agree about when to stop emitting after a returning operation.
    bool dumpBlock(const ir::Block& block) {
        for (const ir::Operation& operation : block.operations) {
            if (dumpOperation(operation)) {
                return true;
            }
        }
        return dumpTerminator(block.terminator);
    }

    // Dump one typed IR operation as handwritten MLIR text.
    //
    // Returning true means the operation completed the function with a return,
    // which currently only happens for the supported returning `if/else` shape.
    bool dumpOperation(const ir::Operation& operation) {
        // This fallback is intentionally small, but it follows the same
        // operation-by-operation lowering model as the real MLIR API path. That
        // keeps development machines without MLIR packages useful for reading
        // output and running the compiler's early tests.
        switch (operation.kind) {
        case ir::Operation::Kind::IntegerLiteral:
            dumpIntegerLiteral(operation);
            return false;
        case ir::Operation::Kind::BoolLiteral:
            dumpBoolLiteral(operation);
            return false;
        case ir::Operation::Kind::StringLiteral:
            throw std::logic_error("backend limitation: string literals are not lowered yet; runtime string support is required before compiling programs that use strings or print/println");
        case ir::Operation::Kind::LoadLocal:
            dumpLoadLocal(operation);
            return false;
        case ir::Operation::Kind::LoadConst:
            throw std::logic_error("backend limitation: module constants are not lowered to MLIR yet");
        case ir::Operation::Kind::DeclareLocal:
            dumpDeclareLocal(operation);
            return false;
        case ir::Operation::Kind::StoreLocal:
            dumpStoreLocal(operation);
            return false;
        case ir::Operation::Kind::Unary:
            dumpUnary(operation);
            return false;
        case ir::Operation::Kind::Binary:
            dumpBinary(operation);
            return false;
        case ir::Operation::Kind::Call:
            dumpCall(operation);
            return false;
        case ir::Operation::Kind::If:
            return dumpIf(operation);
        case ir::Operation::Kind::While:
            dumpWhile(operation);
            return false;
        case ir::Operation::Kind::Break:
        case ir::Operation::Kind::Continue:
            throw std::logic_error(
                "`break`/`continue` must be dumped inside a loop region in this textual "
                "MLIR slice");
        default:
            throw std::logic_error("textual MLIR lowering encountered an unsupported operation");
        }
    }

    // Emit an i32 integer literal as `arith.constant`.
    //
    // The fallback binds the IR ValueRef to the printed constant name so later
    // operations can reuse it exactly as if they were holding a real MLIR Value.
    void dumpIntegerLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "integer literal");
        if (result.type.kind != BuiltinTypeKind::I32) {
            throw std::logic_error("textual MLIR integer literal lowering only supports i32");
        }

        const std::string name = textualConstantName(operation);
        out_ << indent() << name << " = arith.constant "
             << parseIntegerLiteral(operation.text) << " : "
             << textualType(result.type) << '\n';
        bindValue(result, name);
    }

    // Emit a bool literal as an MLIR one-bit constant.
    //
    // The printed names `%true` and `%false` mirror the readable style of real
    // MLIR for boolean constants.
    void dumpBoolLiteral(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "bool literal");
        const std::string name = operation.boolValue ? "%true" : "%false";
        out_ << indent() << name << " = arith.constant "
             << (operation.boolValue ? "true" : "false") << '\n';
        bindValue(result, name);
    }

    // Emit a local load.
    //
    // Parameter locals alias directly to `%argN`, while `let` locals are stored
    // in zero-dimensional memrefs and must be read with `memref.load`.
    void dumpLoadLocal(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "local load");
        if (const auto direct = localValues_.find(operation.local.id);
            direct != localValues_.end()) {
            bindValue(result, direct->second);
            return;
        }

        const auto slot = localSlots_.find(operation.local.id);
        if (slot == localSlots_.end()) {
            throw std::logic_error("textual MLIR lowering loaded an unknown local slot");
        }

        const std::string name = nextValueName();
        out_ << indent() << name << " = memref.load " << slot->second
             << "[] : memref<" << textualType(result.type) << ">\n";
        bindValue(result, name);
    }

    // Emit a local declaration as memref allocation plus initializer store.
    //
    // The fallback mirrors the real MLIR path closely: each local gets a
    // zero-dimensional memref, which behaves like a single stack slot.
    void dumpDeclareLocal(const ir::Operation& operation) {
        const ir::ValueRef init = requiredValue(operation.value, "local initializer");
        const std::string slotName = nextValueName();
        out_ << indent() << slotName << " = memref.alloca() : memref<"
             << textualType(init.type) << ">\n";

        const auto [_, inserted] = localSlots_.emplace(operation.local.id, slotName);
        if (!inserted) {
            throw std::logic_error("textual MLIR lowering declared a local slot twice");
        }

        out_ << indent() << "memref.store " << lookupValue(init) << ", " << slotName
             << "[] : memref<" << textualType(init.type) << ">\n";
    }

    // Emit assignment to a local slot as `memref.store`.
    //
    // The semantic analyzer already guaranteed assignment is legal; this layer
    // only has to find the existing stack slot and store the lowered value.
    void dumpStoreLocal(const ir::Operation& operation) {
        const ir::ValueRef stored = requiredValue(operation.value, "local store value");
        const auto slot = localSlots_.find(operation.local.id);
        if (slot == localSlots_.end()) {
            throw std::logic_error("textual MLIR lowering stored to an unknown local slot");
        }
        out_ << indent() << "memref.store " << lookupValue(stored) << ", "
             << slot->second << "[] : memref<" << textualType(stored.type) << ">\n";
    }

    // Emit unary logical not as a comparison against false/zero.
    //
    // This is intentionally the same boring lowering as the real MLIR path. It
    // avoids inventing a fallback-only representation for `!`.
    void dumpUnary(const ir::Operation& operation) {
        if (operation.op != TokenKind::Bang) {
            throw std::logic_error("textual MLIR lowering only supports unary ! for now");
        }

        const ir::ValueRef result = requiredValue(operation.result, "unary operation");
        const ir::ValueRef operandRef = requiredValue(operation.value, "unary operand");
        const std::string operand = lookupValue(operandRef);
        const std::string name = nextValueName();
        if (operandRef.type.kind == BuiltinTypeKind::Bool) {
            out_ << indent() << "%false = arith.constant false\n";
            out_ << indent() << name << " = arith.cmpi eq, " << operand
                 << ", %false : i1\n";
        } else if (operandRef.type.kind == BuiltinTypeKind::I32) {
            out_ << indent() << "%c0_i32 = arith.constant 0 : i32\n";
            out_ << indent() << name << " = arith.cmpi eq, " << operand
                 << ", %c0_i32 : i32\n";
        } else {
            throw std::logic_error("textual MLIR lowering only supports ! for bool and i32 for now");
        }
        bindValue(result, name);
    }

    // Emit arithmetic or comparison binary operations.
    //
    // Temporaries use nextValueName() rather than the typed IR's ValueRef id
    // because real MLIR renumbers printed SSA values. Matching that behavior
    // keeps golden files stable across real and fallback builds.
    void dumpBinary(const ir::Operation& operation) {
        const ir::ValueRef result = requiredValue(operation.result, "binary operation");
        const std::string left =
            lookupValue(requiredValue(operation.left, "binary left operand"));
        const std::string right =
            lookupValue(requiredValue(operation.right, "binary right operand"));
        const std::string name = nextValueName();

        switch (operation.op) {
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            out_ << indent() << name << " = arith.cmpi "
                 << textualComparisonPredicate(operation.op) << ", " << left
                 << ", " << right << " : " << textualType(operation.left->type)
                 << '\n';
            break;
        default:
            out_ << indent() << name << " = " << textualBinaryOp(operation.op) << ' '
                 << left << ", " << right << " : " << textualType(result.type)
                 << '\n';
            break;
        }
        bindValue(result, name);
    }

    // Emit a direct value-returning function call.
    //
    // Real MLIR prints `func.call` using the shorthand `call`, so the fallback
    // uses `call` too. Built-ins are rejected until runtime lowering exists.
    void dumpCall(const ir::Operation& operation) {
        if (operation.isBuiltin) {
            throw std::logic_error("backend limitation: built-in call '" +
                                   operation.text +
                                   "' is not lowered yet; runtime support is required before compiling print/println programs");
        }

        if (!operation.result) {
            throw std::logic_error("textual MLIR lowering only supports value-returning calls so far");
        }

        const std::string name = nextValueName();
        out_ << indent() << name << " = call @" << operation.text << '(';
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

    // Emit a structured if statement as textual `scf.if`.
    //
    // The fallback supports both returning if/else and fallthrough if/else. This
    // mirrors the real MLIR path so machines without MLIR installed still print
    // the same learning-oriented shape.
    bool dumpIf(const ir::Operation& operation) {
        if (!operation.thenBlock || !operation.elseBlock) {
            return dumpFallthroughIf(operation);
        }

        const ir::Terminator& thenTerminator = operation.thenBlock->terminator;
        const ir::Terminator& elseTerminator = operation.elseBlock->terminator;
        if (thenTerminator.kind == ir::Terminator::Kind::None &&
            elseTerminator.kind == ir::Terminator::Kind::None) {
            return dumpFallthroughIf(operation);
        }

        if (thenTerminator.kind != elseTerminator.kind) {
            throw std::logic_error("textual MLIR lowering requires matching if/else terminators");
        }

        const bool returnsValue =
            thenTerminator.kind == ir::Terminator::Kind::ReturnValue;
        if (!returnsValue && thenTerminator.kind != ir::Terminator::Kind::Return) {
            throw std::logic_error("textual MLIR lowering only supports returning if/else branches for now");
        }

        std::string resultName;
        if (returnsValue) {
            const ir::ValueRef thenValue =
                requiredValue(thenTerminator.value, "then return");
            resultName = nextValueName();
            out_ << indent() << resultName << " = scf.if "
                 << lookupValue(requiredValue(operation.condition, "if condition"))
                 << " -> (" << textualType(thenValue.type) << ") {\n";
        } else {
            out_ << indent() << "scf.if "
                 << lookupValue(requiredValue(operation.condition, "if condition"))
                 << " {\n";
        }

        dumpYieldingReturnBlock(*operation.thenBlock);
        out_ << indent() << "} else {\n";
        dumpYieldingReturnBlock(*operation.elseBlock);
        out_ << indent() << "}\n";

        if (returnsValue) {
            const ir::ValueRef thenValue =
                requiredValue(thenTerminator.value, "then return");
            out_ << indent() << "return " << resultName << " : "
                 << textualType(thenValue.type) << '\n';
        } else {
            out_ << indent() << "return\n";
        }
        return true;
    }

    // Emit a no-result `scf.if` whose branches perform effects and fall through.
    //
    // This is the shape needed for `if` statements inside loops, where the
    // branch usually stores into a mutable local and then execution continues.
    bool dumpFallthroughIf(const ir::Operation& operation) {
        out_ << indent() << "scf.if "
             << lookupValue(requiredValue(operation.condition, "if condition"))
             << " {\n";

        dumpFallthroughRegionBlock(*operation.thenBlock, "if then branch");
        out_ << indent() << "}";
        if (operation.elseBlock) {
            out_ << " else {\n";
            dumpFallthroughRegionBlock(*operation.elseBlock, "if else branch");
            out_ << indent() << "}";
        }
        out_ << '\n';
        return false;
    }

    // Emit a Core v0 while loop as textual `scf.while`.
    //
    // This v0 lowering keeps local variables in memrefs, so there are no
    // loop-carried SSA values to list in the `scf.while` header. The condition
    // region computes an i1 and ends with `scf.condition`; the body region runs
    // side effects and ends with `scf.yield`.
    void dumpWhile(const ir::Operation& operation) {
        if (!operation.conditionBlock || !operation.bodyBlock) {
            throw std::logic_error("while operation is missing its condition or body block");
        }

        const std::string exitSlot = nextValueName();
        out_ << indent() << exitSlot << " = memref.alloca() : memref<i1>\n";
        const std::string cfalse = nextValueName();
        out_ << indent() << cfalse << " = arith.constant false\n";
        out_ << indent() << "memref.store " << cfalse << ", " << exitSlot
             << "[] : memref<i1>\n";

        activeLoops_.push_back(ActiveLoopText{
            .exitSlotName = exitSlot,
            .stepBlock = operation.stepBlock ? operation.stepBlock.get() : nullptr,
        });

        out_ << indent() << "scf.while : () -> () {\n";
        dumpConditionBlock(*operation.conditionBlock, &exitSlot);
        out_ << indent() << "} do {\n";
        dumpFallthroughRegionBlock(*operation.bodyBlock, "while body");
        if (operation.stepBlock) {
            dumpFallthroughRegionBlock(*operation.stepBlock, "for step");
        }
        out_ << childRegionIndent() << "scf.yield\n";
        out_ << indent() << "}\n";

        activeLoops_.pop_back();
    }

    // Dump an if/else branch body in `scf.if` region context.
    //
    // Region indentation is deeper than the function body. The nested_ flag is a
    // small formatting aid so reused operation dumpers emit the correct spaces.
    void dumpYieldingReturnBlock(const ir::Block& block) {
        ++regionDepth_;
        for (const ir::Operation& operation : block.operations) {
            if (dumpOperation(operation)) {
                throw std::logic_error("nested if/else returns are not lowered yet");
            }
        }

        switch (block.terminator.kind) {
        case ir::Terminator::Kind::Return:
            out_ << regionIndent() << "scf.yield\n";
            --regionDepth_;
            return;
        case ir::Terminator::Kind::ReturnValue: {
            const ir::ValueRef value =
                requiredValue(block.terminator.value, "if/else return");
            out_ << regionIndent() << "scf.yield " << lookupValue(value) << " : "
                 << textualType(value.type) << '\n';
            --regionDepth_;
            return;
        }
        default:
            --regionDepth_;
            throw std::logic_error("if/else branch must end in a return for this MLIR slice");
        }
    }

    // Dump a nested `scf` region that must not return from the function.
    //
    // The region depth changes indentation for all reused operation dumpers,
    // which keeps nested while/if output readable without separate printer code
    // for every possible nesting level.
    void dumpFallthroughRegionBlock(const ir::Block& block,
                                    std::string_view context) {
        ++regionDepth_;
        for (const ir::Operation& operation : block.operations) {
            if (operation.kind == ir::Operation::Kind::Break) {
                if (activeLoops_.empty()) {
                    --regionDepth_;
                    throw std::logic_error(std::string(context) +
                                           " contains `break` outside any loop");
                }
                const std::string& slot = activeLoops_.back().exitSlotName;
                const std::string ctrue = nextValueName();
                out_ << regionIndent() << ctrue << " = arith.constant true\n";
                out_ << regionIndent() << "memref.store " << ctrue << ", " << slot
                     << "[] : memref<i1>\n";
                out_ << regionIndent() << "scf.yield\n";
                --regionDepth_;
                return;
            }
            if (operation.kind == ir::Operation::Kind::Continue) {
                if (activeLoops_.empty()) {
                    --regionDepth_;
                    throw std::logic_error(std::string(context) +
                                           " contains `continue` outside any loop");
                }
                const ActiveLoopText& loop = activeLoops_.back();
                if (loop.stepBlock) {
                    dumpFallthroughRegionBlock(*loop.stepBlock, "for step on continue");
                }
                out_ << regionIndent() << "scf.yield\n";
                --regionDepth_;
                return;
            }
            if (dumpOperation(operation)) {
                --regionDepth_;
                throw std::logic_error(std::string(context) +
                                       " cannot contain a function return in this textual MLIR slice");
            }
        }
        --regionDepth_;

        if (block.terminator.kind != ir::Terminator::Kind::None) {
            throw std::logic_error(std::string(context) +
                                   " must fall through in this textual MLIR slice");
        }
    }

    // Dump the condition region of textual `scf.while`.
    //
    // A typed IR while condition is a block of expression operations plus a
    // `ConditionValue` terminator. The textual MLIR equivalent is those
    // operations followed by `scf.condition(%cond)`.
    void dumpConditionBlock(const ir::Block& block,
                            const std::string* breakExitSlot = nullptr) {
        ++regionDepth_;
        for (const ir::Operation& operation : block.operations) {
            if (dumpOperation(operation)) {
                --regionDepth_;
                throw std::logic_error("while condition cannot return from the function");
            }
        }
        if (block.terminator.kind != ir::Terminator::Kind::ConditionValue) {
            --regionDepth_;
            throw std::logic_error("while condition block must end with ConditionValue");
        }

        std::string cond = lookupValue(requiredValue(block.terminator.value, "while condition"));
        if (breakExitSlot) {
            const std::string loaded = nextValueName();
            out_ << regionIndent() << loaded << " = memref.load " << *breakExitSlot
                 << "[] : memref<i1>\n";
            const std::string ctrue = nextValueName();
            out_ << regionIndent() << ctrue << " = arith.constant true\n";
            const std::string flipped = nextValueName();
            out_ << regionIndent() << flipped << " = arith.xori " << loaded << ", "
                 << ctrue << " : i1\n";
            const std::string combined = nextValueName();
            out_ << regionIndent() << combined << " = arith.andi " << cond << ", "
                 << flipped << " : i1\n";
            cond = combined;
        }

        out_ << regionIndent() << "scf.condition(" << cond << ")\n";
        --regionDepth_;
    }

    // Dump a function-body terminator.
    //
    // Return terminators become textual `return`. ConditionValue and InitValue
    // are not valid in this context; they belong to structured condition blocks
    // or const initializer blocks that this fallback cannot lower yet.
    bool dumpTerminator(const ir::Terminator& terminator) {
        switch (terminator.kind) {
        case ir::Terminator::Kind::Return:
            out_ << indent() << "return\n";
            return true;
        case ir::Terminator::Kind::ReturnValue: {
            const ir::ValueRef value =
                requiredValue(terminator.value, "return terminator");
            out_ << indent() << "return " << lookupValue(value) << " : "
                 << textualType(value.type) << '\n';
            return true;
        }
        case ir::Terminator::Kind::None:
            return false;
        case ir::Terminator::Kind::ConditionValue:
        case ir::Terminator::Kind::InitValue:
            throw std::logic_error("textual MLIR lowering expected a function return terminator");
        }
        throw std::logic_error("unknown terminator kind during textual MLIR lowering");
    }

    // Bind a typed IR temporary to the textual MLIR name that represents it.
    //
    // This is the fallback version of FunctionLowerer::bindValue().
    void bindValue(ir::ValueRef ref, std::string name) {
        const auto [_, inserted] = values_.emplace(ref.id, std::move(name));
        if (!inserted) {
            throw std::logic_error("typed IR value was defined more than once during textual MLIR lowering");
        }
    }

    // Look up the textual MLIR name for a previously dumped IR temporary.
    //
    // A miss means the fallback emitter tried to use a value before printing the
    // operation that defines it.
    std::string lookupValue(ir::ValueRef ref) const {
        const auto found = values_.find(ref.id);
        if (found == values_.end()) {
            throw std::logic_error("textual MLIR lowering used a typed IR value before it was defined");
        }
        return found->second;
    }

    // Allocate the next generic SSA name for fallback output.
    //
    // Constants keep readable names such as `%c42_i32`, but computations use
    // `%0`, `%1`, ... to match the real MLIR printer.
    std::string nextValueName() {
        return "%" + std::to_string(nextValueId_++);
    }

    struct ActiveLoopText {
        std::string exitSlotName;
        const ir::Block* stepBlock = nullptr;
    };

    std::ostream& out_;
    const ir::Function& function_;
    std::vector<ActiveLoopText> activeLoops_{};
    std::unordered_map<std::size_t, std::string> values_;
    // localValues_ holds direct SSA aliases for immutable parameter locals.
    std::unordered_map<std::size_t, std::string> localValues_;

    // localSlots_ holds memref names for `let` / `let mut` storage slots.
    std::unordered_map<std::size_t, std::string> localSlots_;

    // Return the current operation indentation.
    //
    // Function-body operations use four spaces. Each nested `scf` region adds
    // two more spaces, which keeps while/if nesting visually aligned with the
    // real MLIR printer.
    std::string indent() const {
        return std::string(4 + (regionDepth_ * 2), ' ');
    }

    // Return indentation for explicit region terminators.
    //
    // This is currently the same as indent(), but the separate name makes call
    // sites that print `scf.yield` or `scf.condition` easier to read.
    std::string regionIndent() const { return indent(); }

    // Return the indentation one region deeper than the current location.
    //
    // This is useful while printing a region wrapper line like `} do {`: the
    // current depth still belongs to the wrapper, but the terminator we are
    // about to print belongs inside the child region.
    std::string childRegionIndent() const {
        return std::string(4 + ((regionDepth_ + 1) * 2), ' ');
    }

    std::size_t regionDepth_ = 0;
    std::size_t nextValueId_ = 0;
};
#endif

} // namespace

#ifdef NEXC_HAS_REAL_MLIR
// Register every dialect used by the Core v0 MLIR module builder.
//
// This function is intentionally tiny, but it is important: MLIR contexts are
// dialect-aware. If a context has not loaded the dialect for an operation such as
// `scf.while` or `memref.load`, building or parsing that operation will fail.
void loadCoreMlirDialects(::mlir::MLIRContext& context) {
    context.getOrLoadDialect<::mlir::func::FuncDialect>();
    context.getOrLoadDialect<::mlir::arith::ArithDialect>();
    context.getOrLoadDialect<::mlir::scf::SCFDialect>();
    context.getOrLoadDialect<::mlir::memref::MemRefDialect>();
    context.getOrLoadDialect<::mlir::LLVM::LLVMDialect>();
}

// Build the high-level MLIR module shared by `--dump-mlir` and LLVM lowering.
//
// The returned module is still in nex's first MLIR shape: func/arith/scf/memref.
// It is verified here so downstream stages can assume the frontend-to-MLIR
// boundary produced structurally valid MLIR before any lowering-to-LLVM passes
// are allowed to run.
::mlir::OwningOpRef<::mlir::ModuleOp>
buildMlirModule(::mlir::MLIRContext& context, const ir::Module& module) {
    loadCoreMlirDialects(context);

    ::mlir::OpBuilder builder(&context);
    const ::mlir::Location loc = builder.getUnknownLoc();
    ::mlir::OwningOpRef<::mlir::ModuleOp> mlirModule =
        ::mlir::ModuleOp::create(loc);

    std::unordered_map<std::string, const ir::Const*> constants;
    for (const ir::Const& constant : module.constants) {
        constants.emplace(constant.name, &constant);
    }

    builder.setInsertionPointToStart(mlirModule->getBody());
    for (const ir::Function& function : module.functions) {
        FunctionLowerer(builder, *mlirModule, function, constants).lower();
        builder.setInsertionPointToEnd(mlirModule->getBody());
    }

    if (failed(::mlir::verify(*mlirModule))) {
        throw std::logic_error("generated MLIR module failed verification");
    }

    return mlirModule;
}
#endif

// Public entry point for typed IR -> MLIR text.
//
// The function has two compile-time implementations. With MLIR available, it
// constructs and verifies a real MLIR module. Without MLIR, it prints a limited
// fallback text format that deliberately mirrors the real MLIR printer closely
// enough for learning and golden tests.
void dumpTextualMlir(std::ostream& out, const ir::Module& module) {
#ifdef NEXC_HAS_REAL_MLIR
    ::mlir::MLIRContext context;
    ::mlir::OwningOpRef<::mlir::ModuleOp> mlirModule =
        buildMlirModule(context, module);

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
