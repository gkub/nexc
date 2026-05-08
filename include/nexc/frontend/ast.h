#pragma once

#include "nexc/frontend/source.h"
#include "nexc/frontend/token.h"

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace nexc {

// BuiltinTypeKind represents the type names Core v0 already understands.
//
// This is still syntax-level information, not the final semantic type system.
// Later semantic analysis can map these syntax nodes to richer Type objects.
enum class BuiltinTypeKind {
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    Bool,
    Str,
    Void,
    Invalid,
};

struct TypeSyntax {
    BuiltinTypeKind kind = BuiltinTypeKind::Invalid;
    SourceSpan span;
};

// Parameters are stored by value because they are small records: name, span, and
// type syntax. Larger recursive structures below use unique_ptr ownership.
struct ParameterSyntax {
    std::string name;
    SourceSpan nameSpan;
    TypeSyntax type;
    SourceSpan span;
};

struct Expr {
    explicit Expr(SourceSpan span) : span(span) {}
    virtual ~Expr() = default;

    SourceSpan span;
};

// Integer literals keep their raw source spelling. Preserving `0x2a` vs `42`
// helps dumps and diagnostics, while numeric interpretation can wait until
// semantic analysis knows the expected type.
struct IntegerLiteralExpr final : Expr {
    IntegerLiteralExpr(SourceSpan span, std::string raw)
        : Expr(span), raw(std::move(raw)) {}

    std::string raw;
};

struct BoolLiteralExpr final : Expr {
    BoolLiteralExpr(SourceSpan span, bool value) : Expr(span), value(value) {}

    bool value = false;
};

struct StringLiteralExpr final : Expr {
    StringLiteralExpr(SourceSpan span, std::string raw)
        : Expr(span), raw(std::move(raw)) {}

    std::string raw;
};

struct NameExpr final : Expr {
    NameExpr(SourceSpan span, std::string name)
        : Expr(span), name(std::move(name)) {}

    std::string name;
};

struct CallExpr final : Expr {
    CallExpr(SourceSpan span, std::unique_ptr<Expr> callee,
             std::vector<std::unique_ptr<Expr>> arguments)
        : Expr(span), callee(std::move(callee)),
          arguments(std::move(arguments)) {}

    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
};

// Unary and binary expressions store TokenKind for the operator instead of a
// string. The parser has already classified the spelling, and later phases can
// switch on a compact enum.
struct UnaryExpr final : Expr {
    UnaryExpr(SourceSpan span, TokenKind op, std::unique_ptr<Expr> operand)
        : Expr(span), op(op), operand(std::move(operand)) {}

    TokenKind op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr final : Expr {
    BinaryExpr(SourceSpan span, std::unique_ptr<Expr> left, TokenKind op,
               std::unique_ptr<Expr> right)
        : Expr(span), left(std::move(left)), op(op), right(std::move(right)) {}

    std::unique_ptr<Expr> left;
    TokenKind op;
    std::unique_ptr<Expr> right;
};

struct ParenExpr final : Expr {
    ParenExpr(SourceSpan span, std::unique_ptr<Expr> inner)
        : Expr(span), inner(std::move(inner)) {}

    std::unique_ptr<Expr> inner;
};

struct Stmt {
    explicit Stmt(SourceSpan span) : span(span) {}
    virtual ~Stmt() = default;

    SourceSpan span;
};

// A block is both a statement and a scope boundary. Scope rules are enforced in
// semantic analysis, but the parser records the nested shape here.
struct BlockStmt final : Stmt {
    explicit BlockStmt(SourceSpan span) : Stmt(span) {}

    std::vector<std::unique_ptr<Stmt>> statements;
};

struct LetStmt final : Stmt {
    LetStmt(SourceSpan span, bool isMutable, std::string name,
            SourceSpan nameSpan, TypeSyntax type, std::unique_ptr<Expr> init)
        : Stmt(span), isMutable(isMutable), name(std::move(name)),
          nameSpan(nameSpan), type(type), init(std::move(init)) {}

    bool isMutable = false;
    std::string name;
    SourceSpan nameSpan;
    TypeSyntax type;
    std::unique_ptr<Expr> init;
};

struct AssignStmt final : Stmt {
    AssignStmt(SourceSpan span, std::string name, SourceSpan nameSpan,
               std::unique_ptr<Expr> value)
        : Stmt(span), name(std::move(name)), nameSpan(nameSpan),
          value(std::move(value)) {}

    std::string name;
    SourceSpan nameSpan;
    std::unique_ptr<Expr> value;
};

struct ReturnStmt final : Stmt {
    ReturnStmt(SourceSpan span, std::unique_ptr<Expr> value)
        : Stmt(span), value(std::move(value)) {}

    std::unique_ptr<Expr> value;
};

struct IfStmt final : Stmt {
    IfStmt(SourceSpan span, std::unique_ptr<Expr> condition,
           std::unique_ptr<Stmt> thenBranch, std::unique_ptr<Stmt> elseBranch)
        : Stmt(span), condition(std::move(condition)),
          thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt final : Stmt {
    WhileStmt(SourceSpan span, std::unique_ptr<Expr> condition,
              std::unique_ptr<Stmt> body)
        : Stmt(span), condition(std::move(condition)), body(std::move(body)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

struct CallStmt final : Stmt {
    CallStmt(SourceSpan span, std::unique_ptr<CallExpr> call)
        : Stmt(span), call(std::move(call)) {}

    std::unique_ptr<CallExpr> call;
};

// Items are top-level declarations in a translation unit. Core v0 only has
// functions and module-level constants.
struct Item {
    explicit Item(SourceSpan span) : span(span) {}
    virtual ~Item() = default;

    SourceSpan span;
};

struct FunctionDecl final : Item {
    FunctionDecl(SourceSpan span, std::string name, SourceSpan nameSpan,
                 std::vector<ParameterSyntax> parameters, TypeSyntax returnType,
                 std::unique_ptr<BlockStmt> body)
        : Item(span), name(std::move(name)), nameSpan(nameSpan),
          parameters(std::move(parameters)), returnType(returnType),
          body(std::move(body)) {}

    std::string name;
    SourceSpan nameSpan;
    std::vector<ParameterSyntax> parameters;
    TypeSyntax returnType;
    std::unique_ptr<BlockStmt> body;
};

struct ConstDecl final : Item {
    ConstDecl(SourceSpan span, std::string name, SourceSpan nameSpan,
              TypeSyntax type, std::unique_ptr<Expr> init)
        : Item(span), name(std::move(name)), nameSpan(nameSpan), type(type),
          init(std::move(init)) {}

    std::string name;
    SourceSpan nameSpan;
    TypeSyntax type;
    std::unique_ptr<Expr> init;
};

// TranslationUnit is the AST root for one source file.
//
// The name is standard compiler terminology: it means "the unit of source code
// this compiler invocation translates." For NEX Core v0, that is simply one
// `.nexs` file containing top-level declarations such as `fn` and `const`.
struct TranslationUnit {
    std::vector<std::unique_ptr<Item>> items;
};

std::string_view builtinTypeName(BuiltinTypeKind kind);
void dumpAst(std::ostream& out, const TranslationUnit& unit);
void dumpAstDot(std::ostream& out, const TranslationUnit& unit);

} // namespace nexc
