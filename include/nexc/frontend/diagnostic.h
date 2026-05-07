#pragma once

#include "nexc/frontend/source.h"

#include <ostream>
#include <string>
#include <vector>

namespace nexc {

enum class DiagnosticSeverity {
    Error,
    Warning,
    Note,
};

// A diagnostic is a compiler message tied to source text.
//
// The frontend reports errors through DiagnosticBag instead of printing
// immediately. Keeping diagnostics as data makes tests easier and lets later
// phases attach notes, caret rendering, or machine-readable output.
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    SourceSpan span;
};

// Simple accumulator for diagnostics produced during one compiler run.
//
// Early frontend code can keep parsing after some errors, so collecting messages
// is more useful than throwing exceptions for normal user mistakes.
class DiagnosticBag {
public:
    void error(SourceSpan span, std::string message);
    void warning(SourceSpan span, std::string message);
    void note(SourceSpan span, std::string message);

    bool hasErrors() const { return hasErrors_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    void add(DiagnosticSeverity severity, SourceSpan span, std::string message);

    bool hasErrors_ = false;
    std::vector<Diagnostic> diagnostics_;
};

void printDiagnostics(std::ostream& out, const SourceFile& source,
                      const DiagnosticBag& diagnostics);

} // namespace nexc
