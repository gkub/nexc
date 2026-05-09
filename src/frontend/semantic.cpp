#include "nexc/frontend/semantic.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nexc {

namespace {

// Semantic Type is deliberately separate from TypeSyntax.
//
// TypeSyntax records what the parser saw in source. Type is the semantic pass's
// meaning-level view: it answers questions such as "is this an integer?" and
// "is this void?" without exposing parser details to every check.
struct Type {
    BuiltinTypeKind kind = BuiltinTypeKind::Invalid;

    // Invalid is an error-recovery type. Treating it specially prevents one bad
    // expression from causing a flood of follow-up type errors.
    bool isInvalid() const { return kind == BuiltinTypeKind::Invalid; }

    // Void is allowed as a function return type, but not as a first-class value.
    bool isVoid() const { return kind == BuiltinTypeKind::Void; }

    // Bool is a distinct Core v0 scalar type, even though lowering later maps it
    // to MLIR i1.
    bool isBool() const { return kind == BuiltinTypeKind::Bool; }

    // String literals currently have type str for semantic built-in printing.
    bool isString() const { return kind == BuiltinTypeKind::Str; }

    // Return true for fixed-width signed/unsigned integer types.
    //
    // Bool is intentionally not treated as an integer here. Conditions accept
    // bool and integers, but arithmetic requires real integer types.
    bool isInteger() const {
        switch (kind) {
        case BuiltinTypeKind::I8:
        case BuiltinTypeKind::I16:
        case BuiltinTypeKind::I32:
        case BuiltinTypeKind::I64:
        case BuiltinTypeKind::U8:
        case BuiltinTypeKind::U16:
        case BuiltinTypeKind::U32:
        case BuiltinTypeKind::U64:
            return true;
        case BuiltinTypeKind::Bool:
        case BuiltinTypeKind::Str:
        case BuiltinTypeKind::Void:
        case BuiltinTypeKind::Invalid:
            return false;
        }

        return false;
    }
};

// Compare two semantic types for exact equality, with invalid acting as a
// recovery wildcard.
bool sameType(Type left, Type right) {
    // Invalid types are treated as compatible so one earlier error does not
    // cascade into many redundant "type mismatch" diagnostics.
    return left.kind == right.kind || left.isInvalid() || right.isInvalid();
}

// Convert a semantic type to the spelling used in diagnostics.
std::string typeName(Type type) {
    return std::string(builtinTypeName(type.kind));
}

// Convert parser type syntax into semantic type information.
//
// This is tiny today because Core v0 only has built-in scalar types. Keeping the
// conversion explicit gives future user-defined types a clear expansion point.
Type typeFromSyntax(TypeSyntax syntax) {
    return Type{.kind = syntax.kind};
}

// Return true if a type is allowed in `if`, `while`, and `!` condition contexts.
//
// Core v0 intentionally follows C-like integer truthiness only for integer and
// bool values. Strings and void are not condition-like.
bool canBeCondition(Type type) {
    // Core v0 allows bool and integer conditions. Invalid is accepted here only
    // to avoid cascading diagnostics after an expression already failed.
    return type.isBool() || type.isInteger() || type.isInvalid();
}

// FunctionSymbol is the semantic table entry for a function name. It stores only
// the signature facts needed to check calls and `main`.
struct FunctionSymbol {
    SourceSpan nameSpan;
    std::vector<Type> parameterTypes;
    Type returnType;
    bool isBuiltin = false;
};

// ValueSymbol is the semantic table entry for a value-like name: locals,
// parameters, and module constants. Functions intentionally live in a separate
// table because Core v0 does not let functions be used as first-class values.
struct ValueSymbol {
    Type type;
    bool isMutable = false;
    bool isConst = false;
    SourceSpan nameSpan;
};

// ExprInfo is the result of semantically analyzing an expression.
//
// It carries the expression type and the tiny amount of constant-evaluation
// state needed for current Core v0 checks. This is not yet a full constant-value
// model; it only tracks unsigned integer payloads where that is enough.
struct ExprInfo {
    Type type;
    bool isConstant = false;
    std::optional<unsigned long long> integerValue = std::nullopt;
};

// Parse the raw spelling of an integer literal into an unsigned payload.
//
// This helper is deliberately unsigned because the parser represents unary minus
// as a separate UnaryExpr. A source spelling like `-1` is not one negative token;
// it is `Minus` plus integer literal `1`.
std::optional<unsigned long long> parseUnsignedInteger(std::string_view raw) {
    // The lexer already validated the surface spelling. Semantic analysis parses
    // the value so it can check type ranges and simple constant expressions.
    int base = 10;
    if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) {
        raw.remove_prefix(2);
        base = 16;
    }

