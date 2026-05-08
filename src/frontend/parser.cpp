#include "nexc/frontend/parser.h"

#include <utility>

namespace nexc {

Parser::Parser(const SourceFile& source, std::span<const Token> tokens,
               DiagnosticBag& diagnostics)
    : source_(source), tokens_(tokens), diagnostics_(diagnostics) {}

TranslationUnit Parser::parseTranslationUnit() {
    TranslationUnit unit;

    while (!isAtEnd()) {
        if (std::unique_ptr<Item> item = parseItem()) {
            unit.items.push_back(std::move(item));
        } else {
            // This keeps the parser from looping forever after an error. Later,
            // smarter synchronization can skip to the next likely item boundary.
            advance();
        }
    }

    return unit;
}

const Token& Parser::peek(std::size_t offset) const {
    const std::size_t index = current_ + offset;
    if (index >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[index];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::EndOfFile;
}

bool Parser::check(TokenKind kind) const {
    return !isAtEnd() && peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }

    advance();
    return true;
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

Token Parser::expect(TokenKind kind, std::string_view message) {
    if (check(kind)) {
        return advance();
    }

    // `expect` is the parser's basic error-reporting primitive. A missing token
    // is reported at the current token, then a zero-width placeholder token is
    // returned so the caller can still build a partial AST.
    diagnostics_.error(peek().span, std::string(message));
    return Token{.kind = kind, .span = peek().span};
}

std::unique_ptr<Item> Parser::parseItem() {
    if (check(TokenKind::KwFn)) {
        return parseFunctionDecl();
    }

    if (check(TokenKind::KwConst)) {
        return parseConstDecl();
    }

    diagnostics_.error(peek().span,
                       "expected top-level declaration (`fn` or `const`)");
    return nullptr;
}

std::unique_ptr<FunctionDecl> Parser::parseFunctionDecl() {
    const Token fn = expect(TokenKind::KwFn, "expected `fn`");
    const Token name = expect(TokenKind::Identifier, "expected function name");
    expect(TokenKind::LeftParen, "expected `(` after function name");
    std::vector<ParameterSyntax> parameters = parseParameterList();
    expect(TokenKind::RightParen, "expected `)` after parameter list");
    expect(TokenKind::Arrow, "expected `->` before function return type");
    TypeSyntax returnType = parseType();
    std::unique_ptr<BlockStmt> body = parseBlockStmt();

    const SourceSpan span{.start = fn.span.start, .end = body->span.end};
    return std::make_unique<FunctionDecl>(span, tokenText(name), name.span,
                                          std::move(parameters), returnType,
                                          std::move(body));
}

std::unique_ptr<ConstDecl> Parser::parseConstDecl() {
    const Token keyword = expect(TokenKind::KwConst, "expected `const`");
    const Token name = expect(TokenKind::Identifier, "expected constant name");
    expect(TokenKind::Colon, "expected `:` after constant name");
    TypeSyntax type = parseType();
    expect(TokenKind::Equal, "expected `=` in constant declaration");
    std::unique_ptr<Expr> init = parseExpr();
    const Token semicolon =
        expect(TokenKind::Semicolon, "expected `;` after constant declaration");

    const SourceSpan span{.start = keyword.span.start, .end = semicolon.span.end};
    return std::make_unique<ConstDecl>(span, tokenText(name), name.span, type,
                                       std::move(init));
}

std::vector<ParameterSyntax> Parser::parseParameterList() {
    std::vector<ParameterSyntax> parameters;

    // Empty lists are common enough to handle directly: `fn main() -> void`.
    if (check(TokenKind::RightParen)) {
        return parameters;
    }

    while (!isAtEnd()) {
        parameters.push_back(parseParameter());

        if (!match(TokenKind::Comma)) {
            break;
        }

        if (check(TokenKind::RightParen)) {
            diagnostics_.error(peek().span,
                               "trailing commas are not allowed in parameter lists in Core v0");
            break;
        }
    }

    return parameters;
}

ParameterSyntax Parser::parseParameter() {
    const Token name = expect(TokenKind::Identifier, "expected parameter name");
    expect(TokenKind::Colon, "expected `:` after parameter name");
    TypeSyntax type = parseType();

    return ParameterSyntax{
        .name = tokenText(name),
        .nameSpan = name.span,
        .type = type,
        .span = SourceSpan{.start = name.span.start, .end = type.span.end},
    };
}

TypeSyntax Parser::parseType() {
    const Token token = advance();
    const BuiltinTypeKind kind = builtinTypeKind(token);

    // `i32` and friends lex as identifiers, then become builtin type syntax only
    // in this parser context. This keeps the lexer from needing to know every
    // possible future type name.
    if (kind == BuiltinTypeKind::Invalid) {
        diagnostics_.error(token.span, "expected Core v0 scalar type");
    }

    return TypeSyntax{.kind = kind, .span = token.span};
}

std::unique_ptr<BlockStmt> Parser::parseBlockStmt() {
    const Token leftBrace = expect(TokenKind::LeftBrace, "expected `{`");
    auto block = std::make_unique<BlockStmt>(
        SourceSpan{.start = leftBrace.span.start, .end = leftBrace.span.end});

    while (!isAtEnd() && !check(TokenKind::RightBrace)) {
        if (std::unique_ptr<Stmt> stmt = parseStmt()) {
            block->statements.push_back(std::move(stmt));
        } else if (!check(TokenKind::RightBrace)) {
            advance();
        }
    }

    const Token rightBrace = expect(TokenKind::RightBrace, "expected `}`");
    block->span.end = rightBrace.span.end;
    return block;
}

std::unique_ptr<Stmt> Parser::parseStmt() {
    if (check(TokenKind::LeftBrace)) {
        return parseBlockStmt();
    }
    if (check(TokenKind::KwLet)) {
        return parseLetStmt();
    }
    if (check(TokenKind::KwReturn)) {
        return parseReturnStmt();
    }
    if (check(TokenKind::KwIf)) {
        return parseIfStmt();
    }
    if (check(TokenKind::KwWhile)) {
        return parseWhileStmt();
    }
    if (check(TokenKind::KwConst)) {
        diagnostics_.error(peek().span,
                           "inner `const` declarations are not part of Core v0");
        return nullptr;
    }
    if (check(TokenKind::Identifier) || check(TokenKind::IntegerLiteral) ||
        check(TokenKind::KwTrue) || check(TokenKind::KwFalse) ||
        check(TokenKind::LeftParen) || check(TokenKind::Minus) ||
        check(TokenKind::Bang)) {
        return parseAssignmentOrCallStmt();
    }

    diagnostics_.error(peek().span, "expected statement");
    return nullptr;
}

std::unique_ptr<Stmt> Parser::parseLetStmt() {
    const Token keyword = expect(TokenKind::KwLet, "expected `let`");
    const bool isMutable = match(TokenKind::KwMut);
    const Token name = expect(TokenKind::Identifier, "expected local name");
    expect(TokenKind::Colon, "expected `:` after local name");
    TypeSyntax type = parseType();
    expect(TokenKind::Equal, "expected `=` in local declaration");
    std::unique_ptr<Expr> init = parseExpr();
    const Token semicolon =
        expect(TokenKind::Semicolon, "expected `;` after local declaration");

    return std::make_unique<LetStmt>(
        SourceSpan{.start = keyword.span.start, .end = semicolon.span.end},
        isMutable, tokenText(name), name.span, type, std::move(init));
}

std::unique_ptr<Stmt> Parser::parseReturnStmt() {
    const Token keyword = expect(TokenKind::KwReturn, "expected `return`");

    if (match(TokenKind::Semicolon)) {
        return std::make_unique<ReturnStmt>(
            SourceSpan{.start = keyword.span.start, .end = previous().span.end},
            nullptr);
    }

    std::unique_ptr<Expr> value = parseExpr();
    const Token semicolon =
        expect(TokenKind::Semicolon, "expected `;` after return statement");

    return std::make_unique<ReturnStmt>(
        SourceSpan{.start = keyword.span.start, .end = semicolon.span.end},
        std::move(value));
}

std::unique_ptr<Stmt> Parser::parseIfStmt() {
    const Token keyword = expect(TokenKind::KwIf, "expected `if`");
    expect(TokenKind::LeftParen, "expected `(` after `if`");
    std::unique_ptr<Expr> condition = parseExpr();
    expect(TokenKind::RightParen, "expected `)` after if condition");
    std::unique_ptr<Stmt> thenBranch = parseStmt();
    std::unique_ptr<Stmt> elseBranch;

    if (match(TokenKind::KwElse)) {
        // Because the then-branch is parsed before looking for `else`, the
        // standard dangling-else rule falls out naturally: an `else` attaches to
        // the innermost if that has not already consumed one.
        elseBranch = parseStmt();
    }

    const std::size_t end =
        elseBranch ? elseBranch->span.end : thenBranch ? thenBranch->span.end : keyword.span.end;

    return std::make_unique<IfStmt>(
        SourceSpan{.start = keyword.span.start, .end = end}, std::move(condition),
        std::move(thenBranch), std::move(elseBranch));
}

std::unique_ptr<Stmt> Parser::parseWhileStmt() {
    const Token keyword = expect(TokenKind::KwWhile, "expected `while`");
    expect(TokenKind::LeftParen, "expected `(` after `while`");
    std::unique_ptr<Expr> condition = parseExpr();
    expect(TokenKind::RightParen, "expected `)` after while condition");
    std::unique_ptr<Stmt> body = parseStmt();

    const std::size_t end = body ? body->span.end : keyword.span.end;
    return std::make_unique<WhileStmt>(
        SourceSpan{.start = keyword.span.start, .end = end}, std::move(condition),
        std::move(body));
}

std::unique_ptr<Stmt> Parser::parseAssignmentOrCallStmt() {
    if (peek(1).kind == TokenKind::Equal) {
        // A single token of lookahead is enough for Core v0 assignment:
        // identifier followed by `=`. More complex "place" syntax, such as
        // indexing or field access, will require revisiting this function.
        const Token name = advance();
        advance();
        std::unique_ptr<Expr> value = parseExpr();
        const Token semicolon =
            expect(TokenKind::Semicolon, "expected `;` after assignment");
        return std::make_unique<AssignStmt>(
            SourceSpan{.start = name.span.start, .end = semicolon.span.end},
            tokenText(name), name.span, std::move(value));
    }

    std::unique_ptr<Expr> expr = parseExpr();
    const Token semicolon =
        expect(TokenKind::Semicolon, "expected `;` after call statement");

    auto* call = dynamic_cast<CallExpr*>(expr.get());
    if (!call) {
        // This is a syntactic restriction from the frontend contract. The parser
        // allows `foo();` but rejects `1 + 2;`. Whether `foo` returns void is a
        // semantic question because it requires symbol lookup.
        diagnostics_.error(expr->span,
                           "only call expressions may be used as expression statements in Core v0");
        return nullptr;
    }

    // The expression parser produced ownership as unique_ptr<Expr>. Once this is
    // known to be a CallExpr, ownership is transferred into the more precise
    // CallStmt node.
    expr.release();
    return std::make_unique<CallStmt>(
        SourceSpan{.start = call->span.start, .end = semicolon.span.end},
        std::unique_ptr<CallExpr>(call));
}

std::unique_ptr<Expr> Parser::parseExpr(int minPrecedence) {
    std::unique_ptr<Expr> left = parseUnaryExpr();

    while (true) {
        const int precedence = binaryPrecedence(peek().kind);
        if (precedence < minPrecedence) {
            break;
        }

        const Token op = advance();

        // `precedence + 1` makes Core v0 binary operators left-associative.
        // Example: `a - b - c` parses as `(a - b) - c`, not `a - (b - c)`.
        std::unique_ptr<Expr> right = parseExpr(precedence + 1);
        const SourceSpan span{.start = left->span.start, .end = right->span.end};
        left = std::make_unique<BinaryExpr>(span, std::move(left), op.kind,
                                            std::move(right));
    }

    return left;
}

std::unique_ptr<Expr> Parser::parseUnaryExpr() {
    if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
        const Token op = advance();

        // Unary operators recurse into parseUnaryExpr so chains like `!!x` and
        // `--x` associate from right to left: `!(!x)`.
        std::unique_ptr<Expr> operand = parseUnaryExpr();
        return std::make_unique<UnaryExpr>(
            SourceSpan{.start = op.span.start, .end = operand->span.end}, op.kind,
            std::move(operand));
    }

    return parsePostfixExpr();
}

std::unique_ptr<Expr> Parser::parsePostfixExpr() {
    std::unique_ptr<Expr> expr = parsePrimaryExpr();

    while (match(TokenKind::LeftParen)) {
        // Calls are postfix operators: first parse the callee expression, then
        // attach argument lists that follow it. This gives calls the highest
        // precedence in Core v0.
        std::vector<std::unique_ptr<Expr>> arguments = parseArgumentList();
        const Token rightParen =
            expect(TokenKind::RightParen, "expected `)` after argument list");
        const SourceSpan span{.start = expr->span.start, .end = rightParen.span.end};
        expr = std::make_unique<CallExpr>(span, std::move(expr),
                                          std::move(arguments));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimaryExpr() {
    if (match(TokenKind::IntegerLiteral)) {
        const Token token = previous();
        return std::make_unique<IntegerLiteralExpr>(token.span, tokenText(token));
    }

    if (match(TokenKind::KwTrue)) {
        return std::make_unique<BoolLiteralExpr>(previous().span, true);
    }

    if (match(TokenKind::KwFalse)) {
        return std::make_unique<BoolLiteralExpr>(previous().span, false);
    }

    if (match(TokenKind::Identifier)) {
        const Token token = previous();
        return std::make_unique<NameExpr>(token.span, tokenText(token));
    }

    if (match(TokenKind::LeftParen)) {
        const Token leftParen = previous();
        std::unique_ptr<Expr> inner = parseExpr();
        const Token rightParen =
            expect(TokenKind::RightParen, "expected `)` after parenthesized expression");
        return std::make_unique<ParenExpr>(
            SourceSpan{.start = leftParen.span.start, .end = rightParen.span.end},
            std::move(inner));
    }

    diagnostics_.error(peek().span, "expected expression");
    const Token token = advance();
    return std::make_unique<NameExpr>(token.span, "<error>");
}

std::vector<std::unique_ptr<Expr>> Parser::parseArgumentList() {
    std::vector<std::unique_ptr<Expr>> arguments;

    if (check(TokenKind::RightParen)) {
        return arguments;
    }

    while (!isAtEnd()) {
        arguments.push_back(parseExpr());

        if (!match(TokenKind::Comma)) {
            break;
        }

        if (check(TokenKind::RightParen)) {
            diagnostics_.error(peek().span,
                               "trailing commas are not allowed in call arguments in Core v0");
            break;
        }
    }

    return arguments;
}

int Parser::binaryPrecedence(TokenKind kind) const {
    // Larger numbers bind more tightly. Returning 0 means "not a binary
    // operator at the current expression position."
    switch (kind) {
    case TokenKind::PipePipe:
        return 1;
    case TokenKind::AmpAmp:
        return 2;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
        return 3;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
        return 4;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
        return 6;
    default:
        return 0;
    }
}

BuiltinTypeKind Parser::builtinTypeKind(const Token& token) const {
    if (token.kind == TokenKind::KwBool) {
        return BuiltinTypeKind::Bool;
    }
    if (token.kind == TokenKind::KwVoid) {
        return BuiltinTypeKind::Void;
    }

    if (token.kind != TokenKind::Identifier) {
        return BuiltinTypeKind::Invalid;
    }

    const std::string text = tokenText(token);
    if (text == "i8") {
        return BuiltinTypeKind::I8;
    }
    if (text == "i16") {
        return BuiltinTypeKind::I16;
    }
    if (text == "i32") {
        return BuiltinTypeKind::I32;
    }
    if (text == "i64") {
        return BuiltinTypeKind::I64;
    }
    if (text == "u8") {
        return BuiltinTypeKind::U8;
    }
    if (text == "u16") {
        return BuiltinTypeKind::U16;
    }
    if (text == "u32") {
        return BuiltinTypeKind::U32;
    }
    if (text == "u64") {
        return BuiltinTypeKind::U64;
    }

    return BuiltinTypeKind::Invalid;
}

std::string Parser::tokenText(const Token& token) const {
    return std::string(source_.slice(token.span));
}

} // namespace nexc
