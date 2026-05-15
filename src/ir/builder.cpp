#include "nexc/ir/builder.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

// Typed IR construction for nex expressions and statements.
//
// `let mut x: T;` lowers to `DeclareLocal` **without** `value`; MLIR allocates a
// memref slot and skips the initializing `memref.store`. Paired with semantic
// definite assignment, this is sound: the front end rejects reads until a store
// has executed.
//
// Short-circuit lowering for `&&` / `||` is implemented here (not in MLIR):
//
// - Function bodies use distinct IR operations (`ShortCircuitAnd` /
//   `ShortCircuitOr`) whose child blocks map cleanly onto `scf.if` regions.
// - Module-level `const` initializers use eager lowering: both operands are
//   evaluated, each is coerced to `bool`, then lowered as a plain `Binary` so
//   replay stays a straight-line sequence (see `buildBinary` and `currentConst_`).
//
// Condition-like to bool uses double logical negation (`!!v` at source level):
// IR emits two unary `!` operations. For integers, each `!` lowers to a compare
// against zero, matching `if` / `while` condition semantics.

namespace nexc::ir {

namespace {

// The builder needs function signatures before it can lower function bodies,
// because a call expression must know the expected argument types and the result
// type. This mirrors the semantic analyzer's top-level collection pass.
struct FunctionSignature {
    std::vector<Type> parameterTypes;
    Type returnType;
    bool isBuiltin = false;
};

// ValueSymbol is the builder's resolved answer for a source name used as a
// value. It can represent either a function local slot or a module-level const.
struct ValueSymbol {
    Type type{};
    LocalRef local{};
    bool isConst = false;
};

// Convert the parser/AST type wrapper into the IR type wrapper.
//
// This is a tiny function today because both layers share BuiltinTypeKind. It is
// still useful as a named boundary: if IR types later grow layout/ABI details,
// this becomes the one place where AST type syntax starts becoming IR type data.
Type typeFromSyntax(TypeSyntax syntax) {
    using TS = ::nexc::TypeSyntaxKind;
    if (syntax.form == TS::FixedArray) {
        return Type{.shape = Type::Shape::FixedArray,
                    .elementKind = syntax.arrayElementKind,
                    .arrayLength = syntax.arrayLength};
    }
    return Type{.shape = Type::Shape::Scalar, .kind = syntax.kind};
}

// TypedIrBuilder owns the stateful AST walk that emits one IR module.
//
// The builder is intentionally private to this .cpp file. The public API is the
// simpler buildTypedIr() function in builder.h; callers should not depend on the
// builder's temporary symbol tables or current-block pointers.
class TypedIrBuilder {
public:
    // Build a complete IR Module from one checked AST translation unit.
    //
    // The build happens in two phases: first collect all top-level symbol
    // signatures, then lower constants/functions in source order. That order
    // lets calls refer to functions declared later in the file.
    Module build(const TranslationUnit& unit) {
        // Built-ins and user-defined top-level declarations must be known before
        // any body is lowered, otherwise calls and const references could not be
        // resolved into typed IR operations.
        installBuiltins();
        collectTopLevelSymbols(unit);

        Module module;

        for (const std::unique_ptr<Item>& item : unit.items) {
            if (const auto* constant = dynamic_cast<const ConstDecl*>(item.get())) {
                module.constants.push_back(buildConst(*constant));
            } else if (const auto* function =
                           dynamic_cast<const FunctionDecl*>(item.get())) {
                module.functions.push_back(buildFunction(*function));
            }
        }

        return module;
    }

private:
    // Register compiler-provided functions in the same signature table as user
    // functions. This lets call lowering treat `print("x")` and `foo(1)` the
    // same way until a later backend phase needs special runtime handling.
    void installBuiltins() {
        const Type str{.kind = BuiltinTypeKind::Str};
        const Type i32{.kind = BuiltinTypeKind::I32};
        const Type u64{.kind = BuiltinTypeKind::U64};
        const Type boolType{.kind = BuiltinTypeKind::Bool};
        const Type voidType{.kind = BuiltinTypeKind::Void};

        functions_["print"] = FunctionSignature{
            .parameterTypes = {},
            .returnType = voidType,
            .isBuiltin = true,
        };
        functions_["println"] = FunctionSignature{
            .parameterTypes = {},
            .returnType = voidType,
            .isBuiltin = true,
        };
        functions_["readln"] = FunctionSignature{
            .parameterTypes = {},
            .returnType = str,
            .isBuiltin = true,
        };
        functions_["parse_i32"] = FunctionSignature{
            .parameterTypes = {str},
            .returnType = i32,
            .isBuiltin = true,
        };
        functions_["parse_u64"] = FunctionSignature{
            .parameterTypes = {str},
            .returnType = u64,
            .isBuiltin = true,
        };
        functions_["parse_bool"] = FunctionSignature{
            .parameterTypes = {str},
            .returnType = boolType,
            .isBuiltin = true,
        };
        functions_["input_ok"] = FunctionSignature{
            .parameterTypes = {},
            .returnType = boolType,
            .isBuiltin = true,
        };
    }