    unsigned long long value = 0;
    const char* begin = raw.data();
    const char* end = raw.data() + raw.size();
    const std::from_chars_result result = std::from_chars(begin, end, value, base);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

// Return the number of bits in a fixed-width integer type.
//
// Non-integer types return 0 so callers can use that as "not applicable" after
// checking type.isInteger().
unsigned bitWidth(Type type) {
    // Integer widths are needed for literal range checks and constant overflow
    // diagnostics. Non-integer types return 0 because they have no integer range.
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

// Return whether an integer type is signed.
//
// Signedness is needed for literal range and overflow checks because i8 and u8
// have the same bit width but different maximum positive literal values.
bool isSigned(Type type) {
    // Signedness only matters for integer types. Returning false for non-integers
    // keeps helper code simple after callers have already checked isInteger().
    switch (type.kind) {
    case BuiltinTypeKind::I8:
    case BuiltinTypeKind::I16:
    case BuiltinTypeKind::I32:
    case BuiltinTypeKind::I64:
        return true;
    case BuiltinTypeKind::U8:
    case BuiltinTypeKind::U16:
    case BuiltinTypeKind::U32:
    case BuiltinTypeKind::U64:
    case BuiltinTypeKind::Bool:
    case BuiltinTypeKind::Str:
    case BuiltinTypeKind::Void:
    case BuiltinTypeKind::Invalid:
        return false;
    }

    return false;
}

// Return the maximum non-negative literal value that fits in an integer type.
//
// This first semantic pass tracks only unsigned literal payloads, so signed
// types use their positive range here. Negative signed constants are represented
// as unary minus plus a positive literal and need richer evaluation later.
unsigned long long maxIntegerValue(Type type) {
    const unsigned width = bitWidth(type);
    if (width == 0) {
        return 0;
    }

    if (width == 64 && !isSigned(type)) {
        return std::numeric_limits<unsigned long long>::max();
    }

    // Signed Core v0 integers use one sign bit, so the positive literal range is
    // 2^(width - 1) - 1. Unsigned integers use all bits for the value.
    const unsigned valueBits = isSigned(type) ? width - 1 : width;
    return (1ULL << valueBits) - 1;
}

// AnalyzerImpl owns one semantic-analysis run.
//
// The public SemanticAnalyzer class is a thin stable API. Keeping the mutable
// implementation here lets the pass use scoped symbol tables and current
// function state without exposing those details in the header.
class AnalyzerImpl {
public:
    // Create one analysis run over a translation unit.
    //
    // The analyzer borrows the DiagnosticBag so all semantic errors join lexer
    // and parser diagnostics in the same reporting path.
    explicit AnalyzerImpl(DiagnosticBag& diagnostics) : diagnostics_(diagnostics) {}

    // Analyze a full AST translation unit.
    //
    // This is the main pass driver. It intentionally separates declaration
    // collection from body checking so functions can call declarations that
    // appear later in the file.
    void analyze(const TranslationUnit& unit) {
        // Analysis has two broad phases:
        //
        // 1. collect top-level names and signatures so calls can reference
        //    functions declared later in the file
        // 2. analyze constant/function bodies using those tables
        installBuiltins();
        collectItems(unit);
        validateMainIfPresent();

        for (const std::unique_ptr<Item>& item : unit.items) {
            if (const auto* constant = dynamic_cast<const ConstDecl*>(item.get())) {
                analyzeConstDecl(*constant);
            } else if (const auto* function =
                           dynamic_cast<const FunctionDecl*>(item.get())) {
                analyzeFunction(*function);
            }
        }
    }

private:
    // Install compiler-provided functions before collecting user declarations.
    //
    // Built-ins participate in normal call checking but cannot be redefined by
    // source code.
    void installBuiltins() {
        const Type str{.kind = BuiltinTypeKind::Str};
        const Type i32{.kind = BuiltinTypeKind::I32};
        const Type u64{.kind = BuiltinTypeKind::U64};
        const Type boolType{.kind = BuiltinTypeKind::Bool};
        const Type voidType{.kind = BuiltinTypeKind::Void};
        const SourceSpan builtinSpan{};

        // Built-ins enter the same function table as user functions so call
        // checking can be uniform. `isBuiltin` lets us reject source attempts to
        // redefine them.
        functions_["print"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {str},
            .returnType = voidType,
            .isBuiltin = true,
        };
        functions_["println"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {str},
            .returnType = voidType,
            .isBuiltin = true,
        };
        functions_["readln"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {},
            .returnType = str,
            .isBuiltin = true,
        };
        functions_["parse_i32"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {str},
            .returnType = i32,
            .isBuiltin = true,
        };
        functions_["parse_u64"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {str},
            .returnType = u64,
            .isBuiltin = true,
        };
        functions_["parse_bool"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {str},
            .returnType = boolType,
            .isBuiltin = true,
        };
        functions_["input_ok"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {},
            .returnType = boolType,
            .isBuiltin = true,
        };
    }

    // Collect top-level function signatures and constants.
    //
    // This phase does not analyze bodies or initializer expressions. It only
    // builds symbol tables needed for later checks and detects duplicate names.
    void collectItems(const TranslationUnit& unit) {
        // This pass is intentionally shallow: inspect declarations and record
        // names/signatures, but do not analyze initializer or body expressions
        // yet. That supports forward function calls.
        for (const std::unique_ptr<Item>& item : unit.items) {
            if (const auto* function = dynamic_cast<const FunctionDecl*>(item.get())) {
                declareTopLevel(function->name, function->nameSpan);
                if (const auto builtin = functions_.find(function->name);
                    builtin != functions_.end() && builtin->second.isBuiltin) {
                    diagnostics_.error(function->nameSpan,
                                       "cannot redefine built-in function `" +
                                           function->name + "`");
                    continue;
                }

                std::vector<Type> parameterTypes;
                parameterTypes.reserve(function->parameters.size());
                for (const ParameterSyntax& parameter : function->parameters) {
                    parameterTypes.push_back(typeFromSyntax(parameter.type));
                }

                functions_[function->name] = FunctionSymbol{
                    .nameSpan = function->nameSpan,
                    .parameterTypes = std::move(parameterTypes),
                    .returnType = typeFromSyntax(function->returnType),
                };
                continue;
            }

            if (const auto* constant = dynamic_cast<const ConstDecl*>(item.get())) {
                declareTopLevel(constant->name, constant->nameSpan);
                globals_[constant->name] = ValueSymbol{
                    .type = typeFromSyntax(constant->type),
                    .isMutable = false,
                    .isConst = true,
                    .nameSpan = constant->nameSpan,
                };
            }
        }
    }

    // Record a top-level name in the module namespace.
    //
    // Core v0 uses one namespace for functions and module constants, so `fn foo`
    // and `const foo` conflict.
    void declareTopLevel(const std::string& name, SourceSpan span) {
        // Functions and constants share one module namespace in Core v0.
        if (topLevelNames_.contains(name)) {
            diagnostics_.error(span, "duplicate top-level name `" + name + "`");
            diagnostics_.note(topLevelNames_[name],
                              "previous declaration of `" + name + "` is here");
            return;
        }
        topLevelNames_[name] = span;
    }

    // Check the special executable entry function if the file defines one.
    //
    // Files without `main` are allowed as library units. Files with `main` must
    // use the narrow Core v0 executable signatures.
    void validateMainIfPresent() {
        // `main` is optional because a source file may be a library unit. If it
        // exists, Core v0 restricts its shape so future execution has a clear
        // entry convention.
        const auto it = functions_.find("main");
        if (it == functions_.end()) {
            return;
        }

        const FunctionSymbol& main = it->second;
        if (!main.parameterTypes.empty()) {
            diagnostics_.error(main.nameSpan, "`main` must not have parameters in Core v0");
        }

        if (main.returnType.kind != BuiltinTypeKind::Void &&
            main.returnType.kind != BuiltinTypeKind::I32) {
            diagnostics_.error(main.nameSpan,
                               "`main` must return `void` or `i32` in Core v0");
        }
    }

    // Analyze one module-level constant declaration.
    //
    // A const initializer must type-check against the declared type and be
    // compile-time evaluable according to the current small constant evaluator.
    void analyzeConstDecl(const ConstDecl& constant) {
        // Module consts must be type-correct and compile-time evaluable. The
        // current evaluator is deliberately small but already catches literals,
        // booleans, strings, and basic integer arithmetic cases.
        ExprInfo init = analyzeExpr(*constant.init, typeFromSyntax(constant.type));
        if (!sameType(typeFromSyntax(constant.type), init.type)) {
            diagnostics_.error(constant.init->span,
                               "cannot initialize constant `" + constant.name +
                                   "` of type `" + typeName(typeFromSyntax(constant.type)) +
                                   "` with value of type `" + typeName(init.type) + "`");
        }
        if (!init.isConstant) {
            diagnostics_.error(constant.init->span,
                               "constant initializer for `" + constant.name +
                                   "` must be compile-time evaluable");
        }
    }

    // Analyze one function body.
    //
    // This sets up parameter locals, checks every statement, and verifies that a
    // non-void function definitely returns along all structured paths.
    void analyzeFunction(const FunctionDecl& function) {
        // Function analysis resets per-function state: return type, local scopes,
        // and return-path tracking.
        currentReturnType_ = typeFromSyntax(function.returnType);
        sawReturnValue_ = false;
        scopes_.clear();
        pushScope();

        for (const ParameterSyntax& parameter : function.parameters) {
            // Parameters behave like immutable locals inside the function body.
            declareLocal(parameter.name, parameter.nameSpan, typeFromSyntax(parameter.type),
                         false);
        }

        const bool allPathsReturn = analyzeStmt(*function.body);

        if (!currentReturnType_.isVoid() && !allPathsReturn) {
            // This is a simple structured return-path check. It knows that a
            // block returns if some child returns, and an if returns only if both
            // branches return. It does not yet reason about loop conditions.
            diagnostics_.error(function.nameSpan,
                               "not all paths in function `" + function.name +
                                   "` return a value of type `" +
                                   typeName(currentReturnType_) + "`");
        }

        popScope();
    }

    // Start a new lexical local scope.
    //
    // Blocks and function bodies use this to make shadowing/local lifetime match
    // source nesting.
    void pushScope() { scopes_.push_back({}); }

    // End the current lexical local scope.
    void popScope() { scopes_.pop_back(); }

    // Declare a local or parameter in the current scope.
    //
    // Mutability is stored with the symbol because assignment checking needs to
    // know whether `x = value;` is allowed.
    void declareLocal(const std::string& name, SourceSpan span, Type type,
                      bool isMutable) {
        // Duplicates are only rejected within the current lexical scope. Shadowing
        // an outer local is allowed by this implementation unless the language
        // spec later forbids it.
        auto& scope = scopes_.back();
        if (scope.contains(name)) {
            diagnostics_.error(span, "duplicate local name `" + name + "`");
            diagnostics_.note(scope[name].nameSpan,
                              "previous declaration of `" + name + "` is here");
            return;
        }

        scope[name] = ValueSymbol{
            .type = type,
            .isMutable = isMutable,
            .isConst = false,
            .nameSpan = span,
        };
    }

    // Resolve a value-like name in lexical scopes, then module constants.
    //
    // Returns nullptr for an unresolved name so callers can issue a diagnostic
    // tailored to the context.
    const ValueSymbol* lookupValue(const std::string& name) const {
        // Lexical lookup walks from innermost scope outward, then falls back to
        // module constants. Function names are handled separately by call
        // analysis because functions are not values in Core v0.
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto it = scope->find(name); it != scope->end()) {
                return &it->second;
            }
        }

        if (const auto it = globals_.find(name); it != globals_.end()) {
            return &it->second;
        }

        return nullptr;
    }

