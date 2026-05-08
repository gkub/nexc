#include "nexc/frontend/ast.h"
#include "nexc/frontend/diagnostic.h"
#include "nexc/frontend/lexer.h"
#include "nexc/frontend/parser.h"
#include "nexc/frontend/semantic.h"
#include "nexc/frontend/source.h"
#include "nexc/frontend/token.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

enum class Mode {
    DumpTokens,
    DumpAst,
    DumpAstDot,
    Check,
};

void printUsage(std::ostream& out) {
    out << "usage: nexc (--dump-tokens | --dump-ast | --dump-ast-dot | --check) <file.nexs>\n";
}

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        printUsage(std::cerr);
        return 2;
    }

    Mode mode;
    const std::string modeArg = argv[1];
    if (modeArg == "--dump-tokens") {
        mode = Mode::DumpTokens;
    } else if (modeArg == "--dump-ast") {
        mode = Mode::DumpAst;
    } else if (modeArg == "--dump-ast-dot") {
        mode = Mode::DumpAstDot;
    } else if (modeArg == "--check") {
        mode = Mode::Check;
    } else {
        printUsage(std::cerr);
        return 2;
    }

    const std::string path = argv[2];

    try {
        nexc::SourceFile source(path, readFile(path));
        nexc::DiagnosticBag diagnostics;

        // The CLI always lexes first because both frontend inspection modes need
        // tokens. `--dump-tokens` stops there; `--dump-ast` feeds the same token
        // stream into the parser.
        nexc::Lexer lexer(source, diagnostics);
        std::vector<nexc::Token> tokens = lexer.tokenize();

        if (mode == Mode::DumpTokens) {
            for (const nexc::Token& token : tokens) {
                nexc::dumpToken(std::cout, source, token);
            }
        } else {
            nexc::Parser parser(source, tokens, diagnostics);
            nexc::TranslationUnit unit = parser.parseTranslationUnit();
            if (mode == Mode::DumpAst) {
                nexc::dumpAst(std::cout, unit);
            } else if (mode == Mode::DumpAstDot) {
                nexc::dumpAstDot(std::cout, unit);
            } else {
                // Semantic analysis assumes the parser produced a trustworthy
                // syntax tree. If syntax already failed, avoid piling meaning
                // errors on top of the more fundamental parse errors.
                if (!diagnostics.hasErrors()) {
                    nexc::SemanticAnalyzer analyzer(source, diagnostics);
                    analyzer.analyze(unit);
                }
            }
        }

        nexc::printDiagnostics(std::cerr, source, diagnostics);
        return diagnostics.hasErrors() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "nexc: " << error.what() << '\n';
        return 1;
    }
}