    // Collect just enough top-level information for IR construction. Semantic
    // analysis already diagnosed duplicates, bad main shapes, and bad calls; the
    // builder only needs the accepted symbol facts.
    void collectTopLevelSymbols(const TranslationUnit& unit) {
        for (const std::unique_ptr<Item>& item : unit.items) {
            if (const auto* constant = dynamic_cast<const ConstDecl*>(item.get())) {
                constants_[constant->name] = typeFromSyntax(constant->type);
                continue;
            }

            if (const auto* function = dynamic_cast<const FunctionDecl*>(item.get())) {
                std::vector<Type> parameterTypes;
                parameterTypes.reserve(function->parameters.size());
                for (const ParameterSyntax& parameter : function->parameters) {
                    parameterTypes.push_back(typeFromSyntax(parameter.type));
                }

                functions_[function->name] = FunctionSignature{
                    .parameterTypes = std::move(parameterTypes),
                    .returnType = typeFromSyntax(function->returnType),
                };
            }
        }
    }

    // Lower one module-level const declaration into IR.
    //
    // Even though source syntax has a single initializer expression, IR stores
    // it as a Block because expression lowering may need multiple operations
    // before the final InitValue terminator names the computed value.
    Const buildConst(const ConstDecl& decl) {
        Const constant{
            .name = decl.name,
            .type = typeFromSyntax(decl.type),
            .span = decl.span,
            .initializer = Block{.span = decl.init->span},
        };

        // A const initializer has its own temporary value namespace. It is not
        // inside a function, so currentFunction_ stays null while makeValue()
        // increments constant.nextValueId.
        currentConst_ = &constant;
        currentFunction_ = nullptr;
        currentBlock_ = &constant.initializer;
        scopes_.clear();

        const ValueRef init = buildExpr(*decl.init, constant.type);
        constant.initializer.terminator = Terminator{
            .kind = Terminator::Kind::InitValue,
            .span = decl.init->span,
            .value = init,
        };

        currentBlock_ = nullptr;
        currentConst_ = nullptr;
        return constant;
    }

    // Lower one source function declaration into an IR Function.
    //
    // This creates parameter locals first, then walks the function body into the
    // function's root block. Local and temporary IDs are reset per function so
    // each function dump starts with familiar names such as `$0` and `%0`.
    Function buildFunction(const FunctionDecl& decl) {
        Function function{
            .name = decl.name,
            .returnType = typeFromSyntax(decl.returnType),
            .span = decl.span,
            .body = Block{.span = decl.body->span},
        };

        // Function construction owns local slots and temporary value IDs. The
        // current* pointers let small emission helpers append to the function
        // currently being built without threading many references through every
        // recursive call.
        currentFunction_ = &function;
        currentConst_ = nullptr;
        currentBlock_ = &function.body;
        scopes_.clear();
        pushScope();

        function.parameters.reserve(decl.parameters.size());
        for (const ParameterSyntax& parameter : decl.parameters) {
            const Type type = typeFromSyntax(parameter.type);
            // Parameters are declared as locals because the body reads them with
            // the same LoadLocal operation used for `let` bindings.
            const LocalRef local = declareLocal(parameter.name, parameter.nameSpan, type,
                                                false, Local::Kind::Parameter);
            function.parameters.push_back(Parameter{
                .name = parameter.name,
                .type = type,
                .local = local,
                .span = parameter.span,
            });
        }

        buildBlockStatements(*decl.body);
        popScope();

        currentBlock_ = nullptr;
        currentFunction_ = nullptr;
        return function;
    }

