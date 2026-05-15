#include "nexc/frontend/parser.h"

#include <cstdint>
#include <string>
#include <utility>

namespace nexc {

// Create a parser over an already-tokenized source file.
//
// The parser borrows tokens rather than owning them because tokenization is a
// separate compiler stage. It also borrows SourceFile so it can recover token
// text for identifiers and literals while building AST nodes.
Parser::Parser(const SourceFile& source, std::span<const Token> tokens,
               DiagnosticBag& diagnostics)
    : source_(source), tokens_(tokens), diagnostics_(diagnostics) {}

// Parse the whole file into the AST root.
//
// A TranslationUnit is the parser's representation of one source file. nex
// only permits top-level declarations, so this loop repeatedly parses items
// until it reaches the explicit EOF token.
TranslationUnit Parser::parseTranslationUnit() {
    TranslationUnit unit;

    // The translation unit is the parser's root. nex accepts only top-level
    // items here, so each loop iteration should consume one `fn` or `const`.
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

// Look ahead in the token stream without consuming.
//
// Recursive-descent parsers use lookahead to decide which grammar branch to take
// before committing. nex usually needs only one token of lookahead.
const Token& Parser::peek(std::size_t offset) const {
    const std::size_t index = current_ + offset;
    if (index >= tokens_.size()) {
        // The lexer always appends EOF. Returning it for out-of-range lookahead
        // lets parser code ask for peek(1) without special vector-bound checks.
        return tokens_.back();
    }
    return tokens_[index];
}

// Return the token most recently consumed by advance().
//
// This is useful after match(): if match(KwTrue) succeeds, previous() is the
// exact token span used to build the BoolLiteralExpr.
const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

// Return true when the current token is the EOF sentinel from the lexer.
bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::EndOfFile;
}

// Test whether the current token has a specific kind without consuming it.
bool Parser::check(TokenKind kind) const {
    return !isAtEnd() && peek().kind == kind;
}

// Consume the current token if it has the requested kind.
//
// Returns true on success and false without side effects otherwise. This keeps
// grammar code compact for optional tokens such as `else` or commas.
bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }

    advance();
    return true;
}

// Consume one token and return it.
//
// advance() never moves past EOF, so repeated recovery calls remain safe even
// after a syntax error near the end of the file.
const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

// Consume a required token or report a syntax error.
//
// The placeholder token lets parsing continue and build a partial AST. That is
// important for friendly diagnostics because one missing `)` should not prevent
// every later parser check from running.
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

// Parse one top-level item.
//
// In nex today an item is either a function declaration or a module constant.
// Statements are intentionally not allowed at file scope.
std::unique_ptr<Item> Parser::parseItem() {
    // Top-level parsing is intentionally strict. A stray statement at module
    // scope should be diagnosed as an item-level error, not parsed and rejected
    // later by semantic analysis.
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

// Parse a function declaration and body.
//
// The AST keeps the syntactic pieces separate: name, parameters, return type,
// and body. Later semantic analysis decides whether the signature is valid.
std::unique_ptr<FunctionDecl> Parser::parseFunctionDecl() {
    // Function parsing follows the concrete source order:
    // `fn name(params) -> return_type body`.
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

// Parse a module-level const declaration.
//
// The parser only checks the syntax shape. Semantic analysis later verifies that
// the initializer is type-correct and compile-time evaluable.
std::unique_ptr<ConstDecl> Parser::parseConstDecl() {
    // Constants are module-level only. The parser handles that by only
    // calling this routine from parseItem(); inner `const` is rejected in
    // parseStmt().
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

// Parse the comma-separated parameter list inside function parentheses.
//
// The surrounding parseFunctionDecl() has already consumed `(` and will consume
// `)`. This helper only owns the list contents.
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
                               "trailing commas are not allowed in parameter lists");
            break;
        }
    }

    return parameters;
}

// Parse one `name: Type` parameter entry.
//
// ParameterSyntax is still syntax, not a resolved semantic symbol. The semantic
// pass later turns it into an immutable local binding.
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

