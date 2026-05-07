#include "nexc/frontend/diagnostic.h"

#include <string_view>

namespace nexc {

namespace {

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
        const LineColumn location = source.lineColumn(diagnostic.span.start);
        out << source.path() << ':' << location.line << ':' << location.column
            << ": " << severityName(diagnostic.severity) << ": "
            << diagnostic.message << '\n';
    }
}

} // namespace nexc
