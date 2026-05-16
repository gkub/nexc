#include "nexc/frontend/semantic.h"

#include "nexc/frontend/string_literal_decode.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    // Non-empty => fixed-size array type; lengths are outermost dimension first
    // (e.g. `{2, 3}` for `[[i32; 3]; 2]`). Empty => scalar `kind`.
    std::vector<std::uint64_t> arrayDimensions;

    bool isInvalid() const {
        return kind == BuiltinTypeKind::Invalid && arrayDimensions.empty();
    }

    bool isVoid() const {
        return arrayDimensions.empty() && kind == BuiltinTypeKind::Void;
    }

    bool isBool() const {
        return arrayDimensions.empty() && kind == BuiltinTypeKind::Bool;
    }

    bool isString() const {
        return arrayDimensions.empty() && kind == BuiltinTypeKind::Str;
    }

    bool isFixedArray() const { return !arrayDimensions.empty(); }

    // Type after peeling one index dimension (still an array if more dims remain).
    Type afterIndex() const {
        Type t{.kind = kind, .arrayDimensions = arrayDimensions};
        if (!t.arrayDimensions.empty()) {
            t.arrayDimensions.erase(t.arrayDimensions.begin());
        }
        return t;
    }

    // Leaf scalar element as a scalar `Type` (for assignments and literals).
    Type elementScalarType() const { return Type{.kind = kind, .arrayDimensions = {}}; }

    bool isInteger() const {
        if (isFixedArray()) {
            return false;
        }
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

Type builtinScalar(BuiltinTypeKind k) {
    return Type{.kind = k, .arrayDimensions = {}};
}

// Compare two semantic types for exact equality, with invalid acting as a
// recovery wildcard.
bool sameType(Type left, Type right) {
    if (left.isInvalid() || right.isInvalid()) {
        return true;
    }
    return left.kind == right.kind && left.arrayDimensions == right.arrayDimensions;
}

// Convert a semantic type to the spelling used in diagnostics.
std::string typeName(Type type) {
    if (type.arrayDimensions.empty()) {
        return std::string(builtinTypeName(type.kind));
    }
    std::string t = std::string(builtinTypeName(type.kind));
    for (auto it = type.arrayDimensions.rbegin(); it != type.arrayDimensions.rend();
         ++it) {
        t = "[" + t + "; " + std::to_string(*it) + "]";
    }
    return t;
}

// Convert parser type syntax into semantic type information.
Type typeFromSyntax(TypeSyntax syntax) {
    return Type{.kind = syntax.kind, .arrayDimensions = syntax.arrayDimensions};
}

// Return true if a type is allowed in `if`, `while`, and `!` condition contexts.
//
// nex intentionally follows C-like integer truthiness only for integer and
// bool values. Strings and void are not condition-like.
bool canBeCondition(Type type) {
    // nex allows bool and integer conditions. Invalid is accepted here only
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
// table because nex does not let functions be used as first-class values.
//
// `bindingId` identifies one storage binding for **flow-sensitive definite
// assignment** of locals and parameters (see AnalyzerImpl). Module constants use
// `isConst == true` and leave `bindingId` at zero because they are not checked
// against the per-function assignment graph.
struct ValueSymbol {
    Type type{};
    bool isMutable = false;
    bool isConst = false;
    SourceSpan nameSpan{};
    std::size_t bindingId = 0;
};

// ExprInfo is the result of semantically analyzing an expression.
//
// It carries the expression type and the tiny amount of constant-evaluation
// state needed for current checks. This is not yet a full constant-value
// model; it only tracks unsigned integer payloads where that is enough.
struct ExprInfo {
    Type type;
    bool isConstant = false;
    std::optional<unsigned long long> integerValue = std::nullopt;
};

// Expression analysis sometimes needs to relax whole-array definite assignment
// when a fixed array appears only as the base of a chained index (`a[i][j]`).
enum class ExprCtx { Normal, IndexBase };

using ArrayElemMap = std::unordered_map<std::size_t, std::vector<std::uint8_t>>;

struct DefAssignReason {
    SourceSpan span;
    std::string message;
};
using DefAssignReasonMap = std::unordered_map<std::size_t, DefAssignReason>;

struct IndexedNameChain {
    const NameExpr* rootName = nullptr;
    // Indices from outer dimension to inner, matching `Type::arrayDimensions` order.
    std::vector<const Expr*> indices;
};

const Expr* stripParensConst(const Expr* e) {
    while (const auto* p = dynamic_cast<const ParenExpr*>(e)) {
        e = p->inner.get();
    }
    return e;
}

// If `outermost` is `name[…][…]` (with optional parens), peel it into a name plus
// an outer-to-inner index list. Returns false for callees, non-name bases, etc.
bool peelIndexedNameChain(const IndexExpr* outermost, IndexedNameChain* out) {
    std::vector<const Expr*> idxRev;
    const Expr* cur = outermost;
    while (true) {
        const auto* ix = dynamic_cast<const IndexExpr*>(stripParensConst(cur));
        if (!ix) {
            break;
        }
        idxRev.push_back(ix->index.get());
        cur = ix->base.get();
    }
    cur = stripParensConst(cur);
    const auto* nm = dynamic_cast<const NameExpr*>(cur);
    if (!nm) {
        return false;
    }
    out->rootName = nm;
    out->indices.assign(idxRev.rbegin(), idxRev.rend());
    return true;
}

unsigned long long flatElementCountRanked(const Type& t) {
    if (!t.isFixedArray()) {
        return 1;
    }
    unsigned long long p = 1;
    for (std::uint64_t d : t.arrayDimensions) {
        p *= d;
    }
    return p;
}

// Per-element definite assignment tracks at most this many elements per binding.
constexpr std::size_t kMaxArrayElemsForDA = 65536;

bool vectorAllOnes(const std::vector<std::uint8_t>& v) {
    for (std::uint8_t b : v) {
        if (b == 0) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> bandMasks(const std::vector<std::uint8_t>& a,
                                    const std::vector<std::uint8_t>& b) {
    std::vector<std::uint8_t> out(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = (a[i] != 0 && b[i] != 0) ? static_cast<std::uint8_t>(1) : 0;
    }
    return out;
}

std::vector<std::uint8_t> maskOrFull(const std::unordered_set<std::size_t>& def,
                                     const ArrayElemMap& arr, std::size_t id, std::size_t n) {
    if (def.contains(id)) {
        return std::vector<std::uint8_t>(n, 1);
    }
    const auto it = arr.find(id);
    if (it == arr.end()) {
        return std::vector<std::uint8_t>(n, 0);
    }
    return it->second;
}

std::optional<std::size_t> constIndicesToFlatOffset(const Type& arrayTy,
                                                    const std::vector<unsigned long long>& idxs) {
    if (!arrayTy.isFixedArray() || idxs.size() != arrayTy.arrayDimensions.size()) {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < idxs.size(); ++k) {
        if (idxs[k] >= arrayTy.arrayDimensions[k]) {
            return std::nullopt;
        }
    }
    unsigned long long offset = 0;
    for (std::size_t k = 0; k < idxs.size(); ++k) {
        unsigned long long stride = 1;
        for (std::size_t j = k + 1; j < arrayTy.arrayDimensions.size(); ++j) {
            stride *= arrayTy.arrayDimensions[j];
        }
        offset += static_cast<unsigned long long>(idxs[k]) * stride;
    }
    return static_cast<std::size_t>(offset);
}

// Flat index of the first element of the slice selected by the first `idxs.size()`
// outer dimensions (indices must be within bounds on those dimensions).
std::optional<std::size_t> flatOffsetForPrefixIndices(
    const Type& arrayTy, const std::vector<unsigned long long>& idxs) {
    if (!arrayTy.isFixedArray() || idxs.empty() || idxs.size() > arrayTy.arrayDimensions.size()) {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < idxs.size(); ++k) {
        if (idxs[k] >= arrayTy.arrayDimensions[k]) {
            return std::nullopt;
        }
    }
    unsigned long long offset = 0;
    for (std::size_t k = 0; k < idxs.size(); ++k) {
        unsigned long long stride = 1;
        for (std::size_t j = k + 1; j < arrayTy.arrayDimensions.size(); ++j) {
            stride *= arrayTy.arrayDimensions[j];
        }
        offset += static_cast<unsigned long long>(idxs[k]) * stride;
    }
    return static_cast<std::size_t>(offset);
}

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
    if (type.isFixedArray()) {
        return bitWidth(type.elementScalarType());
    }
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
    if (type.isFixedArray()) {
        return isSigned(type.elementScalarType());
    }
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

    // Signed nex integers use one sign bit, so the positive literal range is
    // 2^(width - 1) - 1. Unsigned integers use all bits for the value.
    const unsigned valueBits = isSigned(type) ? width - 1 : width;
    return (1ULL << valueBits) - 1;
}

// Intersection of two definite-assignment sets: value is readable after a join
// only if it was readable on **both** incoming structured paths.
std::unordered_set<std::size_t> intersectDefAssign(const std::unordered_set<std::size_t>& a,
                                                   const std::unordered_set<std::size_t>& b) {
    std::unordered_set<std::size_t> out;
    out.reserve(std::min(a.size(), b.size()));
    for (const std::size_t id : a) {
        if (b.contains(id)) {
            out.insert(id);
        }
    }
    return out;
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
        const Type str = builtinScalar(BuiltinTypeKind::Str);
        const Type i32 = builtinScalar(BuiltinTypeKind::I32);
        const Type u64 = builtinScalar(BuiltinTypeKind::U64);
        const Type boolType = builtinScalar(BuiltinTypeKind::Bool);
        const Type voidType = builtinScalar(BuiltinTypeKind::Void);
        const SourceSpan builtinSpan{};

        // Built-ins enter the same function table as user functions so call
        // checking can be uniform. `isBuiltin` lets us reject source attempts to
        // redefine them.
        // `print` / `println` use Rust-style format strings (`"{}"`) checked in
        // analyzeFormatPrintCall; arity is not fixed here.
        functions_["print"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {},
            .returnType = voidType,
            .isBuiltin = true,
        };
        functions_["println"] = FunctionSymbol{
            .nameSpan = builtinSpan,
            .parameterTypes = {},
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
                    const Type pt = typeFromSyntax(parameter.type);
                    validateFixedArrayDecl(pt, parameter.type.span);
                    parameterTypes.push_back(pt);
                }

                const Type returnType = typeFromSyntax(function->returnType);
                validateFixedArrayDecl(returnType, function->returnType.span);

                functions_[function->name] = FunctionSymbol{
                    .nameSpan = function->nameSpan,
                    .parameterTypes = std::move(parameterTypes),
                    .returnType = returnType,
                };
                continue;
            }

            if (const auto* constant = dynamic_cast<const ConstDecl*>(item.get())) {
                declareTopLevel(constant->name, constant->nameSpan);
                const Type constTy = typeFromSyntax(constant->type);
                validateFixedArrayDecl(constTy, constant->type.span);
                globals_[constant->name] = ValueSymbol{
                    .type = constTy,
                    .isMutable = false,
                    .isConst = true,
                    .nameSpan = constant->nameSpan,
                };
            }
        }
    }

    // Record a top-level name in the module namespace.
    //
    // nex uses one namespace for functions and module constants, so `fn foo`
    // and `const foo` conflict.
    void declareTopLevel(const std::string& name, SourceSpan span) {
        // Functions and constants share one module namespace.
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
    // use the narrow executable signatures.
    void validateMainIfPresent() {
        // `main` is optional because a source file may be a library unit. If it
        // exists, nex restricts its shape so future execution has a clear
        // entry convention.
        const auto it = functions_.find("main");
        if (it == functions_.end()) {
            return;
        }

        const FunctionSymbol& main = it->second;
        if (!main.parameterTypes.empty()) {
            diagnostics_.error(main.nameSpan, "`main` must not have parameters");
        }

        if (!main.returnType.isVoid() &&
            (main.returnType.isFixedArray() ||
             main.returnType.kind != BuiltinTypeKind::I32)) {
            diagnostics_.error(main.nameSpan,
                               "`main` must return `void` or `i32`");
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
        // definite-assignment graph, and return-path tracking.
        currentReturnType_ = typeFromSyntax(function.returnType);
        sawReturnValue_ = false;
        scopes_.clear();
        definiteAssign_.clear();
        defAssignReasons_.clear();
        scopeBindingIds_.clear();
        nextBindingId_ = 1;
        pushScope();

        for (const ParameterSyntax& parameter : function.parameters) {
            const std::size_t id = declareLocal(parameter.name, parameter.nameSpan,
                                                typeFromSyntax(parameter.type), false);
            markDefiniteAssigned(id);
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
    // source nesting. Nested scopes also track which bindingIds were declared
    // here so `popScope` can scrub them from the definite-assignment set.
    void pushScope() {
        scopes_.push_back({});
        scopeBindingIds_.push_back({});
    }

    // End the current lexical local scope.
    //
    // Bindings introduced in the scope disappear from the symbol tables, and their
    // ids leave `definiteAssign_` so shadowed outer names do not inherit the
    // inner symbol's assignment state.
    void popScope() {
        for (const std::size_t id : scopeBindingIds_.back()) {
            definiteAssign_.erase(id);
            arrayElemAssign_.erase(id);
            defAssignReasons_.erase(id);
        }
        scopeBindingIds_.pop_back();
        scopes_.pop_back();
    }

    // Declare a local or parameter in the current scope.
    //
    // Returns the new binding id, or 0 on duplicate-name failure (diagnostics
    // already emitted). Parameters should always succeed.
    std::size_t declareLocal(const std::string& name, SourceSpan span, Type type,
                              bool isMutable) {
        // Duplicates are only rejected within the current lexical scope. Shadowing
        // an outer local is allowed by this implementation unless the language
        // spec later forbids it.
        auto& scope = scopes_.back();
        if (scope.contains(name)) {
            diagnostics_.error(span, "duplicate local name `" + name + "`");
            diagnostics_.note(scope[name].nameSpan,
                              "previous declaration of `" + name + "` is here");
            return 0;
        }

        const std::size_t id = nextBindingId_++;
        scope[name] = ValueSymbol{
            .type = type,
            .isMutable = isMutable,
            .isConst = false,
            .nameSpan = span,
            .bindingId = id,
        };
        scopeBindingIds_.back().push_back(id);
        return id;
    }

    void requireReadableLocal(const ValueSymbol& symbol, SourceSpan useSpan,
                              std::string_view nameForDiag) {
        if (symbol.isConst) {
            return;
        }
        if (symbol.type.isFixedArray()) {
            if (definiteAssign_.contains(symbol.bindingId)) {
                return;
            }
            if (const auto it = arrayElemAssign_.find(symbol.bindingId);
                it != arrayElemAssign_.end() && vectorAllOnes(it->second)) {
                return;
            }
            diagnostics_.error(
                useSpan,
                "local `" + std::string(nameForDiag) + "` may be read before assignment");
            noteDefAssignReason(symbol);
            return;
        }
        if (!definiteAssign_.contains(symbol.bindingId)) {
            diagnostics_.error(
                useSpan,
                "local `" + std::string(nameForDiag) + "` may be read before assignment");
            noteDefAssignReason(symbol);
        }
    }

    void rememberDefAssignReason(std::size_t bindingId, SourceSpan span,
                                 std::string message) {
        if (bindingId != 0) {
            defAssignReasons_[bindingId] = DefAssignReason{
                .span = span,
                .message = std::move(message),
            };
        }
    }

    void markDefiniteAssigned(std::size_t bindingId) {
        definiteAssign_.insert(bindingId);
        defAssignReasons_.erase(bindingId);
    }

    void noteDefAssignReason(const ValueSymbol& symbol) {
        const auto it = defAssignReasons_.find(symbol.bindingId);
        if (it != defAssignReasons_.end()) {
            diagnostics_.note(it->second.span, it->second.message);
            return;
        }
        diagnostics_.note(symbol.nameSpan,
                          "binding declared here; it must be assigned on every path before use");
    }

    void initArrayElemTrackingForBinding(std::size_t bindingId, const Type& declared,
                                         SourceSpan span) {
        if (!declared.isFixedArray()) {
            return;
        }
        const unsigned long long n64 = flatElementCountRanked(declared);
        if (n64 == 0 || n64 > kMaxArrayElemsForDA) {
            diagnostics_.error(span,
                               "fixed array is too large for per-element definite assignment "
                               "tracking in this compiler version");
            return;
        }
        arrayElemAssign_.insert_or_assign(bindingId,
                                          std::vector<std::uint8_t>(static_cast<std::size_t>(n64),
                                                                    0));
    }

    void markArrayBindingFullyAssigned(std::size_t bindingId) {
        arrayElemAssign_.erase(bindingId);
        markDefiniteAssigned(bindingId);
    }

    void noteIndexedStoreToLocal(std::size_t bindingId, const Type& rootArrayTy,
                                 const std::vector<ExprInfo>& indexInfos) {
        if (!rootArrayTy.isFixedArray()) {
            return;
        }
        const unsigned long long n64 = flatElementCountRanked(rootArrayTy);
        if (n64 == 0 || n64 > kMaxArrayElemsForDA) {
            return;
        }
        const std::size_t n = static_cast<std::size_t>(n64);
        for (const ExprInfo& ii : indexInfos) {
            if (!ii.type.isInteger() || !ii.isConstant || !ii.integerValue) {
                arrayElemAssign_.insert_or_assign(bindingId, std::vector<std::uint8_t>(n, 0));
                definiteAssign_.erase(bindingId);
                return;
            }
        }
        std::vector<unsigned long long> vals;
        vals.reserve(indexInfos.size());
        for (const ExprInfo& ii : indexInfos) {
            vals.push_back(*ii.integerValue);
        }
        if (vals.size() != rootArrayTy.arrayDimensions.size()) {
            return;
        }
        const std::optional<std::size_t> offset = constIndicesToFlatOffset(rootArrayTy, vals);
        if (!offset) {
            return;
        }
        auto it = arrayElemAssign_.find(bindingId);
        if (it == arrayElemAssign_.end()) {
            arrayElemAssign_.insert_or_assign(bindingId, std::vector<std::uint8_t>(n, 0));
            it = arrayElemAssign_.find(bindingId);
        }
        std::vector<std::uint8_t>& mask = it->second;
        if (*offset >= mask.size()) {
            return;
        }
        mask[*offset] = 1;
        if (vectorAllOnes(mask)) {
            markDefiniteAssigned(bindingId);
            arrayElemAssign_.erase(bindingId);
        }
    }

    void requireIndexedScalarReadable(const ValueSymbol& symbol, const Type& rootTy,
                                      const std::vector<ExprInfo>& indexInfos, SourceSpan useSpan,
                                      std::string_view nameForDiag) {
        if (symbol.isConst) {
            return;
        }
        if (definiteAssign_.contains(symbol.bindingId)) {
            return;
        }
        bool allConst = true;
        std::vector<unsigned long long> vals;
        vals.reserve(indexInfos.size());
        for (const ExprInfo& ii : indexInfos) {
            if (!ii.type.isInteger() || !ii.isConstant || !ii.integerValue) {
                allConst = false;
                break;
            }
            vals.push_back(*ii.integerValue);
        }
        if (!allConst || vals.size() != rootTy.arrayDimensions.size()) {
            requireReadableLocal(symbol, useSpan, nameForDiag);
            return;
        }
        for (std::size_t k = 0; k < vals.size(); ++k) {
            if (vals[k] >= rootTy.arrayDimensions[k]) {
                return;
            }
        }
        const std::optional<std::size_t> offset = constIndicesToFlatOffset(rootTy, vals);
        if (!offset) {
            return;
        }
        const auto it = arrayElemAssign_.find(symbol.bindingId);
        if (it == arrayElemAssign_.end() || *offset >= it->second.size() ||
            it->second[*offset] == 0) {
            diagnostics_.error(useSpan, "indexed read may access an uninitialized element of `" +
                                           std::string(nameForDiag) + "`");
            noteDefAssignReason(symbol);
        }
    }

    void requireSubArraySliceReadable(const ValueSymbol& symbol, const Type& rootTy,
                                      std::size_t prefixLen, const std::vector<ExprInfo>& indexInfos,
                                      SourceSpan useSpan, std::string_view nameForDiag) {
        (void)rootTy;
        if (symbol.isConst) {
            return;
        }
        if (definiteAssign_.contains(symbol.bindingId)) {
            return;
        }
        if (prefixLen == 0) {
            return;
        }
        bool allConst = true;
        std::vector<unsigned long long> vals;
        for (std::size_t i = 0; i < prefixLen; ++i) {
            const ExprInfo& ii = indexInfos[i];
            if (!ii.type.isInteger() || !ii.isConstant || !ii.integerValue) {
                allConst = false;
                break;
            }
            vals.push_back(*ii.integerValue);
        }
        if (!allConst) {
            requireReadableLocal(symbol, useSpan, nameForDiag);
            return;
        }
        Type sliceTy = symbol.type;
        for (std::size_t i = 0; i < prefixLen; ++i) {
            if (vals[i] >= sliceTy.arrayDimensions[0]) {
                return;
            }
            sliceTy = sliceTy.afterIndex();
        }
        const unsigned long long spanElems = flatElementCountRanked(sliceTy);
        if (spanElems == 0 || spanElems > kMaxArrayElemsForDA) {
            requireReadableLocal(symbol, useSpan, nameForDiag);
            return;
        }
        const std::optional<std::size_t> rangeStart = flatOffsetForPrefixIndices(symbol.type, vals);
        if (!rangeStart) {
            return;
        }
        const auto it = arrayElemAssign_.find(symbol.bindingId);
        if (it == arrayElemAssign_.end()) {
            diagnostics_.error(useSpan,
                               "indexed access may read uninitialized elements of `" +
                                   std::string(nameForDiag) + "`");
            noteDefAssignReason(symbol);
            return;
        }
        for (unsigned long long i = 0; i < spanElems; ++i) {
            const std::size_t pos = static_cast<std::size_t>(*rangeStart + i);
            if (pos >= it->second.size() || it->second[pos] == 0) {
                diagnostics_.error(useSpan,
                                   "indexed access may read uninitialized elements of `" +
                                       std::string(nameForDiag) + "`");
                noteDefAssignReason(symbol);
                return;
            }
        }
    }

    void mergeDefiniteAssignAfterIf(const std::unordered_set<std::size_t>& beforeIf,
                                    const std::unordered_set<std::size_t>& afterThen,
                                    const std::unordered_set<std::size_t>& afterElse,
                                    bool hasElse, bool thenReturns, bool elseReturns) {
        if (!hasElse) {
            if (thenReturns) {
                // Only the "condition false, skip then" path reaches the code
                // after the `if`.
                definiteAssign_ = beforeIf;
            } else {
                // Either skipped the then-arm (state `beforeIf`) or ran it and fell
                // through (state `afterThen`).
                definiteAssign_ = intersectDefAssign(beforeIf, afterThen);
            }
            return;
        }

        if (thenReturns && !elseReturns) {
            definiteAssign_ = afterElse;
        } else if (!thenReturns && elseReturns) {
            definiteAssign_ = afterThen;
        } else if (thenReturns && elseReturns) {
            definiteAssign_ = beforeIf;
        } else {
            definiteAssign_ = intersectDefAssign(afterThen, afterElse);
        }
    }

    void noteIfDefAssignLosses(SourceSpan ifSpan,
                               const std::unordered_set<std::size_t>& beforeIf,
                               const std::unordered_set<std::size_t>& afterThen,
                               const std::unordered_set<std::size_t>& afterElse,
                               bool hasElse, bool thenReturns, bool elseReturns) {
        std::unordered_set<std::size_t> candidates = afterThen;
        candidates.insert(afterElse.begin(), afterElse.end());
        for (const std::size_t id : candidates) {
            if (definiteAssign_.contains(id) || beforeIf.contains(id)) {
                continue;
            }
            if (!hasElse) {
                if (afterThen.contains(id)) {
                    rememberDefAssignReason(
                        id, ifSpan,
                        "assignment may be skipped because this `if` has no `else`");
                }
                continue;
            }
            const bool thenCanFallThrough = !thenReturns;
            const bool elseCanFallThrough = !elseReturns;
            const bool missingFromThen = thenCanFallThrough && !afterThen.contains(id);
            const bool missingFromElse = elseCanFallThrough && !afterElse.contains(id);
            if (missingFromThen || missingFromElse) {
                rememberDefAssignReason(
                    id, ifSpan,
                    "assignment is not guaranteed on every branch of this `if`");
            }
        }
    }

    void noteLoopDefAssignLosses(SourceSpan loopSpan,
                                 const std::unordered_set<std::size_t>& beforeLoop,
                                 const std::unordered_set<std::size_t>& afterBody) {
        for (const std::size_t id : afterBody) {
            if (beforeLoop.contains(id)) {
                continue;
            }
            rememberDefAssignReason(
                id, loopSpan,
                "assignment inside a loop is not guaranteed because the loop may run zero times");
        }
    }

    std::optional<Type> lookupLocalTypeByBindingId(std::size_t id) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            for (const auto& entry : *scope) {
                const ValueSymbol& sym = entry.second;
                if (!sym.isConst && sym.bindingId == id) {
                    return sym.type;
                }
            }
        }
        return std::nullopt;
    }

    void mergeArrayElemAssignAfterIf(const ArrayElemMap& beforeArr,
                                     const std::unordered_set<std::size_t>& beforeDef,
                                     const ArrayElemMap& afterThenArr,
                                     const std::unordered_set<std::size_t>& afterThenDef,
                                     const ArrayElemMap& afterElseArr,
                                     const std::unordered_set<std::size_t>& afterElseDef,
                                     bool hasElse, bool thenReturns, bool elseReturns) {
        auto promoteFullMasks = [&]() {
            for (auto it = arrayElemAssign_.begin(); it != arrayElemAssign_.end();) {
                if (vectorAllOnes(it->second)) {
                    definiteAssign_.insert(it->first);
                    it = arrayElemAssign_.erase(it);
                } else {
                    ++it;
                }
            }
        };

        auto assignArrayMap = [&](const ArrayElemMap& src) {
            arrayElemAssign_.clear();
            for (const auto& entry : src) {
                if (!definiteAssign_.contains(entry.first)) {
                    arrayElemAssign_.insert(entry);
                }
            }
            promoteFullMasks();
        };

        auto collectArrayCandidatesTwo = [&](const std::unordered_set<std::size_t>& d0,
                                           const ArrayElemMap& a0,
                                           const std::unordered_set<std::size_t>& d1,
                                           const ArrayElemMap& a1) {
            std::unordered_set<std::size_t> candidates;
            for (const auto& e : a0) {
                candidates.insert(e.first);
            }
            for (const auto& e : a1) {
                candidates.insert(e.first);
            }
            for (const std::size_t id : d0) {
                const auto ty = lookupLocalTypeByBindingId(id);
                if (ty && ty->isFixedArray()) {
                    candidates.insert(id);
                }
            }
            for (const std::size_t id : d1) {
                const auto ty = lookupLocalTypeByBindingId(id);
                if (ty && ty->isFixedArray()) {
                    candidates.insert(id);
                }
            }
            return candidates;
        };

        auto collectArrayCandidatesBoth = [&]() {
            std::unordered_set<std::size_t> candidates;
            for (const auto& e : afterThenArr) {
                candidates.insert(e.first);
            }
            for (const auto& e : afterElseArr) {
                candidates.insert(e.first);
            }
            for (const std::size_t id : afterThenDef) {
                const auto ty = lookupLocalTypeByBindingId(id);
                if (ty && ty->isFixedArray()) {
                    candidates.insert(id);
                }
            }
            for (const std::size_t id : afterElseDef) {
                const auto ty = lookupLocalTypeByBindingId(id);
                if (ty && ty->isFixedArray()) {
                    candidates.insert(id);
                }
            }
            return candidates;
        };

        auto mergeTwoWayMaps = [&](const std::unordered_set<std::size_t>& d0,
                                   const ArrayElemMap& a0, const std::unordered_set<std::size_t>& d1,
                                   const ArrayElemMap& a1) {
            const std::unordered_set<std::size_t> candidates =
                collectArrayCandidatesTwo(d0, a0, d1, a1);
            arrayElemAssign_.clear();
            for (const std::size_t id : candidates) {
                if (definiteAssign_.contains(id)) {
                    continue;
                }
                const auto ty = lookupLocalTypeByBindingId(id);
                if (!ty || !ty->isFixedArray()) {
                    continue;
                }
                const unsigned long long n64 = flatElementCountRanked(*ty);
                if (n64 == 0 || n64 > kMaxArrayElemsForDA) {
                    continue;
                }
                const std::size_t n = static_cast<std::size_t>(n64);
                const std::vector<std::uint8_t> m0 = maskOrFull(d0, a0, id, n);
                const std::vector<std::uint8_t> m1 = maskOrFull(d1, a1, id, n);
                const std::vector<std::uint8_t> merged = bandMasks(m0, m1);
                if (vectorAllOnes(merged)) {
                    definiteAssign_.insert(id);
                    continue;
                }
                bool any = false;
                for (std::uint8_t b : merged) {
                    if (b != 0) {
                        any = true;
                        break;
                    }
                }
                if (any) {
                    arrayElemAssign_.insert({id, merged});
                }
            }
            promoteFullMasks();
        };

        auto mergeBothWayMaps = [&]() {
            const std::unordered_set<std::size_t> candidates = collectArrayCandidatesBoth();
            arrayElemAssign_.clear();
            for (const std::size_t id : candidates) {
                if (definiteAssign_.contains(id)) {
                    continue;
                }
                const auto ty = lookupLocalTypeByBindingId(id);
                if (!ty || !ty->isFixedArray()) {
                    continue;
                }
                const unsigned long long n64 = flatElementCountRanked(*ty);
                if (n64 == 0 || n64 > kMaxArrayElemsForDA) {
                    continue;
                }
                const std::size_t n = static_cast<std::size_t>(n64);
                const std::vector<std::uint8_t> mT =
                    maskOrFull(afterThenDef, afterThenArr, id, n);
                const std::vector<std::uint8_t> mE =
                    maskOrFull(afterElseDef, afterElseArr, id, n);
                const std::vector<std::uint8_t> merged = bandMasks(mT, mE);
                if (vectorAllOnes(merged)) {
                    definiteAssign_.insert(id);
                    continue;
                }
                bool any = false;
                for (std::uint8_t b : merged) {
                    if (b != 0) {
                        any = true;
                        break;
                    }
                }
                if (any) {
                    arrayElemAssign_.insert({id, merged});
                }
            }
            promoteFullMasks();
        };

        if (!hasElse) {
            if (thenReturns) {
                assignArrayMap(beforeArr);
                return;
            }
            mergeTwoWayMaps(beforeDef, beforeArr, afterThenDef, afterThenArr);
            return;
        }

        if (thenReturns && !elseReturns) {
            assignArrayMap(afterElseArr);
            return;
        }
        if (!thenReturns && elseReturns) {
            assignArrayMap(afterThenArr);
            return;
        }
        if (thenReturns && elseReturns) {
            assignArrayMap(beforeArr);
            return;
        }
        mergeBothWayMaps();
    }

    // Resolve a value-like name in lexical scopes, then module constants.
    //
    // Returns nullptr for an unresolved name so callers can issue a diagnostic
    // tailored to the context.
    const ValueSymbol* lookupValue(const std::string& name) const {
        // Lexical lookup walks from innermost scope outward, then falls back to
        // module constants. Function names are handled separately by call
        // analysis because functions are not values.
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

    void validateFixedArrayDecl(Type type, SourceSpan span) {
        if (!type.isFixedArray()) {
            return;
        }
        for (std::uint64_t d : type.arrayDimensions) {
            if (d == 0) {
                diagnostics_.error(span, "fixed array length must be greater than zero");
                return;
            }
        }
        if (type.kind == BuiltinTypeKind::Void || type.kind == BuiltinTypeKind::Str ||
            type.kind == BuiltinTypeKind::Invalid) {
            diagnostics_.error(span,
                               "fixed array element cannot be `void`, `str`, or invalid");
        }
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
            validateFixedArrayDecl(declared, let->type.span);
            if (!let->init) {
                if (!let->isMutable) {
                    diagnostics_.error(let->span,
                                       "`let` requires an initializer; only `let mut` "
                                       "may omit `=`");
                    return false;
                }
                if (declared.isFixedArray()) {
                    const std::size_t id =
                        declareLocal(let->name, let->nameSpan, declared, true);
                    if (id != 0) {
                        initArrayElemTrackingForBinding(id, declared, let->type.span);
                        rememberDefAssignReason(
                            id, let->nameSpan,
                            "binding declared here without an initializer");
                    }
                    return false;
                }
                const std::size_t id =
                    declareLocal(let->name, let->nameSpan, declared, true);
                rememberDefAssignReason(id, let->nameSpan,
                                        "binding declared here without an initializer");
                return false;
            }

            ExprInfo init = analyzeExpr(*let->init, declared);
            if (!sameType(declared, init.type)) {
                diagnostics_.error(let->init->span,
                                   "cannot initialize local `" + let->name +
                                       "` of type `" + typeName(declared) +
                                       "` with value of type `" + typeName(init.type) +
                                       "`");
            }
            const std::size_t id =
                declareLocal(let->name, let->nameSpan, declared, let->isMutable);
            if (id != 0) {
                markDefiniteAssigned(id);
                if (declared.isFixedArray()) {
                    markArrayBindingFullyAssigned(id);
                }
            }
            return false;
        }

        if (const auto* assign = dynamic_cast<const AssignStmt*>(&stmt)) {
            if (const auto* nameExpr = dynamic_cast<const NameExpr*>(assign->target.get())) {
                const ValueSymbol* symbol = lookupValue(nameExpr->name);
                if (!symbol) {
                    diagnostics_.error(nameExpr->span,
                                       "undefined local `" + nameExpr->name + "`");
                    analyzeExpr(*assign->value, std::nullopt);
                    return false;
                }
                if (!symbol->isMutable) {
                    diagnostics_.error(nameExpr->span,
                                       "cannot assign to immutable binding `" +
                                           nameExpr->name + "`");
                }
                ExprInfo value = analyzeExpr(*assign->value, symbol->type);
                if (!sameType(symbol->type, value.type)) {
                    diagnostics_.error(assign->value->span,
                                       "cannot assign value of type `" +
                                           typeName(value.type) + "` to `" + nameExpr->name +
                                           "` of type `" + typeName(symbol->type) + "`");
                }
                markDefiniteAssigned(symbol->bindingId);
                return false;
            }

            if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(assign->target.get())) {
                IndexedNameChain chain;
                if (!peelIndexedNameChain(indexExpr, &chain)) {
                    diagnostics_.error(assign->target->span,
                                       "indexed assignment requires a local array name "
                                       "with a constant index chain");
                    analyzeExpr(*assign->value, std::nullopt);
                    return false;
                }

                const ValueSymbol* symbol = lookupValue(chain.rootName->name);
                if (!symbol) {
                    diagnostics_.error(chain.rootName->span,
                                       "undefined local `" + chain.rootName->name + "`");
                    analyzeExpr(*assign->value, std::nullopt);
                    return false;
                }
                if (!symbol->isMutable) {
                    diagnostics_.error(chain.rootName->span,
                                       "cannot assign through immutable array binding `" +
                                           chain.rootName->name + "`");
                    analyzeExpr(*assign->value, std::nullopt);
                    return false;
                }
                if (!symbol->type.isFixedArray()) {
                    diagnostics_.error(chain.rootName->span,
                                       "indexed assignment requires an array local");
                    analyzeExpr(*assign->value, std::nullopt);
                    return false;
                }

                if (chain.indices.size() != symbol->type.arrayDimensions.size()) {
                    diagnostics_.error(assign->target->span,
                                       "indexed assignment must store through a scalar element "
                                       "of `" +
                                           typeName(symbol->type) + "`");
                    analyzeExpr(*assign->value, std::nullopt);
                    return false;
                }

                Type walk = symbol->type;
                std::vector<ExprInfo> idxInfos;
                idxInfos.reserve(chain.indices.size());
                for (const Expr* idxExpr : chain.indices) {
                    ExprInfo idx = analyzeExpr(*idxExpr, builtinScalar(BuiltinTypeKind::I32));
                    idxInfos.push_back(idx);
                    if (!idx.type.isInteger()) {
                        diagnostics_.error(idxExpr->span,
                                           "array index must be an integer type");
                    }
                    if (idx.isConstant && idx.integerValue) {
                        const unsigned long long idxVal = *idx.integerValue;
                        if (walk.arrayDimensions.empty() ||
                            idxVal >= walk.arrayDimensions[0]) {
                            diagnostics_.error(idxExpr->span,
                                               "array index out of bounds for `" +
                                                   typeName(symbol->type) + "`");
                        }
                    }
                    walk = walk.afterIndex();
                }

                const Type elem = walk;
                ExprInfo value = analyzeExpr(*assign->value, elem);
                if (!sameType(elem, value.type)) {
                    diagnostics_.error(assign->value->span,
                                       "cannot assign value of type `" +
                                           typeName(value.type) + "` to element type `" +
                                           typeName(elem) + "`");
                }

                if (!symbol->isConst) {
                    noteIndexedStoreToLocal(symbol->bindingId, symbol->type, idxInfos);
                }
                return false;
            }

            diagnostics_.error(assign->target->span,
                               "assignment target must be a local name or indexed place");
            analyzeExpr(*assign->value, std::nullopt);
            return false;
        }

        if (const auto* ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
            analyzeReturn(*ret);
            return true;
        }

        if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&stmt)) {
            analyzeCondition(*ifStmt->condition, "`if` condition");
            const std::unordered_set<std::size_t> beforeIf = definiteAssign_;
            const ArrayElemMap beforeIfArrays = arrayElemAssign_;
            const DefAssignReasonMap beforeIfReasons = defAssignReasons_;
            const bool thenReturns = analyzeStmt(*ifStmt->thenBranch);
            const std::unordered_set<std::size_t> afterThen = definiteAssign_;
            const ArrayElemMap afterThenArrays = arrayElemAssign_;
            const DefAssignReasonMap afterThenReasons = defAssignReasons_;
            definiteAssign_ = beforeIf;
            arrayElemAssign_ = beforeIfArrays;
            defAssignReasons_ = beforeIfReasons;
            bool elseReturns = false;
            std::unordered_set<std::size_t> afterElse = beforeIf;
            ArrayElemMap afterElseArrays = beforeIfArrays;
            DefAssignReasonMap afterElseReasons = beforeIfReasons;
            if (ifStmt->elseBranch) {
                elseReturns = analyzeStmt(*ifStmt->elseBranch);
                afterElse = definiteAssign_;
                afterElseArrays = arrayElemAssign_;
                afterElseReasons = defAssignReasons_;
            }
            if (ifStmt->elseBranch && thenReturns && !elseReturns) {
                defAssignReasons_ = afterElseReasons;
            } else if (ifStmt->elseBranch && !thenReturns && elseReturns) {
                defAssignReasons_ = afterThenReasons;
            } else {
                defAssignReasons_ = beforeIfReasons;
            }
            mergeDefiniteAssignAfterIf(beforeIf, afterThen, afterElse,
                                       ifStmt->elseBranch != nullptr, thenReturns,
                                       elseReturns);
            noteIfDefAssignLosses(ifStmt->span, beforeIf, afterThen, afterElse,
                                  ifStmt->elseBranch != nullptr, thenReturns, elseReturns);
            mergeArrayElemAssignAfterIf(beforeIfArrays, beforeIf, afterThenArrays, afterThen,
                                        afterElseArrays, afterElse, ifStmt->elseBranch != nullptr,
                                        thenReturns, elseReturns);
            return ifStmt->elseBranch && thenReturns && elseReturns;
        }

        if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&stmt)) {
            const std::unordered_set<std::size_t> saved = definiteAssign_;
            const ArrayElemMap savedArrays = arrayElemAssign_;
            const DefAssignReasonMap savedReasons = defAssignReasons_;
            analyzeCondition(*whileStmt->condition, "`while` condition");
            ++loopDepth_;
            analyzeStmt(*whileStmt->body);
            --loopDepth_;
            const std::unordered_set<std::size_t> afterBody = definiteAssign_;
            definiteAssign_ = saved;
            arrayElemAssign_ = savedArrays;
            defAssignReasons_ = savedReasons;
            noteLoopDefAssignLosses(whileStmt->span, saved, afterBody);
            return false;
        }

        if (const auto* forStmt = dynamic_cast<const ForStmt*>(&stmt)) {
            pushScope();
            if (forStmt->init) {
                analyzeStmt(*forStmt->init);
            }
            const std::unordered_set<std::size_t> afterInit = definiteAssign_;
            const ArrayElemMap afterInitArrays = arrayElemAssign_;
            const DefAssignReasonMap afterInitReasons = defAssignReasons_;
            if (forStmt->condition) {
                analyzeCondition(*forStmt->condition, "`for` condition");
            }
            ++loopDepth_;
            analyzeStmt(*forStmt->body);
            if (forStmt->step) {
                inForStepClause_ = true;
                analyzeStmt(*forStmt->step);
                inForStepClause_ = false;
            }
            --loopDepth_;
            const std::unordered_set<std::size_t> afterBody = definiteAssign_;
            definiteAssign_ = afterInit;
            arrayElemAssign_ = afterInitArrays;
            defAssignReasons_ = afterInitReasons;
            noteLoopDefAssignLosses(forStmt->span, afterInit, afterBody);
            popScope();
            return false;
        }

        if (dynamic_cast<const BreakStmt*>(&stmt)) {
            if (loopDepth_ == 0) {
                diagnostics_.error(stmt.span,
                                   "`break` is only valid inside `while` or `for`");
            }
            return false;
        }

        if (dynamic_cast<const ContinueStmt*>(&stmt)) {
            if (loopDepth_ == 0) {
                diagnostics_.error(
                    stmt.span, "`continue` is only valid inside `while` or `for`");
            } else if (inForStepClause_) {
                diagnostics_.error(stmt.span,
                                   "`continue` cannot appear in a `for` update clause");
            }
            return false;
        }

        if (const auto* callStmt = dynamic_cast<const CallStmt*>(&stmt)) {
            // Parser syntax allows any call expression as a statement. Semantic
            // analysis enforces the rule that only void call results may
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
    // Conditions are special because nex accepts both bool and integers
    // there. Passing no expected type lets integer literals default before the
    // condition-kind check runs.
    void analyzeCondition(const Expr& condition, std::string_view label) {
        // Conditions deliberately do not pass an expected type. Integer and bool
        // are both accepted, so the expression should choose its
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
    ExprInfo analyzeExpr(const Expr& expr, std::optional<Type> expected,
                         ExprCtx ctx = ExprCtx::Normal) {
        if (const auto* integer = dynamic_cast<const IntegerLiteralExpr*>(&expr)) {
            // Unsuffixed integer literals get their type from context when there
            // is one, otherwise they default to i32.
            Type type = expected.value_or(builtinScalar(BuiltinTypeKind::I32));
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
                .type = builtinScalar(BuiltinTypeKind::Bool),
                .isConstant = true,
                .integerValue = boolean->value ? 1ULL : 0ULL,
            };
        }

        if (const auto* string = dynamic_cast<const StringLiteralExpr*>(&expr)) {
            (void)string;
            return ExprInfo{
                .type = builtinScalar(BuiltinTypeKind::Str),
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
            if (!(ctx == ExprCtx::IndexBase && symbol->type.isFixedArray())) {
                requireReadableLocal(*symbol, name->span, name->name);
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

        if (const auto* arrayLit = dynamic_cast<const ArrayLiteralExpr*>(&expr)) {
            if (expected && expected->isFixedArray()) {
                const Type arr = *expected;
                if (arrayLit->elements.size() != arr.arrayDimensions[0]) {
                    diagnostics_.error(expr.span,
                                       "array literal length " +
                                           std::to_string(arrayLit->elements.size()) +
                                           " does not match type `" + typeName(arr) + "`");
                }
                const Type innerExpected = arr.afterIndex();
                bool allConst = true;
                for (const std::unique_ptr<Expr>& el : arrayLit->elements) {
                    ExprInfo ei = analyzeExpr(*el, innerExpected);
                    if (!sameType(innerExpected, ei.type)) {
                        diagnostics_.error(el->span,
                                           "array element type `" + typeName(ei.type) +
                                               "` does not match `" + typeName(innerExpected) +
                                               "`");
                    }
                    allConst = allConst && ei.isConstant;
                }
                return ExprInfo{.type = arr, .isConstant = allConst};
            }

            if (arrayLit->elements.empty()) {
                diagnostics_.error(expr.span,
                                   "empty array literal requires a contextual `[T; N]` type");
                return ExprInfo{.type = Type{}, .isConstant = false};
            }

            ExprInfo first = analyzeExpr(*arrayLit->elements[0], std::nullopt);
            bool allConst = first.isConstant;
            for (std::size_t i = 1; i < arrayLit->elements.size(); ++i) {
                ExprInfo ei =
                    analyzeExpr(*arrayLit->elements[i], first.type);
                if (!sameType(first.type, ei.type)) {
                    diagnostics_.error(arrayLit->elements[i]->span,
                                       "array literal elements must have the same type");
                }
                allConst = allConst && ei.isConstant;
            }

            if (first.type.isVoid() || first.type.isString() || first.type.isInvalid()) {
                diagnostics_.error(expr.span,
                                   "cannot infer array type from these element types");
                return ExprInfo{.type = Type{}, .isConstant = false};
            }

            if (first.type.isFixedArray()) {
                Type inferred = first.type;
                inferred.arrayDimensions.insert(inferred.arrayDimensions.begin(),
                                                arrayLit->elements.size());
                return ExprInfo{.type = inferred, .isConstant = allConst};
            }

            const Type inferred{.kind = first.type.kind,
                                .arrayDimensions = {arrayLit->elements.size()}};
            return ExprInfo{.type = inferred, .isConstant = allConst};
        }

        if (const auto* indexExpr = dynamic_cast<const IndexExpr*>(&expr)) {
            if (ctx == ExprCtx::Normal) {
                IndexedNameChain chain;
                if (peelIndexedNameChain(indexExpr, &chain)) {
                    const ValueSymbol* sym = lookupValue(chain.rootName->name);
                    if (!sym) {
                        if (functions_.contains(chain.rootName->name)) {
                            diagnostics_.error(chain.rootName->span,
                                               "function `" + chain.rootName->name +
                                                   "` cannot be used as a value");
                        } else {
                            diagnostics_.error(chain.rootName->span,
                                               "undefined name `" + chain.rootName->name + "`");
                        }
                        return ExprInfo{.type = Type{}, .isConstant = false};
                    }
                    Type walk = sym->type;
                    if (!walk.isFixedArray()) {
                        diagnostics_.error(chain.rootName->span,
                                           "indexed access requires an array value");
                        return ExprInfo{.type = Type{}, .isConstant = false};
                    }
                    if (chain.indices.size() > walk.arrayDimensions.size()) {
                        diagnostics_.error(expr.span, "too many index operations for type `" +
                                                          typeName(sym->type) + "`");
                        return ExprInfo{.type = Type{}, .isConstant = false};
                    }
                    std::vector<ExprInfo> idxInfos;
                    idxInfos.reserve(chain.indices.size());
                    for (const Expr* idxExpr : chain.indices) {
                        ExprInfo ii =
                            analyzeExpr(*idxExpr, builtinScalar(BuiltinTypeKind::I32));
                        idxInfos.push_back(ii);
                        if (!ii.type.isInteger()) {
                            diagnostics_.error(idxExpr->span,
                                               "array index must be an integer type");
                        }
                        if (ii.isConstant && ii.integerValue) {
                            const unsigned long long idxVal = *ii.integerValue;
                            if (walk.arrayDimensions.empty() ||
                                idxVal >= walk.arrayDimensions[0]) {
                                diagnostics_.error(idxExpr->span,
                                                   "array index out of bounds for `" +
                                                       typeName(sym->type) + "`");
                            }
                        }
                        walk = walk.afterIndex();
                    }
                    if (!sym->isConst && sym->type.isFixedArray()) {
                        if (walk.isFixedArray()) {
                            requireSubArraySliceReadable(*sym, sym->type, chain.indices.size(),
                                                         idxInfos, expr.span, chain.rootName->name);
                        } else {
                            requireIndexedScalarReadable(*sym, sym->type, idxInfos, expr.span,
                                                         chain.rootName->name);
                        }
                    }
                    return ExprInfo{.type = walk, .isConstant = false};
                }
            }

            ExprInfo base =
                analyzeExpr(*indexExpr->base, std::nullopt, ExprCtx::IndexBase);
            ExprInfo index =
                analyzeExpr(*indexExpr->index, builtinScalar(BuiltinTypeKind::I32));

            if (!base.type.isFixedArray()) {
                diagnostics_.error(indexExpr->base->span,
                                   "indexed access requires an array value");
                return ExprInfo{.type = Type{}, .isConstant = false};
            }

            if (!index.type.isInteger()) {
                diagnostics_.error(indexExpr->index->span,
                                   "array index must be an integer type");
            }

            if (index.isConstant && index.integerValue) {
                const unsigned long long idxVal = *index.integerValue;
                if (idxVal >= base.type.arrayDimensions[0]) {
                    diagnostics_.error(indexExpr->index->span,
                                       "array index out of bounds for `" +
                                           typeName(base.type) + "`");
                }
            }

            return ExprInfo{.type = base.type.afterIndex(), .isConstant = false};
        }

        if (const auto* paren = dynamic_cast<const ParenExpr*>(&expr)) {
            // Parentheses affect parsing but not semantic type, so forward the
            // expected type into the inner expression.
            return analyzeExpr(*paren->inner, expected, ctx);
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    static bool isFormatSubstitutionType(Type type) {
        return type.isInteger() || type.isBool() || type.isString();
    }

    // `print("…{}…", …)` / `println`: Rust-style placeholders, checked against
    // compile-time string literal (first argument).
    ExprInfo analyzeFormatPrintCall(const CallExpr& call, const std::string& builtinName) {
        const Type voidType = builtinScalar(BuiltinTypeKind::Void);
        const Type strType = builtinScalar(BuiltinTypeKind::Str);
        if (call.arguments.empty()) {
            diagnostics_.error(call.span, "`" + builtinName +
                                              "` requires at least a format string argument");
            return ExprInfo{.type = voidType, .isConstant = false};
        }

        const auto* fmtLit = dynamic_cast<const StringLiteralExpr*>(call.arguments[0].get());
        if (!fmtLit) {
            // `print(x)` / `println(x)` when `x` is `str`: legacy passthrough (no `{}`
            // placeholders). Multiple arguments always require a compile-time format
            // literal so placeholders can be checked.
            if (call.arguments.size() != 1) {
                diagnostics_.error(call.arguments[0]->span,
                                   "`" + builtinName +
                                       "` with multiple arguments requires a string "
                                       "literal format as the first argument");
                for (std::size_t i = 1; i < call.arguments.size(); ++i) {
                    analyzeExpr(*call.arguments[i], std::nullopt);
                }
                return ExprInfo{.type = voidType, .isConstant = false};
            }

            ExprInfo arg = analyzeExpr(*call.arguments[0], strType);
            if (!sameType(arg.type, strType)) {
                diagnostics_.error(call.arguments[0]->span,
                                   "`" + builtinName +
                                       "` expects either a format string literal or "
                                       "one `str` argument");
            }
            return ExprInfo{.type = voidType, .isConstant = false};
        }

        std::string decoded;
        std::string decErr;
        if (!decodeStringLiteralContent(fmtLit->raw, decoded, &decErr)) {
            diagnostics_.error(fmtLit->span, decErr);
            for (std::size_t i = 1; i < call.arguments.size(); ++i) {
                analyzeExpr(*call.arguments[i], std::nullopt);
            }
            return ExprInfo{.type = voidType, .isConstant = false};
        }

        std::vector<std::string> literals;
        std::string splitErr;
        if (!splitFormatString(decoded, literals, splitErr)) {
            diagnostics_.error(fmtLit->span, splitErr);
            for (std::size_t i = 1; i < call.arguments.size(); ++i) {
                analyzeExpr(*call.arguments[i], std::nullopt);
            }
            return ExprInfo{.type = voidType, .isConstant = false};
        }

        const std::size_t holes = literals.size() - 1;
        if (call.arguments.size() != 1 + holes) {
            diagnostics_.error(call.span,
                               "`" + builtinName + "` format string has " +
                                   std::to_string(holes) +
                                   " `{}` placeholder(s), but callsite has " +
                                   std::to_string(call.arguments.size()) +
                                   " argument(s) (expected " + std::to_string(1 + holes) + ")");
        }

        const std::size_t toCheck =
            std::min(call.arguments.size() > 0 ? call.arguments.size() - 1 : 0, holes);
        for (std::size_t i = 0; i < toCheck; ++i) {
            ExprInfo arg = analyzeExpr(*call.arguments[i + 1], std::nullopt);
            if (!arg.type.isInvalid() && !isFormatSubstitutionType(arg.type)) {
                diagnostics_.error(call.arguments[i + 1]->span,
                                   "format argument has type `" + typeName(arg.type) +
                                       "`; supported types are integers, bool, and str");
            }
        }
        for (std::size_t i = 1 + toCheck; i < call.arguments.size(); ++i) {
            analyzeExpr(*call.arguments[i], std::nullopt);
        }

        return ExprInfo{.type = voidType, .isConstant = false};
    }

    // Analyze a direct function call expression.
    //
    // This resolves the callee name, checks argument count and argument types,
    // and returns the function's result type.
    ExprInfo analyzeCallExpr(const CallExpr& call) {
        const auto* callee = dynamic_cast<const NameExpr*>(call.callee.get());
        if (!callee) {
            // The AST can represent a general callee expression, but nex
            // only allows direct calls by function name.
            diagnostics_.error(call.callee->span,
                               "callee must be a function name");
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
        if (symbol.isBuiltin &&
            (callee->name == "print" || callee->name == "println")) {
            return analyzeFormatPrintCall(call, callee->name);
        }

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
            // `!` always produces bool. nex accepts either bool or integer
            // operands as condition-like values.
            ExprInfo operand = analyzeExpr(*unary.operand, std::nullopt);
            if (!canBeCondition(operand.type)) {
                diagnostics_.error(unary.operand->span,
                                   "`!` operand must be `bool` or integer, not `" +
                                       typeName(operand.type) + "`");
            }
            return ExprInfo{.type = builtinScalar(BuiltinTypeKind::Bool),
                            .isConstant = operand.isConstant};
        }

        if (unary.op == TokenKind::Minus) {
            // Unary minus defaults integer literals to i32 unless an outer
            // expression or declaration provides a more specific expected type.
            Type expectedInteger = expected.value_or(builtinScalar(BuiltinTypeKind::I32));
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
            // Logical operators accept condition-like operands and produce bool.
            // Constant folding mirrors runtime short-circuit: a constant false LHS
            // on `&&` (or true LHS on `||`) determines the result without requiring
            // a constant RHS.
            ExprInfo left = analyzeExpr(*binary.left, std::nullopt);
            if (!canBeCondition(left.type)) {
                diagnostics_.error(binary.left->span,
                                   "left operand must be `bool` or integer");
            }
            ExprInfo right = analyzeExpr(*binary.right, std::nullopt);
            if (!canBeCondition(right.type)) {
                diagnostics_.error(binary.right->span,
                                   "right operand must be `bool` or integer");
            }

            auto truth = [](const ExprInfo& e) -> std::optional<bool> {
                if (!e.isConstant || !e.integerValue) {
                    return std::nullopt;
                }
                return *e.integerValue != 0;
            };

            const std::optional<bool> lt = truth(left);
            const std::optional<bool> rt = truth(right);

            bool foldedConst = false;
            std::optional<unsigned long long> outVal;

            if (binary.op == TokenKind::AmpAmp) {
                if (lt && !*lt) {
                    foldedConst = true;
                    outVal = 0;
                } else if (lt && *lt && rt) {
                    foldedConst = true;
                    outVal = *rt ? 1ULL : 0ULL;
                }
            } else {
                if (lt && *lt) {
                    foldedConst = true;
                    outVal = 1;
                } else if (lt && !*lt && rt) {
                    foldedConst = true;
                    outVal = *rt ? 1ULL : 0ULL;
                }
            }

            ExprInfo info{.type = builtinScalar(BuiltinTypeKind::Bool),
                          .isConstant = foldedConst};
            if (foldedConst) {
                info.integerValue = outVal;
            }
            return info;
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
            // helper below also performs the small overflow checks.
            return analyzeConstantArithmetic(binary, left, right);
        }
        if (comparison || equality) {
            // Comparisons and equality produce bool even when their operands are
            // integers.
            return ExprInfo{.type = builtinScalar(BuiltinTypeKind::Bool),
                            .isConstant = left.isConstant && right.isConstant};
        }

        return ExprInfo{.type = Type{}, .isConstant = false};
    }

    // Evaluate enough constant arithmetic to diagnose obvious errors.
    //
    // This helper returns normal ExprInfo either way. Diagnostics record overflow
    // or division-by-zero; the compiler can continue analyzing the rest of the
    // file after reporting them.
    ExprInfo analyzeConstantArithmetic(const BinaryExpr& binary, ExprInfo left,
                                       ExprInfo right) {
        // This is intentionally not a full constant evaluator. It only evaluates
        // simple unsigned integer payloads far enough to diagnose overflow and
        // division/remainder by zero in current tests.
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

    // Lexical loop nesting for `break` / `continue` (both `while` and `for` body).
    int loopDepth_ = 0;
    // True while analyzing the third clause of `for`; `continue` there is invalid.
    bool inForStepClause_ = false;

    // Top-level namespace shared by functions and module constants.
    std::unordered_map<std::string, SourceSpan> topLevelNames_;

    // Function and global value tables collected before body analysis.
    std::unordered_map<std::string, FunctionSymbol> functions_;
    std::unordered_map<std::string, ValueSymbol> globals_;

    // Lexical local scopes for the function currently being analyzed.
    std::vector<std::unordered_map<std::string, ValueSymbol>> scopes_;

    // --- Definite assignment (flow-sensitive) --------------------------------
    //
    // Each non-const local/parameter gets a stable bindingId (see ValueSymbol).
    // `definiteAssign_` holds the ids proven readable at the current point. Module
    // constants skip this: they use isConst and are always readable.
    //
    // Loops use a deliberately conservative rule: body assignments do not
    // strengthen state after the loop (see docs/design/definite_assignment.md).
    std::unordered_set<std::size_t> definiteAssign_;
    DefAssignReasonMap defAssignReasons_;
    ArrayElemMap arrayElemAssign_;
    std::vector<std::vector<std::size_t>> scopeBindingIds_;
    std::size_t nextBindingId_ = 1;

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