    // Lower the statements inside an AST block while honoring lexical scope.
    //
    // This helper is used both for whole function bodies and nested source
    // blocks. It deliberately stops after a terminator such as `return`, because
    // the current structured IR has no place to attach operations after a block
    // has already said "control leaves here."
    void buildBlockStatements(const BlockStmt& block) {
        // AST blocks are lexical scopes. A nested block can shadow outer names,
        // so the IR builder mirrors semantic analysis with a scope stack.
        pushScope();
        for (const std::unique_ptr<Stmt>& statement : block.statements) {
            buildStmt(*statement);
            if (currentBlock_->terminator.kind != Terminator::Kind::None) {
                // Once a block returns, later AST statements are unreachable for
                // this structured IR dump. Semantic analysis already decided
                // whether return paths are valid.
                break;
            }
            if (dynamic_cast<const BreakStmt*>(statement.get()) ||
                dynamic_cast<const ContinueStmt*>(statement.get())) {
                break;
            }
        }
        popScope();
    }

    // Dispatch one checked AST statement to the corresponding IR shape.
    //
    // The AST has a class per statement form. The IR has a smaller collection of
    // operations and terminators, so this method is where source-level ideas like
    // `let`, assignment, `return`, `if`, and `while` become backend-facing forms.
    void buildStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            buildBlockStatements(*block);
            return;
        }

        if (const auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
            const Type type = typeFromSyntax(let->type);
            const LocalRef local =
                declareLocal(let->name, let->nameSpan, type, let->isMutable,
                             Local::Kind::Local);
            Operation op{
                .kind = Operation::Kind::DeclareLocal,
                .span = let->span,
            };
            op.local = local;
            op.isMutable = let->isMutable;
            if (let->init) {
                const ValueRef init = buildExpr(*let->init, type);
                op.value = init;
            }
            append(std::move(op));
            return;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            if (const auto* nameExpr = dynamic_cast<const NameExpr*>(assign->target.get())) {
                const ValueSymbol symbol = lookupValue(nameExpr->name);
                const ValueRef value = buildExpr(*assign->value, symbol.type);
                Operation op{
                    .kind = Operation::Kind::StoreLocal,
                    .span = assign->span,
                };
                op.local = symbol.local;
                op.value = value;
                append(std::move(op));
                return;
            }

            if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(assign->target.get())) {
                const ValueRef baseAddr =
                    buildExpr(*indexExpr->base, std::nullopt);
                const ValueRef indexVal =
                    buildExpr(*indexExpr->index,
                              Type{.shape = Type::Shape::Scalar,
                                   .kind = BuiltinTypeKind::I32});
                const ValueRef stored =
                    buildExpr(*assign->value, baseAddr.type.elementType());
                Operation op{
                    .kind = Operation::Kind::IndexStore,
                    .span = assign->span,
                };
                op.left = baseAddr;
                op.right = indexVal;
                op.value = stored;
                append(std::move(op));
                return;
            }

            throw std::logic_error("unsupported assignment target in typed IR builder");
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            if (ret->value) {
                const ValueRef value = buildExpr(*ret->value, currentReturnType());
                currentBlock_->terminator = Terminator{
                    .kind = Terminator::Kind::ReturnValue,
                    .span = ret->span,
                    .value = value,
                };
            } else {
                currentBlock_->terminator = Terminator{
                    .kind = Terminator::Kind::Return,
                    .span = ret->span,
                };
            }
            return;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            const ValueRef condition = buildExpr(*ifStmt->condition, std::nullopt);
            Operation op{
                .kind = Operation::Kind::If,
                .span = ifStmt->span,
            };
            op.condition = condition;
            // Keep `if` structured for now. A later lower-level pass can turn
            // this into blocks and branches if LLVM-style CFG lowering needs it.
            op.thenBlock = buildNestedStatementBlock(*ifStmt->thenBranch);
            if (ifStmt->elseBranch) {
                op.elseBlock = buildNestedStatementBlock(*ifStmt->elseBranch);
            }
            append(std::move(op));
            return;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            Operation op{
                .kind = Operation::Kind::While,
                .span = whileStmt->span,
            };
            // A while condition is a block because evaluating it may require
            // multiple operations before producing the final condition value.
            op.conditionBlock = buildConditionBlock(*whileStmt->condition);
            op.bodyBlock = buildNestedStatementBlock(*whileStmt->body);
            append(std::move(op));
            return;
        }