    // Analyze one statement and report whether it definitely returns.
    //
    // The bool return is for return-path analysis only. It does not mean the
    // statement is valid/invalid; diagnostics are emitted into diagnostics_.
    bool analyzeStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
            // Blocks introduce scopes. The returned bool summarizes whether
            // control flow definitely returns somewhere in this structured block.
            pushScope();
            bool blockReturns = false;
            for (const std::unique_ptr<Stmt>& child : block->statements) {
                const bool childReturns = analyzeStmt(*child);
                blockReturns = blockReturns || childReturns;
            }
            popScope();
            return blockReturns;
        }

        if (const auto* let = dynamic_cast<const LetStmt*>(&stmt)) {
            // Analyze the initializer before declaring the local, so `let x: i32
            // = x;` does not accidentally refer to the binding being declared.
            const Type declared = typeFromSyntax(let->type);
            ExprInfo init = analyzeExpr(*let->init, declared);
            if (!sameType(declared, init.type)) {
                diagnostics_.error(let->init->span,
                                   "cannot initialize local `" + let->name +
                                       "` of type `" + typeName(declared) +
                                       "` with value of type `" + typeName(init.type) +
                                       "`");
            }
            declareLocal(let->name, let->nameSpan, declared, let->isMutable);
            return false;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            // Assignment checks three separate semantic facts: the name exists,
            // the binding is mutable, and the assigned value has the right type.
            const ValueSymbol* symbol = lookupValue(assign->name);
            if (!symbol) {
                diagnostics_.error(assign->nameSpan,
                                   "undefined local `" + assign->name + "`");
                analyzeExpr(*assign->value, std::nullopt);
                return false;
            }
            if (!symbol->isMutable) {
                diagnostics_.error(assign->nameSpan,
                                   "cannot assign to immutable binding `" +
                                       assign->name + "`");
            }
            ExprInfo value = analyzeExpr(*assign->value, symbol->type);
            if (!sameType(symbol->type, value.type)) {
                diagnostics_.error(assign->value->span,
                                   "cannot assign value of type `" +
                                       typeName(value.type) + "` to `" + assign->name +
                                       "` of type `" + typeName(symbol->type) + "`");
            }
            return false;
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            analyzeReturn(*ret);
            return true;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            // For return-path analysis, an if expression returns only when both
            // branches exist and both branches return.
            analyzeCondition(*ifStmt->condition, "`if` condition");
            const bool thenReturns = analyzeStmt(*ifStmt->thenBranch);
            bool elseReturns = false;
            if (ifStmt->elseBranch) {
                elseReturns = analyzeStmt(*ifStmt->elseBranch);
            }
            return ifStmt->elseBranch && thenReturns && elseReturns;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            // This first analyzer does not prove loops execute, so while never
            // counts as a guaranteed return path.
            analyzeCondition(*whileStmt->condition, "`while` condition");
            analyzeStmt(*whileStmt->body);
            return false;
        }

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
            // Parser syntax allows any call expression as a statement. Semantic
            // analysis enforces the Core v0 rule that only void call results may
            // be discarded.
            ExprInfo call = analyzeCallExpr(*callStmt->call);
            if (!call.type.isVoid() && !call.type.isInvalid()) {
                diagnostics_.error(callStmt->span,
                                   "cannot discard value of type `" + typeName(call.type) +
                                       "`; only `void` calls may be statements");
            }
            return false;
        }

        return false;
    }

    // Check a return statement against the current function return type.
    //
    // The function context is stored in currentReturnType_ while analyzeFunction
    // walks the body.
    void analyzeReturn(const ReturnStmt& ret) {
        if (!ret.value) {
            // Bare `return;` is only valid in void functions.
            if (!currentReturnType_.isVoid()) {
                diagnostics_.error(ret.span,
                                   "non-void function must return a value of type `" +
                                       typeName(currentReturnType_) + "`");
            }
            return;
        }

        if (currentReturnType_.isVoid()) {
            // A void function may return, but it may not return a value.
            diagnostics_.error(ret.value->span,
                               "`void` function cannot return a value");
            analyzeExpr(*ret.value, std::nullopt);
            return;
        }

        sawReturnValue_ = true;
        ExprInfo value = analyzeExpr(*ret.value, currentReturnType_);
        if (!sameType(currentReturnType_, value.type)) {
            diagnostics_.error(ret.value->span,
                               "cannot return value of type `" + typeName(value.type) +
                                   "` from function returning `" +
                                   typeName(currentReturnType_) + "`");
        }
    }

    // Analyze an expression used as a condition.
    //
    // Conditions are special because Core v0 accepts both bool and integers
    // there. Passing no expected type lets integer literals default before the
    // condition-kind check runs.
    void analyzeCondition(const Expr& condition, std::string_view label) {
        // Conditions deliberately do not pass an expected type. Integer and bool
        // are both accepted in Core v0, so the expression should choose its
        // natural/default type before canBeCondition checks it.
        ExprInfo info = analyzeExpr(condition, std::nullopt);
        if (!canBeCondition(info.type)) {
            diagnostics_.error(condition.span,
                               std::string(label) +
                                   " must be `bool` or an integer type, not `" +
                                   typeName(info.type) + "`");
        }
    }

    // Analyze one expression and return its semantic facts.
    //
    // expected is a contextual type from declarations, returns, or call
    // arguments. It is especially important for unsuffixed integer literals.
    ExprInfo analyzeExpr(const Expr& expr, std::optional<Type> expected) {
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
            // Unsuffixed integer literals get their type from context when there
            // is one, otherwise they default to i32 for Core v0.
            Type type = expected.value_or(Type{.kind = BuiltinTypeKind::I32});
            if (!type.isInteger()) {
                diagnostics_.error(expr.span,
                                   "integer literal cannot be used as `" +
                                       typeName(type) + "`");
                return ExprInfo{.type = Type{}, .isConstant = true};
            }
            checkIntegerLiteralRange(*integer, type);
            return ExprInfo{
                .type = type,
                .isConstant = true,
                .integerValue = parseUnsignedInteger(integer->raw),
            };
        }

        if (const auto* boolean = dynamic_cast<const BoolLiteralExpr*>(&expr)) {
            (void)boolean;
            return ExprInfo{
                .type = Type{.kind = BuiltinTypeKind::Bool},
                .isConstant = true,
                .integerValue = boolean->value ? 1ULL : 0ULL,
            };
        }

        if (const auto* string = dynamic_cast<const StringLiteralExpr*>(&expr)) {
            (void)string;
            return ExprInfo{
                .type = Type{.kind = BuiltinTypeKind::Str},
                .isConstant = true,
            };
        }

        if (const auto* name = dynamic_cast<const NameExpr*>(&expr)) {
            // A bare name must resolve to a value-like symbol. If it matches a
            // function, report that more specific mistake instead of a generic
            // undefined-name error.
            const ValueSymbol* symbol = lookupValue(name->name);
            if (!symbol) {
                if (functions_.contains(name->name)) {
                    diagnostics_.error(name->span,
                                       "function `" + name->name +
                                           "` cannot be used as a value");
                } else {
                    diagnostics_.error(name->span,
                                       "undefined name `" + name->name + "`");
                }
                return ExprInfo{.type = Type{}, .isConstant = false};
            }
            return ExprInfo{.type = symbol->type, .isConstant = symbol->isConst};
        }

        if (const auto* call = dynamic_cast<const CallExpr*>(&expr)) {
            return analyzeCallExpr(*call);
        }

        if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            return analyzeUnaryExpr(*unary, expected);
        }

        if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            return analyzeBinaryExpr(*binary, expected);
        }

        if (const auto* paren = dynamic_cast<const ParenExpr*>(&expr)) {
            // Parentheses affect parsing but not semantic type, so forward the
            // expected type into the inner expression.
            return analyzeExpr(*paren->inner, expected);
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    // Analyze a direct function call expression.
    //
    // This resolves the callee name, checks argument count and argument types,
    // and returns the function's result type.
    ExprInfo analyzeCallExpr(const CallExpr& call) {
        const auto* callee = dynamic_cast<const NameExpr*>(call.callee.get());
        if (!callee) {
            // The AST can represent a general callee expression, but Core v0
            // only allows direct calls by function name.
            diagnostics_.error(call.callee->span,
                               "callee must be a function name in Core v0");
            for (const std::unique_ptr<Expr>& argument : call.arguments) {
                analyzeExpr(*argument, std::nullopt);
            }
            return ExprInfo{.type = Type{}, .isConstant = false};
        }

        const auto function = functions_.find(callee->name);
        if (function == functions_.end()) {
            // Still analyze arguments after an unknown callee so diagnostics
            // inside the argument expressions are not hidden.
            diagnostics_.error(callee->span,
                               "undefined function `" + callee->name + "`");
            for (const std::unique_ptr<Expr>& argument : call.arguments) {
                analyzeExpr(*argument, std::nullopt);
            }
            return ExprInfo{.type = Type{}, .isConstant = false};
        }

        const FunctionSymbol& symbol = function->second;
        if (call.arguments.size() != symbol.parameterTypes.size()) {
            diagnostics_.error(call.span,
                               "function `" + callee->name + "` expects " +
                                   std::to_string(symbol.parameterTypes.size()) +
                                   " argument(s), got " +
                                   std::to_string(call.arguments.size()));
        }

        const std::size_t count =
            std::min(call.arguments.size(), symbol.parameterTypes.size());
        for (std::size_t i = 0; i < count; ++i) {
            // Passing each parameter type as the expected expression type lets
            // integer literals be checked in the context of the function
            // signature, e.g. f(u8) constrains `f(255)`.
            const Type expected = symbol.parameterTypes[i];
            ExprInfo actual = analyzeExpr(*call.arguments[i], expected);
            if (!sameType(expected, actual.type)) {
                diagnostics_.error(call.arguments[i]->span,
                                   "argument " + std::to_string(i + 1) +
                                       " to `" + callee->name + "` has type `" +
                                       typeName(actual.type) + "`, expected `" +
                                       typeName(expected) + "`");
            }
        }

        for (std::size_t i = count; i < call.arguments.size(); ++i) {
            analyzeExpr(*call.arguments[i], std::nullopt);
        }

        return ExprInfo{.type = symbol.returnType, .isConstant = false};
    }

    // Analyze a unary expression.
    //
    // `!` is condition-like and always produces bool. Unary `-` is integer-only
    // and keeps the operand type.
    ExprInfo analyzeUnaryExpr(const UnaryExpr& unary, std::optional<Type> expected) {
        if (unary.op == TokenKind::Bang) {
            // `!` always produces bool. Core v0 accepts either bool or integer
            // operands as condition-like values.
            ExprInfo operand = analyzeExpr(*unary.operand, std::nullopt);
            if (!canBeCondition(operand.type)) {
                diagnostics_.error(unary.operand->span,
                                   "`!` operand must be `bool` or integer, not `" +
                                       typeName(operand.type) + "`");
            }
            return ExprInfo{.type = Type{.kind = BuiltinTypeKind::Bool},
                            .isConstant = operand.isConstant};
        }

        if (unary.op == TokenKind::Minus) {
            // Unary minus defaults integer literals to i32 unless an outer
            // expression or declaration provides a more specific expected type.
            Type expectedInteger = expected.value_or(Type{.kind = BuiltinTypeKind::I32});
            ExprInfo operand = analyzeExpr(*unary.operand, expectedInteger);
            if (!operand.type.isInteger()) {
                diagnostics_.error(unary.operand->span,
                                   "`-` operand must be an integer type, not `" +
                                       typeName(operand.type) + "`");
            }
            return ExprInfo{.type = operand.type, .isConstant = operand.isConstant};
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    // Analyze a binary expression.
    //
    // This is where operator-specific type rules live: arithmetic requires
    // integers and returns the operand type; comparison/equality returns bool;
    // logical operators accept condition-like operands and return bool.
    ExprInfo analyzeBinaryExpr(const BinaryExpr& binary, std::optional<Type> expected) {
        if (binary.op == TokenKind::AmpAmp || binary.op == TokenKind::PipePipe) {
            // Logical binary operators are condition-like on both sides and
            // always produce bool.
            ExprInfo left = analyzeExpr(*binary.left, std::nullopt);
            ExprInfo right = analyzeExpr(*binary.right, std::nullopt);
            if (!canBeCondition(left.type)) {
                diagnostics_.error(binary.left->span,
                                   "left operand must be `bool` or integer");
            }
            if (!canBeCondition(right.type)) {
                diagnostics_.error(binary.right->span,
                                   "right operand must be `bool` or integer");
            }
            return ExprInfo{.type = Type{.kind = BuiltinTypeKind::Bool},
                            .isConstant = left.isConstant && right.isConstant};
        }

        // For arithmetic/comparison/equality, infer/check the left side first,
        // then use its type as context for the right. This keeps literals like
        // `x + 1` typed consistently with `x`.
        ExprInfo left = analyzeExpr(*binary.left, expected);
        ExprInfo right = analyzeExpr(*binary.right, left.type);

        const bool arithmetic = binary.op == TokenKind::Plus ||
                                binary.op == TokenKind::Minus ||
                                binary.op == TokenKind::Star ||
                                binary.op == TokenKind::Slash ||
                                binary.op == TokenKind::Percent;
        const bool comparison = binary.op == TokenKind::Less ||
                                binary.op == TokenKind::LessEqual ||
                                binary.op == TokenKind::Greater ||
                                binary.op == TokenKind::GreaterEqual;
        const bool equality = binary.op == TokenKind::EqualEqual ||
                              binary.op == TokenKind::BangEqual;

        if (arithmetic || comparison) {
            if (!left.type.isInteger()) {
                diagnostics_.error(binary.left->span,
                                   "left operand must be an integer type");
            }
            if (!right.type.isInteger()) {
                diagnostics_.error(binary.right->span,
                                   "right operand must be an integer type");
            }
        }

        if (!sameType(left.type, right.type)) {
            diagnostics_.error(binary.span,
                               "binary operands must have the same type, got `" +
                                   typeName(left.type) + "` and `" +
                                   typeName(right.type) + "`");
        }

        if (arithmetic) {
            // Arithmetic keeps the operand type. If both sides are constant, the
            // helper below also performs the small Core v0 overflow checks.
            return analyzeConstantArithmetic(binary, left, right);
        }
        if (comparison || equality) {
            // Comparisons and equality produce bool even when their operands are
            // integers.
            return ExprInfo{.type = Type{.kind = BuiltinTypeKind::Bool},
                            .isConstant = left.isConstant && right.isConstant};
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    // Evaluate enough constant arithmetic to diagnose obvious Core v0 errors.
    //
    // This helper returns normal ExprInfo either way. Diagnostics record overflow
    // or division-by-zero; the compiler can continue analyzing the rest of the
    // file after reporting them.
    ExprInfo analyzeConstantArithmetic(const BinaryExpr& binary, ExprInfo left,
                                       ExprInfo right) {
        // This is intentionally not a full constant evaluator. It only evaluates
        // simple unsigned integer payloads far enough to diagnose overflow and
        // division/remainder by zero in current Core v0 tests.
        ExprInfo result{
            .type = left.type,
            .isConstant = left.isConstant && right.isConstant,
        };

        if (!result.isConstant || !left.integerValue || !right.integerValue ||
            !left.type.isInteger()) {
            return result;
        }

        const unsigned long long lhs = *left.integerValue;
        const unsigned long long rhs = *right.integerValue;
        const unsigned long long max = maxIntegerValue(left.type);

        auto overflow = [&]() {
            // Report overflow at the whole binary expression because the problem
            // is created by the operation, not by either operand alone.
            diagnostics_.error(binary.span,
                               "constant expression overflows type `" +
                                   typeName(left.type) + "`");
        };

        switch (binary.op) {
        case TokenKind::Plus:
            if (rhs > max || lhs > max - rhs) {
                overflow();
                return result;
            }
            result.integerValue = lhs + rhs;
            return result;
        case TokenKind::Minus:
            if (lhs < rhs) {
                // Negative constant values need a slightly richer value model
                // than this first pass stores. Unsigned underflow is definitely
                // overflow; signed negative results are left for the next
                // constant-evaluation tightening pass.
                if (!isSigned(left.type)) {
                    overflow();
                }
                return result;
            }
            result.integerValue = lhs - rhs;
            return result;
        case TokenKind::Star:
            if (rhs != 0 && lhs > max / rhs) {
                overflow();
                return result;
            }
            result.integerValue = lhs * rhs;
            return result;
        case TokenKind::Slash:
            if (rhs == 0) {
                diagnostics_.error(binary.right->span,
                                   "division by zero in constant expression");
                return result;
            }
            result.integerValue = lhs / rhs;
            return result;
        case TokenKind::Percent:
            if (rhs == 0) {
                diagnostics_.error(binary.right->span,
                                   "remainder by zero in constant expression");
                return result;
            }
            result.integerValue = lhs % rhs;
            return result;
        default:
            return result;
        }
    }

    // Check that an integer literal fits in its selected semantic type.
    //
    // The selected type comes from context or the i32 default, so this check must
    // happen in semantic analysis rather than lexing.
    void checkIntegerLiteralRange(const IntegerLiteralExpr& literal, Type type) {
        // Literal range checks use the selected semantic type. The same spelling
        // can be valid in one context and invalid in another, e.g. `255` for u8
        // vs i8.
        const std::optional<unsigned long long> value =
            parseUnsignedInteger(literal.raw);
        if (!value) {
            return;
        }

        if (*value > maxIntegerValue(type)) {
            diagnostics_.error(literal.span,
                               "integer literal `" + literal.raw +
                                   "` does not fit in type `" + typeName(type) +
                                   "`");
        }
    }

    DiagnosticBag& diagnostics_;

    // Top-level namespace shared by functions and module constants.
    std::unordered_map<std::string, SourceSpan> topLevelNames_;

    // Function and global value tables collected before body analysis.
    std::unordered_map<std::string, FunctionSymbol> functions_;
    std::unordered_map<std::string, ValueSymbol> globals_;

    // Lexical local scopes for the function currently being analyzed.
    std::vector<std::unordered_map<std::string, ValueSymbol>> scopes_;

    // Current function state used while analyzing return statements.
    Type currentReturnType_;
    bool sawReturnValue_ = false;
};

} // namespace

// Create the public semantic analyzer wrapper.
//
// The SourceFile parameter is kept in the API for symmetry with lexer/parser and
// future diagnostics, even though the current implementation only needs the bag.
SemanticAnalyzer::SemanticAnalyzer(const SourceFile&,
                                   DiagnosticBag& diagnostics)
    : diagnostics_(diagnostics) {}

// Run semantic analysis over a parsed AST.
//
// All mutable analysis state is kept in AnalyzerImpl so this public class remains
// a small stable facade for the CLI and later tools.
void SemanticAnalyzer::analyze(const TranslationUnit& unit) {
    AnalyzerImpl(diagnostics_).analyze(unit);
}

} // namespace nexc