// Parse a nex built-in type name.
//
// User-defined types do not exist yet, so every accepted type is represented by
// BuiltinTypeKind. Invalid type syntax still produces a TypeSyntax placeholder so
// parsing can continue.
TypeSyntax Parser::parseType() {
    if (check(TokenKind::LeftBracket)) {
        const Token lb = advance();
        TypeSyntax inner = parseType();
        expect(TokenKind::Semicolon, "expected `;` in `[T; N]`");
        const Token lenTok =
            expect(TokenKind::IntegerLiteral, "expected compile-time array length");
        expect(TokenKind::RightBracket, "expected `]` after array length");

        const std::string lenText = tokenText(lenTok);
        std::size_t consumed = 0;
        const unsigned long long lenVal = std::stoull(lenText, &consumed, 0);
        if (consumed != lenText.size() || lenVal == 0ULL) {
            diagnostics_.error(lenTok.span,
                                 "array length must be a positive integer literal");
        }

        if (inner.kind == BuiltinTypeKind::Invalid || inner.kind == BuiltinTypeKind::Void ||
            inner.kind == BuiltinTypeKind::Str) {
            diagnostics_.error(inner.span,
                                 "array element type cannot be `void`, `str`, or invalid");
        }

        TypeSyntax out;
        out.kind = inner.kind;
        out.arrayDimensions = inner.arrayDimensions;
        out.arrayDimensions.insert(out.arrayDimensions.begin(),
                                   static_cast<std::uint64_t>(lenVal));
        out.span = SourceSpan{.start = lb.span.start, .end = previous().span.end};
        return out;
    }

    const Token token = advance();
    const BuiltinTypeKind kind = builtinTypeKind(token);

    if (kind == BuiltinTypeKind::Invalid) {
        diagnostics_.error(token.span, "expected scalar type");
    }

    return TypeSyntax{.kind = kind, .arrayDimensions = {}, .span = token.span};
}

// Parse a `{ ... }` statement block.
//
// Blocks collect statements and keep a span from the opening brace through the
// closing brace. Semantic analysis later gives blocks lexical-scope meaning.
std::unique_ptr<BlockStmt> Parser::parseBlockStmt() {
    const Token leftBrace = expect(TokenKind::LeftBrace, "expected `{`");
    auto block = std::make_unique<BlockStmt>(
        SourceSpan{.start = leftBrace.span.start, .end = leftBrace.span.end});

    while (!isAtEnd() && !check(TokenKind::RightBrace)) {
        if (std::unique_ptr<Stmt> stmt = parseStmt()) {
            block->statements.push_back(std::move(stmt));
        } else if (!check(TokenKind::RightBrace)) {
            // Minimal recovery: consume one token after a failed statement so
            // the parser can keep looking for the closing brace.
            advance();
        }
    }

    const Token rightBrace = expect(TokenKind::RightBrace, "expected `}`");
    block->span.end = rightBrace.span.end;
    return block;
}

// Parse one statement inside a function body or nested block.
//
// This is the statement-level dispatch table for nex. Each branch mirrors a
// grammar production such as let-statement, return-statement, or if-statement.
std::unique_ptr<Stmt> Parser::parseStmt() {
    // Statement dispatch is based on the first token. This is recursive descent:
    // each source construct has a small parser routine that mirrors the grammar.
    if (check(TokenKind::LeftBrace)) {
        return parseBlockStmt();
    }
    if (check(TokenKind::KwLet)) {
        return parseLetStmt();
    }
    if (check(TokenKind::KwReturn)) {
        return parseReturnStmt();
    }
    if (check(TokenKind::KwBreak)) {
        const Token keyword = expect(TokenKind::KwBreak, "expected `break`");
        const Token semicolon =
            expect(TokenKind::Semicolon, "expected `;` after `break`");
        return std::make_unique<BreakStmt>(
            SourceSpan{.start = keyword.span.start, .end = semicolon.span.end});
    }
    if (check(TokenKind::KwContinue)) {
        const Token keyword = expect(TokenKind::KwContinue, "expected `continue`");
        const Token semicolon =
            expect(TokenKind::Semicolon, "expected `;` after `continue`");
        return std::make_unique<ContinueStmt>(
            SourceSpan{.start = keyword.span.start, .end = semicolon.span.end});
    }
    if (check(TokenKind::KwIf)) {
        return parseIfStmt();
    }
    if (check(TokenKind::KwWhile)) {
        return parseWhileStmt();
    }
    if (check(TokenKind::KwFor)) {
        return parseForStmt();
    }
    if (check(TokenKind::KwConst)) {
        diagnostics_.error(peek().span,
                           "inner `const` declarations are not supported yet");
        return nullptr;
    }
    if (check(TokenKind::Identifier) || check(TokenKind::IntegerLiteral) ||
        check(TokenKind::StringLiteral) || check(TokenKind::KwTrue) ||
        check(TokenKind::KwFalse) || check(TokenKind::LeftParen) ||
        check(TokenKind::LeftBracket) || check(TokenKind::Minus) ||
        check(TokenKind::Bang)) {
        return parseAssignmentOrCallStmt();
    }

    diagnostics_.error(peek().span, "expected statement");
    return nullptr;
}

