#include "nexc/frontend/ast.h"
#include "nexc/frontend/diagnostic.h"
#include "nexc/frontend/lexer.h"
#include "nexc/frontend/parser.h"
#include "nexc/frontend/semantic.h"
#include "nexc/frontend/source.h"
#include "nexc/frontend/token.h"
#include "nexc/ir/builder.h"
#include "nexc/ir/dump.h"
#include "nexc/llvm/textual.h"
#include "nexc/mlir/textual.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {

enum class Mode {
    DumpTokens,
    DumpAst,
    DumpAstDot,
    DumpIr,
    DumpMlir,
    DumpLlvm,
    CompileExecutable,
    Check,
};

struct Options {
    Mode mode = Mode::Check;
    std::string inputPath;
    std::string outputPath;
};

void printUsage(std::ostream& out) {
    out << "usage:\n"
        << "  nexc (--dump-tokens | --dump-ast | --dump-ast-dot | --dump-ir | --dump-mlir | --dump-llvm | --check) <file.nexs>\n"
        << "  nexc <file.nexs> -o <output>\n";
}

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

Options parseArgs(int argc, char** argv) {
    // Keep inspection modes deliberately simple: one mode flag plus one input.
    // The native compile form is also intentionally small for v0:
    // `nexc input.nexs -o output`.
    if (argc == 3) {
        Options options;
        const std::string modeArg = argv[1];
        if (modeArg == "--dump-tokens") {
            options.mode = Mode::DumpTokens;
        } else if (modeArg == "--dump-ast") {
            options.mode = Mode::DumpAst;
        } else if (modeArg == "--dump-ast-dot") {
            options.mode = Mode::DumpAstDot;
        } else if (modeArg == "--dump-ir") {
            options.mode = Mode::DumpIr;
        } else if (modeArg == "--dump-mlir") {
            options.mode = Mode::DumpMlir;
        } else if (modeArg == "--dump-llvm") {
            options.mode = Mode::DumpLlvm;
        } else if (modeArg == "--check") {
            options.mode = Mode::Check;
        } else {
            throw std::invalid_argument("unknown compiler mode: " + modeArg);
        }
        options.inputPath = argv[2];
        return options;
    }

    if (argc == 4 && std::string(argv[2]) == "-o") {
        Options options;
        options.mode = Mode::CompileExecutable;
        options.inputPath = argv[1];
        options.outputPath = argv[3];
        return options;
    }

    throw std::invalid_argument("invalid command line");
}

std::filesystem::path writeTemporaryLlvmIr(const nexc::ir::Module& module) {
    // Native v0 compilation uses LLVM IR as a handoff file to clang. We create a
    // real temporary file instead of piping so error messages can include a path
    // and so the implementation stays easy to inspect while the driver is young.
    std::filesystem::path pattern =
        std::filesystem::temp_directory_path() / "nexc-llvm-XXXXXX.ll";
    std::string path = pattern.string();
    std::vector<char> writablePath(path.begin(), path.end());
    writablePath.push_back('\0');

    const int fd = ::mkstemps(writablePath.data(), 3);
    if (fd == -1) {
        throw std::runtime_error(std::string("failed to create temporary LLVM IR file: ") +
                                 std::strerror(errno));
    }
    ::close(fd);

    std::ofstream out(writablePath.data(), std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open temporary LLVM IR file for writing");
    }
    nexc::llvm::dumpTextualLlvmIr(out, module);
    if (!out) {
        throw std::runtime_error("failed to write temporary LLVM IR file");
    }

    return writablePath.data();
}

std::filesystem::path currentExecutablePath() {
    // Linux exposes the running program through /proc/self/exe. Reading this
    // symlink gives us the real location of `nexc`, even when the user invokes it
    // from some unrelated working directory. That is the key to finding bundled
    // compiler resources without baking source-tree paths into the binary.
    std::vector<char> buffer(4096, '\0');
    while (true) {
        const ssize_t size = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (size == -1) {
            throw std::runtime_error(std::string("failed to locate nexc executable: ") +
                                     std::strerror(errno));
        }

        if (static_cast<std::size_t>(size) < buffer.size()) {
            return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(size)));
        }

        // If the buffer was exactly full, the path may have been truncated. Grow
        // and retry rather than guessing.
        buffer.resize(buffer.size() * 2, '\0');
    }
}

std::filesystem::path resolveRuntimeLibrary() {
    // Developers and tests can override the runtime location explicitly. This is
    // useful for packaging experiments, staged installs, or unusual build
    // layouts, while the normal build continues to work without configuration.
    if (const char* overridePath = std::getenv("NEXC_RUNTIME_LIBRARY")) {
        if (*overridePath != '\0') {
            return overridePath;
        }
    }

#ifdef NEXC_RUNTIME_LIBRARY_RELATIVE_PATH
    const std::filesystem::path bundled =
        currentExecutablePath().parent_path() / NEXC_RUNTIME_LIBRARY_RELATIVE_PATH;
    if (std::filesystem::exists(bundled)) {
        return bundled;
    }
#endif

    throw std::runtime_error(
        "failed to find nex runtime library; set NEXC_RUNTIME_LIBRARY to libnexrt.a");
}

