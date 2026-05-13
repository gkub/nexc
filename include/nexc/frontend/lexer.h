#pragma once

#include "nexc/frontend/diagnostic.h"
#include "nexc/frontend/token.h"

#include <vector>

namespace nexc {

// The lexer is the first real compiler stage.
//
// Its job is deliberately narrow: consume raw source characters and produce a
// flat token stream. It does not understand grammar, types, scopes, or whether
// a program is meaningful. For example, it recognizes `return` as a keyword but
// does not know whether `return` appears inside a function.
class Lexer {
public:
    Lexer(const SourceFile& source, DiagnosticBag& diagnostics);

    std::vector<Token> tokenize();

private:
    // Cursor helpers. `current_` is an offset into the source buffer.
    bool isAtEnd() const;
    char peek(std::size_t offset = 0) const;
    char advance();

    // If the next character matches `expected`, consume it. This is useful for
    // two-character tokens such as `==`, `!=`, and `->`.
    bool match(char expected);

    // Whitespace and comments separate tokens but are not tokens themselves in
    // nex, so the parser never sees them.
    void skipWhitespaceAndComments();

    Token lexToken();
    Token lexIdentifierOrKeyword();
    Token lexIntegerLiteral();
    Token lexStringLiteral(std::size_t start);

    // Build a token whose spelling is already fully consumed.
    Token single(TokenKind kind, std::size_t start);

    const SourceFile& source_;
    DiagnosticBag& diagnostics_;
    std::size_t current_ = 0;
};

} // namespace nexc