// Parse `let` or `let mut`.
//
// The parser records mutability as a flag but does not enforce reassignment
// rules; that belongs to semantic analysis where names can be resolved.
std::unique_ptr<Stmt> Parser::parseLetStmt() {
    // `let mut` is represented as one LetStmt node with an isMutable flag. The
    // parser records the syntax; semantic analysis enforces assignment rules.
    const Token keyword = expect(TokenKind::KwLet, "expected `let`");
    const bool isMutable = match(TokenKind::KwMut);
    const Token name = expect(TokenKind::Identifier, "expected local name");
    expect(TokenKind::Colon, "expected `:` after local name");
    TypeSyntax type = parseType();
    std::unique_ptr<Expr> init;
    if (match(TokenKind::Equal)) {
        init = parseExpr();
    } else {
        if (!isMutable) {
            diagnostics_.error(
                peek().span,
                "`let` requires an initializer expression after `=`; use `let mut` "
                "to declare mutable storage without an initializer");
        }
    }
    const Token semicolon =
        expect(TokenKind::Semicolon, "expected `;` after local declaration");

    return std::make_unique<LetStmt>(
        SourceSpan{.start = keyword.span.start, .end = semicolon.span.end},
        isMutable, tokenText(name), name.span, type, std::move(init));
}

// Parse `return;` or `return expr;`.
//
// The AST distinguishes those forms by storing an optional expression. Semantic
// analysis then compares the form against the current function return type.
std::unique_ptr<Stmt> Parser::parseReturnStmt() {
    const Token keyword = expect(TokenKind::KwReturn, "expected `return`");

    if (match(TokenKind::Semicolon)) {
        // `return;` is represented by a ReturnStmt with no value expression.
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

// Parse an `if (condition) then [else else]` statement.
//
// The then/else bodies are general statements, not only blocks, which matches C
// style syntax and naturally supports `if (...) return 1;`.
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

// Parse a `while (condition) body` statement.
//
// The parser only captures shape. Semantic analysis checks that the condition is
// bool/integer, and lowering later decides how to represent the loop.
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

// Parse `for (init; condition; step) body`.
//
// Each `;` separates clauses. Omitted `init` / `condition` / `step` are
// represented as null fields on `ForStmt`. The step clause ends at `)` without a
// trailing semicolon, matching C-family syntax.
std::unique_ptr<Stmt> Parser::parseForStmt() {
    const Token keyword = expect(TokenKind::KwFor, "expected `for`");
    expect(TokenKind::LeftParen, "expected `(` after `for`");

    std::unique_ptr<Stmt> init;
    if (check(TokenKind::Semicolon)) {
        advance();
    } else if (check(TokenKind::KwLet)) {
        init = parseLetStmt();
    } else {
        init = parseAssignmentOrCallStmt();
        if (!init) {
            while (!isAtEnd() && !check(TokenKind::Semicolon)) {
                advance();
            }
            if (check(TokenKind::Semicolon)) {
                advance();
            }
        }
    }

    std::unique_ptr<Expr> condition;
    if (check(TokenKind::Semicolon)) {
        advance();
    } else {
        condition = parseExpr();
        expect(TokenKind::Semicolon, "expected `;` after for condition");
    }

    std::unique_ptr<Stmt> step = parseForStepClause();
    if (!step) {
        expect(TokenKind::RightParen, "expected `)` after for clauses");
    }

    std::unique_ptr<Stmt> body = parseStmt();
    const std::size_t end = body ? body->span.end : keyword.span.end;
    return std::make_unique<ForStmt>(
        SourceSpan{.start = keyword.span.start, .end = end}, std::move(init),
        std::move(condition), std::move(step), std::move(body));
}

// Parse the third clause of a `for` header: assignment, void call, or empty.
//
// When empty, the parser leaves the closing `)` unconsumed for `parseForStmt` to
// match uniformly.
std::unique_ptr<Stmt> Parser::parseForStepClause() {
    if (check(TokenKind::RightParen)) {
        return nullptr;
    }

    const std::size_t stmtStart = peek().span.start;
    std::unique_ptr<Expr> lhs = parsePostfixExpr();

    if (match(TokenKind::Equal)) {
        if (!dynamic_cast<const NameExpr*>(lhs.get()) &&
            !dynamic_cast<const IndexExpr*>(lhs.get())) {
            diagnostics_.error(lhs->span,
                               "for step assignment target must be a local name or "
                               "indexed place");
            parseExpr();
            if (!check(TokenKind::RightParen)) {
                expect(TokenKind::RightParen, "expected `)` after for step");
            } else {
                advance();
            }
            return nullptr;
        }

        std::unique_ptr<Expr> rhs = parseExpr();
        const Token closing =
            expect(TokenKind::RightParen, "expected `)` after for step");
        return std::make_unique<AssignStmt>(
            SourceSpan{.start = stmtStart, .end = closing.span.end}, std::move(lhs),
            std::move(rhs));
    }

    auto* call = dynamic_cast<CallExpr*>(lhs.get());
    if (!call) {
        diagnostics_.error(lhs->span,
                           "for step must be an assignment or a void call expression");
        if (!check(TokenKind::RightParen)) {
            expect(TokenKind::RightParen, "expected `)` after for step");
        } else {
            advance();
        }
        return nullptr;
    }

    lhs.release();
    const Token closing = expect(TokenKind::RightParen, "expected `)` after for step");
    return std::make_unique<CallStmt>(
        SourceSpan{.start = stmtStart, .end = closing.span.end},
        std::unique_ptr<CallExpr>(call));
}

// Parse the statement forms that start like expressions.
//
// An identifier followed by `=` is assignment. Otherwise nex allows only a
// call expression as a statement. This function exists because both forms begin
// with expression-looking tokens.
std::unique_ptr<Stmt> Parser::parseAssignmentOrCallStmt() {
    const std::size_t stmtStart = peek().span.start;
    std::unique_ptr<Expr> lhs = parsePostfixExpr();

    if (match(TokenKind::Equal)) {
        if (!dynamic_cast<const NameExpr*>(lhs.get()) &&
            !dynamic_cast<const IndexExpr*>(lhs.get())) {
            diagnostics_.error(lhs->span,
                               "assignment target must be a local name or indexed place");
            parseExpr();
            expect(TokenKind::Semicolon, "expected `;` after assignment");
            return nullptr;
        }

        std::unique_ptr<Expr> rhs = parseExpr();
        const Token semicolon =
            expect(TokenKind::Semicolon, "expected `;` after assignment");
        return std::make_unique<AssignStmt>(
            SourceSpan{.start = stmtStart, .end = semicolon.span.end}, std::move(lhs),
            std::move(rhs));
    }

    const Token semicolon =
        expect(TokenKind::Semicolon, "expected `;` after expression statement");

    auto* call = dynamic_cast<CallExpr*>(lhs.get());
    if (!call) {
        diagnostics_.error(lhs->span,
                           "only call expressions may be used as expression statements");
        return nullptr;
    }

    lhs.release();
    return std::make_unique<CallStmt>(
        SourceSpan{.start = stmtStart, .end = semicolon.span.end},
        std::unique_ptr<CallExpr>(call));
}

// Parse an expression using precedence climbing.
//
// minPrecedence says "do not consume operators looser than this." Recursive
// calls with higher minimum precedence produce the usual grouping for arithmetic,
// comparison, equality, and logical operators.
std::unique_ptr<Expr> Parser::parseExpr(int minPrecedence) {
    std::unique_ptr<Expr> left = parseUnaryExpr();

    while (true) {
        const int precedence = binaryPrecedence(peek().kind);
        if (precedence < minPrecedence) {
            break;
        }

        const Token op = advance();

        // `precedence + 1` makes binary operators left-associative.
        // Example: `a - b - c` parses as `(a - b) - c`, not `a - (b - c)`.
        std::unique_ptr<Expr> right = parseExpr(precedence + 1);
        const SourceSpan span{.start = left->span.start, .end = right->span.end};
        left = std::make_unique<BinaryExpr>(span, std::move(left), op.kind,
                                            std::move(right));
    }

    return left;
}

// Parse prefix unary operators before falling through to calls/primary forms.
//
// Prefix operators bind more tightly than binary operators, so they are parsed
// before parseExpr starts consuming infix operators.
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

// Parse postfix expression forms such as function calls.
//
// Starting from a primary expression, repeatedly attach argument lists. This
// supports `f()(x)` syntactically even if semantic analysis later rejects
// non-name callees.
std::unique_ptr<Expr> Parser::parsePostfixExpr() {
    std::unique_ptr<Expr> expr = parsePrimaryExpr();

    while (true) {
        if (match(TokenKind::LeftParen)) {
            std::vector<std::unique_ptr<Expr>> arguments = parseArgumentList();
            const Token rightParen =
                expect(TokenKind::RightParen, "expected `)` after argument list");
            const SourceSpan span{.start = expr->span.start, .end = rightParen.span.end};
            expr = std::make_unique<CallExpr>(span, std::move(expr),
                                              std::move(arguments));
            continue;
        }

        if (match(TokenKind::LeftBracket)) {
            std::unique_ptr<Expr> index = parseExpr();
            const Token rb =
                expect(TokenKind::RightBracket, "expected `]` after index expression");
            const SourceSpan span{.start = expr->span.start, .end = rb.span.end};
            expr = std::make_unique<IndexExpr>(span, std::move(expr), std::move(index));
            continue;
        }

        break;
    }

    return expr;
}

// Parse the atomic expression forms.
//
// Primaries are the leaves that larger unary, call, and binary expressions are
// built from: literals, names, and parenthesized expressions.
std::unique_ptr<Expr> Parser::parsePrimaryExpr() {
    // Primary expressions are the leaves of the expression grammar: literals,
    // names, and parenthesized subexpressions.
    if (match(TokenKind::IntegerLiteral)) {
        const Token token = previous();
        return std::make_unique<IntegerLiteralExpr>(token.span, tokenText(token));
    }

    if (match(TokenKind::StringLiteral)) {
        const Token token = previous();
        return std::make_unique<StringLiteralExpr>(token.span, tokenText(token));
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

    if (match(TokenKind::LeftBracket)) {
        const Token lb = previous();
        std::vector<std::unique_ptr<Expr>> elements;

        if (!check(TokenKind::RightBracket)) {
            while (true) {
                elements.push_back(parseExpr());
                if (!match(TokenKind::Comma)) {
                    break;
                }
                if (check(TokenKind::RightBracket)) {
                    diagnostics_.error(peek().span,
                                       "trailing commas are not allowed in array literals");
                    break;
                }
            }
        }

        const Token rb =
            expect(TokenKind::RightBracket, "expected `]` to close array literal");
        return std::make_unique<ArrayLiteralExpr>(
            SourceSpan{.start = lb.span.start, .end = rb.span.end}, std::move(elements));
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
    // Return a placeholder expression so callers can keep building a partial
    // tree after reporting the syntax error.
    return std::make_unique<NameExpr>(token.span, "<error>");
}

// Parse the comma-separated expression list inside a call.
//
// The caller has already consumed `(` and will consume the final `)`. This
// helper intentionally rejects trailing commas for the current grammar.
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
                               "trailing commas are not allowed in call arguments");
            break;
        }
    }

    return arguments;
}

// Return the binding power for a binary operator token.
//
// Precedence climbing depends on larger numbers binding more tightly. Tokens
// that are not binary operators return 0 so parseExpr stops consuming.
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

// Interpret a token as a builtin type name in type syntax context.
//
// Some type names, such as `i32` and `str`, lex as identifiers because they are
// not globally reserved keywords. This parser-context check is what makes them
// type names after `:` or `->`.
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
    if (text == "str") {
        return BuiltinTypeKind::Str;
    }

    return BuiltinTypeKind::Invalid;
}

// Copy the source spelling covered by a token span.
//
// AST nodes keep names/literal spellings as strings so later stages do not need
// to repeatedly slice SourceFile text.
std::string Parser::tokenText(const Token& token) const {
    return std::string(source_.slice(token.span));
}

} // namespace nexc
