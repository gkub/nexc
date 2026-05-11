#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/frontend/diagnostic.h"
#include "nexc/frontend/token.h"

#include <memory>
#include <span>

namespace nexc {

// The parser turns a flat token stream into a tree-shaped AST.
//
// It checks syntax only. It can decide that `fn f() -> i32 { return true; }`
// has the right grammatical shape, but it cannot decide whether returning bool
// from an i32 function is legal. That later meaning check belongs to semantic
// analysis.
class Parser {
public:
    Parser(const SourceFile& source, std::span<const Token> tokens,
           DiagnosticBag& diagnostics);

    TranslationUnit parseTranslationUnit();

private:
    // Token cursor helpers. These mirror the lexer's character cursor, but at a
    // higher level: the parser moves through tokens rather than bytes.
    const Token& peek(std::size_t offset = 0) const;
    const Token& previous() const;
    bool isAtEnd() const;
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& advance();

    // Consume a required token or report a syntax error at the current token.
    // The returned placeholder lets simple parsing continue after an error.
    Token expect(TokenKind kind, std::string_view message);

    // Recursive descent routines generally mirror grammar rules: parse an item,
    // parse a function, parse a statement, and so on.
    std::unique_ptr<Item> parseItem();
    std::unique_ptr<FunctionDecl> parseFunctionDecl();
    std::unique_ptr<ConstDecl> parseConstDecl();
    std::vector<ParameterSyntax> parseParameterList();
    ParameterSyntax parseParameter();
    TypeSyntax parseType();

    std::unique_ptr<BlockStmt> parseBlockStmt();
    std::unique_ptr<Stmt> parseStmt();
    std::unique_ptr<Stmt> parseLetStmt();
    std::unique_ptr<Stmt> parseReturnStmt();
    std::unique_ptr<Stmt> parseIfStmt();
    std::unique_ptr<Stmt> parseWhileStmt();
    std::unique_ptr<Stmt> parseForStmt();

    // `for` step clause ends at `)` (no `;` after assignment or void call).
    std::unique_ptr<Stmt> parseForStepClause();

    // In Core v0, statements beginning with an identifier have one-token
    // ambiguity: `x = ...` is assignment, while `f(...)` is a call statement.
    // Other expression starts are parsed here too so invalid expression
    // statements such as `1 + 2;` get one focused diagnostic.
    std::unique_ptr<Stmt> parseAssignmentOrCallStmt();

    // Expressions use precedence climbing. `minPrecedence` means "only parse
    // binary operators at least this strong at the current recursion level."
    std::unique_ptr<Expr> parseExpr(int minPrecedence = 1);
    std::unique_ptr<Expr> parseUnaryExpr();
    std::unique_ptr<Expr> parsePostfixExpr();
    std::unique_ptr<Expr> parsePrimaryExpr();
    std::vector<std::unique_ptr<Expr>> parseArgumentList();

    int binaryPrecedence(TokenKind kind) const;
    BuiltinTypeKind builtinTypeKind(const Token& token) const;
    std::string tokenText(const Token& token) const;

    const SourceFile& source_;
    std::span<const Token> tokens_;
    DiagnosticBag& diagnostics_;
    std::size_t current_ = 0;
};

} // namespace nexc
