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

struct Type {
    BuiltinTypeKind kind = BuiltinTypeKind::Invalid;

    bool isInvalid() const { return kind == BuiltinTypeKind::Invalid; }
    bool isVoid() const { return kind == BuiltinTypeKind::Void; }
    bool isBool() const { return kind == BuiltinTypeKind::Bool; }
    bool isString() const { return kind == BuiltinTypeKind::Str; }

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

bool sameType(Type left, Type right) {
    return left.kind == right.kind || left.isInvalid() || right.isInvalid();
}

std::string typeName(Type type) {
    return std::string(builtinTypeName(type.kind));
}

Type typeFromSyntax(TypeSyntax syntax) {
    return Type{.kind = syntax.kind};
}

bool canBeCondition(Type type) {
    return type.isBool() || type.isInteger() || type.isInvalid();
}

struct FunctionSymbol {
    SourceSpan nameSpan;
    std::vector<Type> parameterTypes;
    Type returnType;
    bool isBuiltin = false;
};

struct ValueSymbol {
    Type type;
    bool isMutable = false;
    bool isConst = false;
    SourceSpan nameSpan;
};

struct ExprInfo {
    Type type;
    bool isConstant = false;
    std::optional<unsigned long long> integerValue = std::nullopt;
};

std::optional<unsigned long long> parseUnsignedInteger(std::string_view raw) {
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

unsigned bitWidth(Type type) {
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

bool isSigned(Type type) {
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

unsigned long long maxIntegerValue(Type type) {
    const unsigned width = bitWidth(type);
    if (width == 0) {
        return 0;
    }

    if (width == 64 && !isSigned(type)) {
        return std::numeric_limits<unsigned long long>::max();
    }

    const unsigned valueBits = isSigned(type) ? width - 1 : width;
    return (1ULL << valueBits) - 1;
}

class AnalyzerImpl {
public:
    explicit AnalyzerImpl(DiagnosticBag& diagnostics) : diagnostics_(diagnostics) {}

    void analyze(const TranslationUnit& unit) {
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
    void installBuiltins() {
        const Type str{.kind = BuiltinTypeKind::Str};
        const Type voidType{.kind = BuiltinTypeKind::Void};
        const SourceSpan builtinSpan{};

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
    }

    void collectItems(const TranslationUnit& unit) {
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

    void declareTopLevel(const std::string& name, SourceSpan span) {
        if (topLevelNames_.contains(name)) {
            diagnostics_.error(span, "duplicate top-level name `" + name + "`");
            diagnostics_.note(topLevelNames_[name],
                              "previous declaration of `" + name + "` is here");
            return;
        }
        topLevelNames_[name] = span;
    }

    void validateMainIfPresent() {
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

    void analyzeConstDecl(const ConstDecl& constant) {
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

    void analyzeFunction(const FunctionDecl& function) {
        currentReturnType_ = typeFromSyntax(function.returnType);
        sawReturnValue_ = false;
        scopes_.clear();
        pushScope();

        for (const ParameterSyntax& parameter : function.parameters) {
            declareLocal(parameter.name, parameter.nameSpan, typeFromSyntax(parameter.type),
                         false);
        }

        const bool allPathsReturn = analyzeStmt(*function.body);

        if (!currentReturnType_.isVoid() && !allPathsReturn) {
            diagnostics_.error(function.nameSpan,
                               "not all paths in function `" + function.name +
                                   "` return a value of type `" +
                                   typeName(currentReturnType_) + "`");
        }

        popScope();
    }

    void pushScope() { scopes_.push_back({}); }
    void popScope() { scopes_.pop_back(); }

    void declareLocal(const std::string& name, SourceSpan span, Type type,
                      bool isMutable) {
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

    const ValueSymbol* lookupValue(const std::string& name) const {
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

    bool analyzeStmt(const Stmt& stmt) {
        if (const auto* block = dynamic_cast<const BlockStmt*>(&stmt)) {
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
            analyzeCondition(*ifStmt->condition, "`if` condition");
            const bool thenReturns = analyzeStmt(*ifStmt->thenBranch);
            bool elseReturns = false;
            if (ifStmt->elseBranch) {
                elseReturns = analyzeStmt(*ifStmt->elseBranch);
            }
            return ifStmt->elseBranch && thenReturns && elseReturns;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            analyzeCondition(*whileStmt->condition, "`while` condition");
            analyzeStmt(*whileStmt->body);
            return false;
        }

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
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

    void analyzeReturn(const ReturnStmt& ret) {
        if (!ret.value) {
            if (!currentReturnType_.isVoid()) {
                diagnostics_.error(ret.span,
                                   "non-void function must return a value of type `" +
                                       typeName(currentReturnType_) + "`");
            }
            return;
        }

        if (currentReturnType_.isVoid()) {
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

    void analyzeCondition(const Expr& condition, std::string_view label) {
        ExprInfo info = analyzeExpr(condition, std::nullopt);
        if (!canBeCondition(info.type)) {
            diagnostics_.error(condition.span,
                               std::string(label) +
                                   " must be `bool` or an integer type, not `" +
                                   typeName(info.type) + "`");
        }
    }

    ExprInfo analyzeExpr(const Expr& expr, std::optional<Type> expected) {
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
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
            return analyzeExpr(*paren->inner, expected);
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    ExprInfo analyzeCallExpr(const CallExpr& call) {
        const auto* callee = dynamic_cast<const NameExpr*>(call.callee.get());
        if (!callee) {
            diagnostics_.error(call.callee->span,
                               "callee must be a function name in Core v0");
            for (const std::unique_ptr<Expr>& argument : call.arguments) {
                analyzeExpr(*argument, std::nullopt);
            }
            return ExprInfo{.type = Type{}, .isConstant = false};
        }

        const auto function = functions_.find(callee->name);
        if (function == functions_.end()) {
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

    ExprInfo analyzeUnaryExpr(const UnaryExpr& unary, std::optional<Type> expected) {
        if (unary.op == TokenKind::Bang) {
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

    ExprInfo analyzeBinaryExpr(const BinaryExpr& binary, std::optional<Type> expected) {
        if (binary.op == TokenKind::AmpAmp || binary.op == TokenKind::PipePipe) {
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
            return analyzeConstantArithmetic(binary, left, right);
        }
        if (comparison || equality) {
            return ExprInfo{.type = Type{.kind = BuiltinTypeKind::Bool},
                            .isConstant = left.isConstant && right.isConstant};
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    ExprInfo analyzeConstantArithmetic(const BinaryExpr& binary, ExprInfo left,
                                       ExprInfo right) {
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

    void checkIntegerLiteralRange(const IntegerLiteralExpr& literal, Type type) {
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
    std::unordered_map<std::string, SourceSpan> topLevelNames_;
    std::unordered_map<std::string, FunctionSymbol> functions_;
    std::unordered_map<std::string, ValueSymbol> globals_;
    std::vector<std::unordered_map<std::string, ValueSymbol>> scopes_;
    Type currentReturnType_;
    bool sawReturnValue_ = false;
};

} // namespace

SemanticAnalyzer::SemanticAnalyzer(const SourceFile&,
                                   DiagnosticBag& diagnostics)
    : diagnostics_(diagnostics) {}

void SemanticAnalyzer::analyze(const TranslationUnit& unit) {
    AnalyzerImpl(diagnostics_).analyze(unit);
}

} // namespace nexc
