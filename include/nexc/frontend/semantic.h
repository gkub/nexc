#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/frontend/diagnostic.h"

namespace nexc {

// SemanticAnalyzer is the first "meaning" pass.
//
// The parser answers: "does this source have valid Core v0 syntax?"
// The semantic analyzer answers: "do those syntax nodes mean a valid program?"
// It checks names, scopes, types, mutability, calls, returns, and the optional
// executable entry point shape.
class SemanticAnalyzer {
public:
    SemanticAnalyzer(const SourceFile& source, DiagnosticBag& diagnostics);

    void analyze(const TranslationUnit& unit);

private:
    const SourceFile& source_;
    DiagnosticBag& diagnostics_;
};

} // namespace nexc
