#include "nexc/frontend/lexer.h"

#include <cctype>

namespace nexc {

namespace {

// Core v0 syntax is ASCII-only outside comments and string literals. These
// helpers intentionally avoid locale-sensitive <cctype> classification so the
// lexer behaves the same on every developer machine.
// Return true for ASCII letters only.
//
// We do not use std::isalpha here because that can depend on locale. A compiler
// should tokenize the same source file the same way on every machine.
bool isAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Return true for the first character of an identifier.
//
// Core v0 keeps identifiers deliberately simple: ASCII letters and underscore.
bool isIdentifierStart(char c) {
    return isAsciiAlpha(c) || c == '_';
}

// Return true for every character after the first identifier character.
//
// Digits are allowed after the first character, so `x1` is one identifier but
// `1x` starts as an integer literal followed by an identifier.
bool isIdentifierContinue(char c) {
    return isIdentifierStart(c) || (c >= '0' && c <= '9');
}

// Return true for a digit accepted inside a hexadecimal integer literal.
bool isHexDigit(char c) {
    // Hex literal validation accepts both lowercase and uppercase digits after
    // the 0x/0X prefix.
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

// Detect bytes outside ASCII so diagnostics can explain why the lexer rejected a
// character in Core v0 syntax.
bool isNonAscii(char c) {
    return static_cast<unsigned char>(c) >= 0x80;
}

} // namespace

// Create a lexer for one source file.
//
// The lexer borrows the SourceFile so token spans can refer back to byte ranges
// in the original text. Diagnostics are also borrowed so lexing errors join the
// same DiagnosticBag used by later compiler stages.
Lexer::Lexer(const SourceFile& source, DiagnosticBag& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

// Convert the entire source file into a token stream.
//
// Tokenization is a single left-to-right pass. The result always ends with an
// explicit EndOfFile token so the parser has a stable sentinel to stop on.
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        // The lexer repeatedly skips trivia, then consumes exactly one real
        // token. "Trivia" is compiler terminology for whitespace/comments that
        // matter for source positions but not for grammar.
        skipWhitespaceAndComments();

        if (isAtEnd()) {
            // An explicit EOF token gives the parser a stable sentinel. That is
            // easier than making every parser routine ask whether the token
            // vector itself has run out.
            tokens.push_back(Token{
                .kind = TokenKind::EndOfFile,
                .span = SourceSpan{.start = current_, .end = current_},
            });
            break;
        }

        tokens.push_back(lexToken());
    }

    return tokens;
}

// Return true once current_ has reached or passed the end of the source buffer.
bool Lexer::isAtEnd() const {
    return current_ >= source_.size();
}

// Look ahead without consuming characters.
//
// Returning '\0' past the end is a common lexer trick: callers can inspect
// peek(1) for two-character operators without needing separate bounds checks.
char Lexer::peek(std::size_t offset) const {
    const std::size_t index = current_ + offset;
    if (index >= source_.size()) {
        return '\0';
    }
    return source_.text()[index];
}

// Consume and return the current character.
//
// Callers are responsible for checking isAtEnd() first. Keeping advance() small
// makes the main lexing loops easy to read.
char Lexer::advance() {
    return source_.text()[current_++];
}

// Consume the next character only if it matches the expected byte.
//
// This is used for two-character tokens such as `==`, `!=`, `<=`, `>=`, `&&`,
// `||`, and `->`.
bool Lexer::match(char expected) {
    if (isAtEnd() || peek() != expected) {
        return false;
    }

    ++current_;
    return true;
}

// Skip trivia before the next real token.
//
// Trivia is source text that affects spans/locations but does not appear in the
// parser's grammar: whitespace, line comments, and block comments.
void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        const char c = peek();

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
            continue;
        }

        if (c == '/' && peek(1) == '/') {
            // Line comments are skipped through, but not including, the
            // newline. The next loop iteration consumes the newline as
            // whitespace.
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }

        if (c == '/' && peek(1) == '*') {
            const std::size_t start = current_;
            advance();
            advance();

            // Core v0 block comments do not nest. The first `*/` closes the
            // comment, even if another `/*` appears inside it.
            while (!isAtEnd() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }

            if (isAtEnd()) {
                diagnostics_.error(SourceSpan{.start = start, .end = current_},
                                   "unterminated block comment");
                return;
            }

            advance();
            advance();
            continue;
        }

        return;
    }
}