int runClang(const std::string& clangName,
             const std::filesystem::path& llvmIrPath,
             const std::filesystem::path& runtimeLibraryPath,
             const std::string& outputPath) {
    // The compiler driver delegates final code generation and linking to clang.
    // That is normal for an early compiler: clang already knows the platform C
    // runtime startup files, linker flags, target defaults, and object format.
    const pid_t child = ::fork();
    if (child == -1) {
        throw std::runtime_error(std::string("failed to fork clang: ") +
                                 std::strerror(errno));
    }

    if (child == 0) {
        std::string llvmIr = llvmIrPath.string();
        std::string runtimeLibrary = runtimeLibraryPath.string();
        char* const args[] = {
            const_cast<char*>(clangName.c_str()),
            const_cast<char*>("-Wno-override-module"),
            const_cast<char*>("-x"),
            const_cast<char*>("ir"),
            llvmIr.data(),
            const_cast<char*>("-x"),
            const_cast<char*>("none"),
            runtimeLibrary.data(),
            const_cast<char*>("-o"),
            const_cast<char*>(outputPath.c_str()),
            nullptr,
        };
        ::execvp(clangName.c_str(), args);
        ::_exit(127);
    }

    int status = 0;
    if (::waitpid(child, &status, 0) == -1) {
        throw std::runtime_error(std::string("failed to wait for clang: ") +
                                 std::strerror(errno));
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

void compileExecutable(const nexc::ir::Module& module,
                       const std::string& outputPath) {
    const std::filesystem::path llvmIrPath = writeTemporaryLlvmIr(module);
    const std::filesystem::path runtimeLibraryPath = resolveRuntimeLibrary();
    if (!std::filesystem::exists(runtimeLibraryPath)) {
        throw std::runtime_error("nex runtime library does not exist: " +
                                 runtimeLibraryPath.string());
    }

    struct TemporaryCleanup {
        std::filesystem::path path;
        ~TemporaryCleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{llvmIrPath};

    // Prefer `clang`, but try the version-suffixed binary used by Ubuntu's LLVM
    // packages too. A 127 exit from our child means exec failed, not that clang
    // rejected the input.
    int exitCode = runClang("clang", llvmIrPath, runtimeLibraryPath, outputPath);
    if (exitCode == 127) {
        exitCode = runClang("clang-18", llvmIrPath, runtimeLibraryPath, outputPath);
    }
    if (exitCode != 0) {
        throw std::runtime_error("clang failed while creating executable");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        nexc::SourceFile source(options.inputPath, readFile(options.inputPath));
        nexc::DiagnosticBag diagnostics;

        // The CLI always lexes first because both frontend inspection modes need
        // tokens. `--dump-tokens` stops there; `--dump-ast` feeds the same token
        // stream into the parser.
        nexc::Lexer lexer(source, diagnostics);
        std::vector<nexc::Token> tokens = lexer.tokenize();

        if (options.mode == Mode::DumpTokens) {
            for (const nexc::Token& token : tokens) {
                nexc::dumpToken(std::cout, source, token);
            }
        } else {
            nexc::Parser parser(source, tokens, diagnostics);
            nexc::TranslationUnit unit = parser.parseTranslationUnit();
            if (options.mode == Mode::DumpAst) {
                nexc::dumpAst(std::cout, unit);
            } else if (options.mode == Mode::DumpAstDot) {
                nexc::dumpAstDot(std::cout, unit);
            } else {
                // Semantic analysis assumes the parser produced a trustworthy
                // syntax tree. If syntax already failed, avoid piling meaning
                // errors on top of the more fundamental parse errors.
                if (!diagnostics.hasErrors()) {
                    nexc::SemanticAnalyzer analyzer(source, diagnostics);
                    analyzer.analyze(unit);
                }
                if ((options.mode == Mode::DumpIr ||
                     options.mode == Mode::DumpMlir ||
                     options.mode == Mode::DumpLlvm ||
                     options.mode == Mode::CompileExecutable) &&
                    !diagnostics.hasErrors()) {
                    const nexc::ir::Module module = nexc::ir::buildTypedIr(unit);
                    if (options.mode == Mode::DumpIr) {
                        nexc::ir::dumpModule(std::cout, module);
                    } else if (options.mode == Mode::DumpMlir) {
                        nexc::mlir::dumpTextualMlir(std::cout, module);
                    } else if (options.mode == Mode::DumpLlvm) {
                        nexc::llvm::dumpTextualLlvmIr(std::cout, module);
                    } else {
                        compileExecutable(module, options.outputPath);
                    }
                }
            }
        }

        nexc::printDiagnostics(std::cerr, source, diagnostics);
        return diagnostics.hasErrors() ? 1 : 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "nexc: " << error.what() << '\n';
        printUsage(std::cerr);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "nexc: " << error.what() << '\n';
        return 1;
    }
}