        if (const auto* forStmt = dynamic_cast<const ForStmt*>(&stmt)) {
            buildForStmt(*forStmt);
            return;
        }

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
            buildCall(*callStmt->call);
            return;
        }

        if (const auto* br = dynamic_cast<const BreakStmt*>(&stmt)) {
            append(Operation{.kind = Operation::Kind::Break, .span = br->span});
            return;
        }

        if (const auto* co = dynamic_cast<const ContinueStmt*>(&stmt)) {
            append(Operation{.kind = Operation::Kind::Continue, .span = co->span});
            return;
        }

        throw std::logic_error("unsupported statement in typed IR builder");
    }

    // Build a child Block owned by a structured control-flow operation.
    //
    // `if` and `while` keep nested bodies as nested IR blocks rather than
    // flattening them into labels and branches. This helper temporarily redirects
    // operation emission into the child block, then restores the outer block.
    std::unique_ptr<Block> buildNestedStatementBlock(const Stmt& stmt) {
        auto block = std::make_unique<Block>(Block{.span = stmt.span});

        // Temporarily redirect emission into the nested structured block. The
        // enclosing currentBlock_ is restored before returning.
        Block* outerBlock = currentBlock_;
        currentBlock_ = block.get();
        buildStmt(stmt);
        currentBlock_ = outerBlock;

        return block;
    }

    // Build a condition as a block that ends in a ConditionValue terminator.
    //
    // A condition might be more than one operation, for example `x + 1 < y`.
    // Representing it as a block preserves every intermediate value and gives
    // later lowering a single terminator value to use as the loop condition.
    std::unique_ptr<Block> buildConditionBlock(const Expr& condition) {
        auto block = std::make_unique<Block>(Block{.span = condition.span});

        // Conditions are represented as normal operation sequences ending in a
        // ConditionValue terminator. This makes complex conditions easy to dump:
        // all intermediate values appear before the final condition.
        Block* outerBlock = currentBlock_;
        currentBlock_ = block.get();
        const ValueRef value = buildExpr(condition, std::nullopt);
        block->terminator = Terminator{
            .kind = Terminator::Kind::ConditionValue,
            .span = condition.span,
            .value = value,
        };
        currentBlock_ = outerBlock;

        return block;
    }

    // `for (;;)` omits the middle clause; lowering treats that as an infinite loop
    // by testing a constant `true` condition each iteration.
    std::unique_ptr<Block> buildConditionBlockAlwaysTrue(SourceSpan span) {
        auto block = std::make_unique<Block>(Block{.span = span});
        Block* outerBlock = currentBlock_;
        currentBlock_ = block.get();
        Operation op{
            .kind = Operation::Kind::BoolLiteral,
            .span = span,
            .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
            .boolValue = true,
        };
        const ValueRef value = *op.result;
        append(std::move(op));
        block->terminator = Terminator{
            .kind = Terminator::Kind::ConditionValue,
            .span = span,
            .value = value,
        };
        currentBlock_ = outerBlock;
        return block;
    }

    // Lower `for` to `init; while (cond) { body; step; }` using structured `While`
    // IR so MLIR lowering stays unchanged.
    //
    // Rationale: `scf.while` lowering and return-path habits already understand
    // `While`; duplicating that for a distinct `For` IR node would not teach much
    // more at this stage. The dump will show `While` even when the source said
    // `for`—see `docs/reference/language/statements.md`.
    void buildForStmt(const ForStmt& forStmt) {
        if (forStmt.init) {
            buildStmt(*forStmt.init);
        }

        Operation whileOp{
            .kind = Operation::Kind::While,
            .span = forStmt.span,
        };
        if (forStmt.condition) {
            whileOp.conditionBlock = buildConditionBlock(*forStmt.condition);
        } else {
            whileOp.conditionBlock = buildConditionBlockAlwaysTrue(forStmt.span);
        }

        auto bodyBlock = std::make_unique<Block>(Block{.span = forStmt.body->span});
        Block* outerBlock = currentBlock_;
        currentBlock_ = bodyBlock.get();
        buildStmt(*forStmt.body);
        currentBlock_ = outerBlock;
        whileOp.bodyBlock = std::move(bodyBlock);

        if (forStmt.step) {
            auto stepBlock = std::make_unique<Block>(Block{.span = forStmt.step->span});
            currentBlock_ = stepBlock.get();
            buildStmt(*forStmt.step);
            currentBlock_ = outerBlock;
            whileOp.stepBlock = std::move(stepBlock);
        }

        append(std::move(whileOp));
    }

    // Lower one expression and return the IR temporary value it produces.
    //
    // The optional expected type is the main way integer literals get their
    // concrete type. For example, in `let x: i32 = 1`, the declaration passes
    // `i32` down so the literal operation is born typed as i32.
    ValueRef buildExpr(const Expr& expr, std::optional<Type> expected) {
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
            Type type = expected.value_or(Type{.kind = BuiltinTypeKind::I32});
            if (!type.isInteger()) {
                // This should only happen if semantic analysis failed to reject
                // the program first. Preserve an invalid type rather than
                // guessing a lowering type.
                type = Type{};
            }

            Operation op{
                .kind = Operation::Kind::IntegerLiteral,
                .span = integer->span,
                .result = makeValue(type),
                .text = integer->raw,
            };
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* boolean = dynamic_cast<const BoolLiteralExpr*>(&expr)) {
            Operation op{
                .kind = Operation::Kind::BoolLiteral,
                .span = boolean->span,
                .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
                .boolValue = boolean->value,
            };
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* string = dynamic_cast<const StringLiteralExpr*>(&expr)) {
            Operation op{
                .kind = Operation::Kind::StringLiteral,
                .span = string->span,
                .result = makeValue(Type{.kind = BuiltinTypeKind::Str}),
                .text = string->raw,
            };
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* name = dynamic_cast<const NameExpr*>(&expr)) {
            const ValueSymbol symbol = lookupValue(name->name);
            // Source names disappear here. The IR records whether the name was a
            // local storage slot or a module const and emits the corresponding
            // resolved load operation.
            Operation op{
                .kind = symbol.isConst ? Operation::Kind::LoadConst
                                       : Operation::Kind::LoadLocal,
                .span = name->span,
                .result = makeValue(symbol.type),
                .text = name->name,
            };
            op.local = symbol.local;
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            const std::optional<ValueRef> result = buildCall(*call);
            if (!result) {
                // Semantic analysis rejects using a void expression as a value.
                // Reaching this path means the builder was called on an invalid
                // AST or the semantic contract changed without updating IR.
                throw std::logic_error("void call used where typed IR value is required");
            }
            return *result;
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            if (unary->op == TokenKind::Bang) {
                const ValueRef operand = buildExpr(*unary->operand, std::nullopt);
                Operation op{
                    .kind = Operation::Kind::Unary,
                    .span = unary->span,
                    .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
                    .op = unary->op,
                };
                op.value = operand;
                const ValueRef result = *op.result;
                append(std::move(op));
                return result;
            }

            const Type type = expected.value_or(Type{.kind = BuiltinTypeKind::I32});
            const ValueRef operand = buildExpr(*unary->operand, type);
            Operation op{
                .kind = Operation::Kind::Unary,
                .span = unary->span,
                .result = makeValue(operand.type),
                .op = unary->op,
            };
            op.value = operand;
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            return buildBinary(*binary, expected);
        }

        if (const auto* arrayLit = dynamic_cast<const ArrayLiteralExpr*>(&expr)) {
            if (!expected || !expected->isFixedArray()) {
                throw std::logic_error(
                    "array literal lowering requires a contextual fixed-array type");
            }
            const Type arrType = *expected;
            Operation op{
                .kind = Operation::Kind::ArrayLiteral,
                .span = expr.span,
                .result = makeValue(arrType),
            };
            const Type elemType = arrType.elementType();
            for (const std::unique_ptr<Expr>& el : arrayLit->elements) {
                op.arguments.push_back(buildExpr(*el, elemType));
            }
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(&expr)) {
            const ValueRef baseVal = buildExpr(*indexExpr->base, std::nullopt);
            const ValueRef indexVal =
                buildExpr(*indexExpr->index,
                          Type{.shape = Type::Shape::Scalar,
                               .kind = BuiltinTypeKind::I32});
            if (!baseVal.type.isFixedArray()) {
                throw std::logic_error("indexed load base must be a fixed array");
            }
            const Type elemType = baseVal.type.elementType();
            Operation op{
                .kind = Operation::Kind::IndexLoad,
                .span = expr.span,
                .result = makeValue(elemType),
            };
            op.left = baseVal;
            op.right = indexVal;
            const ValueRef result = *op.result;
            append(std::move(op));
            return result;
        }

        if (const auto* paren = dynamic_cast<const ParenExpr*>(&expr)) {
            return buildExpr(*paren->inner, expected);
        }

        throw std::logic_error("unsupported expression in typed IR builder");
    }

    // Lower a binary expression while choosing the correct result type.
    //
    // Arithmetic keeps the operand type. Comparisons and equality operators
    // produce bool. Logical `&&` / `||` also produce bool. In function bodies they
    // use structured short-circuit operations; in module `const` initializers they
    // lower as eager truthify-then-binary (`Binary` on bool) for a straight-line
    // initializer block.
    ValueRef buildBinary(const BinaryExpr& binary, std::optional<Type> expected) {
        if (binary.op == TokenKind::AmpAmp || binary.op == TokenKind::PipePipe) {
            if (currentConst_ != nullptr) {
                // Const contexts are compile-time-only. Both sides are emitted and
                // truthified so MLIR const replay can stay a simple operation stream
                // without nested `scf.if`. Observable semantics match short-circuit
                // for constant folding, but invalid RHS side effects are not the
                // concern of const initializers (they are rejected earlier).
                const ValueRef left = buildExpr(*binary.left, std::nullopt);
                const ValueRef right = buildExpr(*binary.right, std::nullopt);
                const ValueRef lb =
                    coerceConditionLikeToBool(left, binary.left->span);
                const ValueRef rb =
                    coerceConditionLikeToBool(right, binary.right->span);
                return appendBinary(binary, lb, rb,
                                    Type{.kind = BuiltinTypeKind::Bool});
            }
            if (binary.op == TokenKind::AmpAmp) {
                return buildShortCircuitAnd(binary);
            }
            return buildShortCircuitOr(binary);
        }

        // For arithmetic/equality/comparison, semantic analysis has already
        // ensured both operands have the same type. Passing the left type as the
        // right expected type keeps integer literal typing aligned.
        const ValueRef left = buildExpr(*binary.left, expected);
        const ValueRef right = buildExpr(*binary.right, left.type);

        const bool comparison = binary.op == TokenKind::Less ||
                                binary.op == TokenKind::LessEqual ||
                                binary.op == TokenKind::Greater ||
                                binary.op == TokenKind::GreaterEqual ||
                                binary.op == TokenKind::EqualEqual ||
                                binary.op == TokenKind::BangEqual;
        const Type resultType =
            comparison ? Type{.kind = BuiltinTypeKind::Bool} : left.type;
        return appendBinary(binary, left, right, resultType);
    }

    // Emit one unary logical-not (`!`) and return its bool result.
    //
    // Integer `!` is implemented in the backend as `icmp eq v, 0`, so chaining two
    // unary nodes implements C-style truthiness without inventing a dedicated IR
    // opcode.
    ValueRef appendUnaryBang(ValueRef operand, SourceSpan span) {
        Operation op{
            .kind = Operation::Kind::Unary,
            .span = span,
            .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
            .op = TokenKind::Bang,
        };
        op.value = operand;
        const ValueRef result = *op.result;
        append(std::move(op));
        return result;
    }

    // Interpret `v` as a condition, then normalize to an explicit `bool` value.
    //
    // `bool` is already boolean. Integers use double-negation (`!!`) so only zero
    // becomes false, matching `if`/`while` condition rules in semantic analysis.
    ValueRef coerceConditionLikeToBool(ValueRef v, SourceSpan span) {
        if (v.type.kind == BuiltinTypeKind::Bool) {
            return v;
        }
        if (v.type.isInteger()) {
            const ValueRef once = appendUnaryBang(v, span);
            return appendUnaryBang(once, span);
        }
        throw std::logic_error("truthify expected bool or integer operand");
    }

    // Build `expr` inside a fresh block, truthify its value to `bool`, and end
    // with `ReturnValue` for use as an `scf.if` arm (MLIR `scf.yield`).
    std::unique_ptr<Block> buildBoolResultBlock(const Expr& expr) {
        auto block = std::make_unique<Block>(Block{.span = expr.span});
        Block* const outerBlock = currentBlock_;
        currentBlock_ = block.get();
        const ValueRef v = buildExpr(expr, std::nullopt);
        const ValueRef b = coerceConditionLikeToBool(v, expr.span);
        block->terminator = Terminator{
            .kind = Terminator::Kind::ReturnValue,
            .span = expr.span,
            .value = b,
        };
        currentBlock_ = outerBlock;
        return block;
    }

    // `&&`: evaluate LHS first; enter RHS block only if LHS tests true.
    ValueRef buildShortCircuitAnd(const BinaryExpr& binary) {
        const ValueRef leftVal = buildExpr(*binary.left, std::nullopt);

        auto elseBlk = std::make_unique<Block>(Block{.span = binary.span});
        {
            Block* const outerBlock = currentBlock_;
            currentBlock_ = elseBlk.get();
            Operation lit{
                .kind = Operation::Kind::BoolLiteral,
                .span = binary.span,
                .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
                .boolValue = false,
            };
            const ValueRef falseRef = *lit.result;
            append(std::move(lit));
            elseBlk->terminator = Terminator{
                .kind = Terminator::Kind::ReturnValue,
                .span = binary.span,
                .value = falseRef,
            };
            currentBlock_ = outerBlock;
        }

        Operation op{
            .kind = Operation::Kind::ShortCircuitAnd,
            .span = binary.span,
            .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
            .left = leftVal,
        };
        op.thenBlock = buildBoolResultBlock(*binary.right);
        op.elseBlock = std::move(elseBlk);
        const ValueRef result = *op.result;
        append(std::move(op));
        return result;
    }

    // `||`: evaluate LHS first; skip RHS when LHS already tests true.
    ValueRef buildShortCircuitOr(const BinaryExpr& binary) {
        const ValueRef leftVal = buildExpr(*binary.left, std::nullopt);

        auto thenBlk = std::make_unique<Block>(Block{.span = binary.span});
        {
            Block* const outerBlock = currentBlock_;
            currentBlock_ = thenBlk.get();
            Operation lit{
                .kind = Operation::Kind::BoolLiteral,
                .span = binary.span,
                .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
                .boolValue = true,
            };
            const ValueRef trueRef = *lit.result;
            append(std::move(lit));
            thenBlk->terminator = Terminator{
                .kind = Terminator::Kind::ReturnValue,
                .span = binary.span,
                .value = trueRef,
            };
            currentBlock_ = outerBlock;
        }

        Operation op{
            .kind = Operation::Kind::ShortCircuitOr,
            .span = binary.span,
            .result = makeValue(Type{.kind = BuiltinTypeKind::Bool}),
            .left = leftVal,
        };
        op.thenBlock = std::move(thenBlk);
        op.elseBlock = buildBoolResultBlock(*binary.right);
        const ValueRef result = *op.result;
        append(std::move(op));
        return result;
    }

    // Append the actual Binary operation after both operands have been lowered.
    //
    // Splitting this from buildBinary keeps the type decision above separate
    // from the repetitive operation construction below.
    ValueRef appendBinary(const BinaryExpr& binary, ValueRef left, ValueRef right,
                          Type resultType) {
        Operation op{
            .kind = Operation::Kind::Binary,
            .span = binary.span,
            .result = makeValue(resultType),
            .op = binary.op,
        };
        op.left = left;
        op.right = right;
        const ValueRef result = *op.result;
        append(std::move(op));
        return result;
    }

    // Lower a function call expression or call statement.
    //
    // Non-void calls return the ValueRef produced by the call operation. Void
    // calls return std::nullopt because there is no expression result to use.
    // Semantic analysis guarantees the callee exists and arguments type-check.
    std::optional<ValueRef> buildCall(const CallExpr& call) {
        const auto* callee = dynamic_cast<const NameExpr*>(call.callee.get());
        if (!callee) {
            throw std::logic_error("typed IR only supports named callees");
        }

        const auto signature = functions_.find(callee->name);
        if (signature == functions_.end()) {
            throw std::logic_error("typed IR call target was not resolved");
        }

        if (signature->second.isBuiltin &&
            (callee->name == "print" || callee->name == "println")) {
            Operation op{
                .kind = Operation::Kind::Call,
                .span = call.span,
                .text = callee->name,
                .isBuiltin = true,
            };
            for (const std::unique_ptr<Expr>& argument : call.arguments) {
                op.arguments.push_back(buildExpr(*argument, std::nullopt));
            }
            append(std::move(op));
            return std::nullopt;
        }

        Operation op{
            .kind = Operation::Kind::Call,
            .span = call.span,
            .text = callee->name,
            .isBuiltin = signature->second.isBuiltin,
        };

        // Build arguments before allocating the call result so value IDs appear
        // in the same order operations are emitted in the dump.
        const std::size_t count =
            std::min(call.arguments.size(), signature->second.parameterTypes.size());
        for (std::size_t i = 0; i < count; ++i) {
            op.arguments.push_back(
                buildExpr(*call.arguments[i], signature->second.parameterTypes[i]));
        }

        if (!signature->second.returnType.isVoid()) {
            op.result = makeValue(signature->second.returnType);
        }

        const std::optional<ValueRef> result = op.result;
        append(std::move(op));
        return result;
    }

    // Start a new lexical value scope. Source blocks can shadow names from outer
    // blocks, so lookup must search a stack rather than one flat table.
    void pushScope() { scopes_.push_back({}); }

    // End the current lexical value scope. All locals declared in the scope stay
    // in Function::locals for debugging/lowering, but their source names stop
    // being visible after this point.
    void popScope() { scopes_.pop_back(); }

    // Allocate a function-local storage slot and make its source name visible.
    //
    // Parameters and `let` bindings both use LocalRef so later expression
    // lowering can use the same LoadLocal operation for either one.
    LocalRef declareLocal(std::string_view name, SourceSpan span, Type type,
                          bool isMutable, Local::Kind kind) {
        if (!currentFunction_) {
            throw std::logic_error("typed IR locals can only be declared in functions");
        }

        // Local IDs are function-local and stable for the duration of the
        // function. They are assigned when parameters/lets are declared, not
        // when the local is first loaded.
        const LocalRef ref{.id = currentFunction_->locals.size()};
        currentFunction_->locals.push_back(Local{
            .ref = ref,
            .name = std::string(name),
            .type = type,
            .isMutable = isMutable,
            .kind = kind,
            .span = span,
        });
        scopes_.back()[std::string(name)] = ValueSymbol{
            .type = type,
            .local = ref,
            .isConst = false,
        };
        return ref;
    }

    // Resolve a source-level value name to the storage or const it denotes.
    //
    // This should never produce a user-facing "undefined name" diagnostic here:
    // semantic analysis already did that. A miss is an internal compiler bug or
    // a mismatch between semantic analysis and IR construction.
    ValueSymbol lookupValue(const std::string& name) const {
        // Lookup mirrors source lexical scoping: innermost local scope first,
        // then module-level constants. Functions are intentionally not values in
        // nex; calls resolve functions through buildCall().
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto found = scope->find(name); found != scope->end()) {
                return found->second;
            }
        }

        if (const auto found = constants_.find(name); found != constants_.end()) {
            return ValueSymbol{
                .type = found->second,
                .isConst = true,
            };
        }

        throw std::logic_error("typed IR value was not resolved: " + name);
    }

    // Allocate a new temporary value ID in the current function or const body.
    //
    // ValueRef is the IR's name for "the result of some operation." These IDs are
    // intentionally scoped to one body, so every function can have its own `%0`.
    ValueRef makeValue(Type type) {
        std::size_t id = 0;
        if (currentFunction_) {
            id = currentFunction_->nextValueId++;
        } else if (currentConst_) {
            id = currentConst_->nextValueId++;
        } else {
            throw std::logic_error("typed IR value allocated outside a body");
        }
        // Value IDs are local to the body being emitted. A function and a const
        // initializer can both have `%0` because they are separate IR bodies.
        return ValueRef{.id = id, .type = type};
    }

    // Append an operation to whichever block is currently receiving emission.
    //
    // Most builder methods create an Operation locally, fill the payload fields
    // selected by Operation::Kind, and funnel through this helper at the end.
    void append(Operation op) {
        if (!currentBlock_) {
            throw std::logic_error("typed IR operation emitted outside a block");
        }
        // All operation emission funnels through this helper so invalid
        // current-block state fails loudly during development.
        currentBlock_->operations.push_back(std::move(op));
    }

    // Return the source function's declared return type while lowering `return`.
    //
    // This is only valid during function-body emission. Const initializers do not
    // have a current function or a return type.
    Type currentReturnType() const {
        if (!currentFunction_) {
            throw std::logic_error("return type requested outside a function");
        }
        return currentFunction_->returnType;
    }

    // Top-level tables collected before lowering bodies.
    std::unordered_map<std::string, FunctionSignature> functions_;
    std::unordered_map<std::string, Type> constants_;

    // Lexical value scopes for the function currently being built.
    std::vector<std::unordered_map<std::string, ValueSymbol>> scopes_;

    // Current emission targets. Exactly one of currentConst_ or currentFunction_
    // is non-null while values are being emitted, and currentBlock_ points at
    // the block receiving operations.
    Const* currentConst_ = nullptr;
    Function* currentFunction_ = nullptr;
    Block* currentBlock_ = nullptr;
};

} // namespace

// Public entry point for AST -> typed IR conversion.
//
// Keeping the stateful builder private makes the call site simple and prevents
// other compiler stages from depending on temporary details such as currentBlock_
// or the lexical scope stack.
Module buildTypedIr(const TranslationUnit& unit) {
    return TypedIrBuilder().build(unit);
}

} // namespace nexc::ir
