#pragma once

#include "nexc/frontend/ast.h"
#include "nexc/frontend/diagnostic.h"

namespace nexc {

// SemanticAnalyzer is the first "meaning" pass.
//
// The parser answers: "does this source have valid nex syntax?"
// The semantic analyzer answers: "do those syntax nodes mean a valid program?"
// It checks names, scopes, types, mutability, calls, returns, and the optional
// executable entry point shape.
class SemanticAnalyzer {
public:
    SemanticAnalyzer(const SourceFile& source, DiagnosticBag& diagnostics);

    // Analyze a parsed translation unit and append semantic diagnostics.
    //
    // This pass does not rewrite the AST or produce IR. It is currently a
    // validation pass: if diagnostics has no errors afterward, later stages can
    // assume names, types, mutability, calls, returns, and constants obey Core
    // v0 rules.
    void analyze(const TranslationUnit& unit);

private:
    DiagnosticBag& diagnostics_;
};

} // namespace nexc