// Lex one non-trivia token at the current position.
//
// The caller has already skipped whitespace/comments. This function consumes the
// bytes that make up exactly one token, reports unsupported characters, and
// returns a Token carrying both kind and source span.
Token Lexer::lexToken() {
    const std::size_t start = current_;
    const char c = advance();

    // Identifier scanning uses the "maximal munch" rule: consume the longest
    // possible identifier first, then decide whether that spelling is reserved.
    // This is why `letx` becomes one identifier instead of `let` plus `x`.
    if (isIdentifierStart(c)) {
        return lexIdentifierOrKeyword();
    }

    if (c >= '0' && c <= '9') {
        return lexIntegerLiteral();
    }

    switch (c) {
    case '"':
        return lexStringLiteral(start);
    case '+':
        return single(TokenKind::Plus, start);
    case '-':
        // Operators with shared prefixes are handled by looking one character
        // ahead. `->` must become Arrow, while bare `-` remains Minus.
        return match('>') ? single(TokenKind::Arrow, start)
                          : single(TokenKind::Minus, start);
    case '*':
        return single(TokenKind::Star, start);
    case '/':
        return single(TokenKind::Slash, start);
    case '%':
        return single(TokenKind::Percent, start);
    case '!':
        return match('=') ? single(TokenKind::BangEqual, start)
                          : single(TokenKind::Bang, start);
    case '=':
        return match('=') ? single(TokenKind::EqualEqual, start)
                          : single(TokenKind::Equal, start);
    case '<':
        return match('=') ? single(TokenKind::LessEqual, start)
                          : single(TokenKind::Less, start);
    case '>':
        return match('=') ? single(TokenKind::GreaterEqual, start)
                          : single(TokenKind::Greater, start);
    case '&':
        if (match('&')) {
            return single(TokenKind::AmpAmp, start);
        }
        break;
    case '|':
        if (match('|')) {
            return single(TokenKind::PipePipe, start);
        }
        break;
    case '(':
        return single(TokenKind::LeftParen, start);
    case ')':
        return single(TokenKind::RightParen, start);
    case '{':
        return single(TokenKind::LeftBrace, start);
    case '}':
        return single(TokenKind::RightBrace, start);
    case ',':
        return single(TokenKind::Comma, start);
    case ';':
        return single(TokenKind::Semicolon, start);
    case ':':
        return single(TokenKind::Colon, start);
    default:
        break;
    }

    diagnostics_.error(SourceSpan{.start = start, .end = current_},
                       isNonAscii(c) ? "unsupported non-ASCII character in Core v0 syntax"
                                     : "unsupported character");

    // Return a placeholder token so tokenization can continue far enough to
    // report the diagnostic through the usual pipeline. Later error recovery can
    // use a dedicated BadToken kind if richer behavior is needed.
    return Token{
        .kind = TokenKind::EndOfFile,
        .span = SourceSpan{.start = start, .end = current_},
    };
}

// Lex an identifier-like spelling and classify it as a keyword if reserved.
//
// The lexer does not need a separate path for `fn`, `return`, or user names:
// maximal-munch scanning collects the spelling, then keywordKind decides whether
// that spelling is reserved in Core v0.
Token Lexer::lexIdentifierOrKeyword() {
    const std::size_t start = current_ - 1;

    while (!isAtEnd() && isIdentifierContinue(peek())) {
        advance();
    }

    const SourceSpan span{.start = start, .end = current_};
    return Token{
        .kind = keywordKind(source_.slice(span)),
        .span = span,
    };
}

// Lex an integer literal spelling.
//
// This validates decimal vs hexadecimal surface syntax but does not choose a
// numeric type or check whether the value fits. Those questions require context
// from semantic analysis.
Token Lexer::lexIntegerLiteral() {
    const std::size_t start = current_ - 1;

    if (source_.text()[start] == '0' && (peek() == 'x' || peek() == 'X')) {
        advance();
        const std::size_t firstHexDigit = current_;

        // The lexer records the spelling and validates the base-specific
        // surface form. It intentionally does not decide whether the literal is
        // i32, u64, etc.; that requires type context from semantic analysis.
        while (!isAtEnd() && isHexDigit(peek())) {
            advance();
        }

        if (current_ == firstHexDigit) {
            diagnostics_.error(SourceSpan{.start = start, .end = current_},
                               "expected at least one hexadecimal digit after '0x'");
        }

        return Token{
            .kind = TokenKind::IntegerLiteral,
            .span = SourceSpan{.start = start, .end = current_},
        };
    }

    while (!isAtEnd() && peek() >= '0' && peek() <= '9') {
        advance();
    }

    return Token{
        .kind = TokenKind::IntegerLiteral,
        .span = SourceSpan{.start = start, .end = current_},
    };
}

// Lex a double-quoted string literal.
//
// The token keeps the raw source spelling, including quotes and escape
// backslashes. Later stages can decide whether and how to decode that spelling
// into runtime string data.
Token Lexer::lexStringLiteral(std::size_t start) {
    // String support is intentionally small: preserve the raw spelling and
    // validate only the escape sequences Core v0 recognizes. Actual runtime
    // string representation is a later backend/runtime concern.
    while (!isAtEnd()) {
        const char c = advance();

        if (c == '"') {
            return Token{
                .kind = TokenKind::StringLiteral,
                .span = SourceSpan{.start = start, .end = current_},
            };
        }

        if (c == '\n' || c == '\r') {
            diagnostics_.error(SourceSpan{.start = start, .end = current_},
                               "unterminated string literal");
            return Token{
                .kind = TokenKind::StringLiteral,
                .span = SourceSpan{.start = start, .end = current_},
            };
        }

        if (c == '\\') {
            // Consume the escaped byte so a quote in `\"` does not terminate the
            // string. Unsupported escapes are diagnosed but lexing continues.
            if (isAtEnd()) {
                break;
            }

            const char escaped = advance();
            if (escaped != '"' && escaped != '\\' && escaped != 'n' &&
                escaped != 't' && escaped != 'r') {
                diagnostics_.error(
                    SourceSpan{.start = current_ - 2, .end = current_},
                    "unsupported string escape sequence");
            }
        }
    }

    diagnostics_.error(SourceSpan{.start = start, .end = current_},
                       "unterminated string literal");
    return Token{
        .kind = TokenKind::StringLiteral,
        .span = SourceSpan{.start = start, .end = current_},
    };
}

// Build a token for punctuation/operator forms whose span is already known.
//
// By the time this helper is called, lexToken() has consumed the token's bytes,
// so current_ points one past the end.
Token Lexer::single(TokenKind kind, std::size_t start) {
    return Token{
        .kind = kind,
        .span = SourceSpan{.start = start, .end = current_},
    };
}

} // namespace nexc
