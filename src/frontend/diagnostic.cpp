#include "nexc/frontend/diagnostic.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace nexc {

namespace {

// Diagnostics use lowercase severity names because the output is meant to feel
// like familiar compiler errors: `file:line:column: error: ...`.
std::string_view severityName(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Note:
        return "note";
    }

    return "diagnostic";
}

std::size_t lineStart(std::string_view text, std::size_t offset) {
    const std::size_t clamped = std::min(offset, text.size());

    // Find the newline before the diagnostic position. If there is none, the
    // diagnostic is on the first source line.
    const std::size_t newline = text.rfind('\n', clamped);
    return newline == std::string_view::npos ? 0 : newline + 1;
}

std::size_t lineEnd(std::string_view text, std::size_t offset) {
    const std::size_t clamped = std::min(offset, text.size());

    // Find the newline after the diagnostic position. If there is none, the
    // current line extends to EOF.
    const std::size_t newline = text.find('\n', clamped);
    return newline == std::string_view::npos ? text.size() : newline;
}

void printSourceLine(std::ostream& out, const SourceFile& source,
                     SourceSpan span) {
    const std::string_view text = source.text();
    const std::size_t start = std::min(span.start, text.size());
    const std::size_t lineStartOffset = lineStart(text, start);
    const std::size_t lineEndOffset = lineEnd(text, start);
    const std::string_view line =
        text.substr(lineStartOffset, lineEndOffset - lineStartOffset);

    const std::size_t caretColumn = start - lineStartOffset;

    // Multi-line diagnostics still underline only the first line for now. That
    // keeps rendering simple while preserving the most important debugging
    // information: where the compiler first noticed the problem.
    const std::size_t rawEnd = std::min(span.end, lineEndOffset);
    const std::size_t underlineWidth =
        std::max<std::size_t>(1, rawEnd > start ? rawEnd - start : 1);

    out << "  | " << line << '\n';
    out << "  | ";
    for (std::size_t i = 0; i < caretColumn; ++i) {
        // Preserve tabs in the leading whitespace so the caret stays aligned
        // with source that uses tabs. Spaces are emitted for all other bytes.
        out << (line[i] == '\t' ? '\t' : ' ');
    }

    out << '^';
    for (std::size_t i = 1; i < underlineWidth; ++i) {
        out << '~';
    }
    out << '\n';
}

} // namespace

void DiagnosticBag::error(SourceSpan span, std::string message) {
    add(DiagnosticSeverity::Error, span, std::move(message));
}

void DiagnosticBag::warning(SourceSpan span, std::string message) {
    add(DiagnosticSeverity::Warning, span, std::move(message));
}

void DiagnosticBag::note(SourceSpan span, std::string message) {
    add(DiagnosticSeverity::Note, span, std::move(message));
}

void DiagnosticBag::add(DiagnosticSeverity severity, SourceSpan span,
                        std::string message) {
    if (severity == DiagnosticSeverity::Error) {
        // `hasErrors_` is cached so the driver can cheaply decide whether later
        // stages such as semantic analysis or IR construction should run.
        hasErrors_ = true;
    }

    diagnostics_.push_back(Diagnostic{
        .severity = severity,
        .message = std::move(message),
        .span = span,
    });
}

void printDiagnostics(std::ostream& out, const SourceFile& source,
                      const DiagnosticBag& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        // Convert byte offsets to line/column only at the output boundary. The
        // rest of the compiler can stay byte-offset based.
        const LineColumn location = source.lineColumn(diagnostic.span.start);
        out << source.path() << ':' << location.line << ':' << location.column
            << ": " << severityName(diagnostic.severity) << ": "
            << diagnostic.message << '\n';
        printSourceLine(out, source, diagnostic.span);
    }
}

} // namespace nexc
