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

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

#include "nexc_link_config.h"

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
    std::vector<std::string> inputPaths;
    std::string outputPath;
};

void printUsage(std::ostream& out) {
    out << "usage:\n"
        << "  nexc (--dump-tokens | --dump-ast | --dump-ast-dot | --dump-ir | --dump-mlir | --dump-llvm | --check) <file.nexs>\n"
        << "  nexc <file.nexs>... -o <output>\n";
}

std::string readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

// Merge several nex sources into one buffer for a single parse and analysis pass.
//
// Spans refer to the concatenated text (lines still map correctly for caret
// diagnostics). The synthetic label lists inputs joined with \"+\" so messages
// show which files were combined.
std::pair<std::string, std::string> mergeSourceFiles(const std::vector<std::string>& paths) {
    std::string merged;
    std::string label;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) {
            merged += '\n';
            label += '+';
        }
        merged += readFile(paths[i]);
        label += paths[i];
    }
    return {std::move(label), std::move(merged)};
}

Options parseArgs(int argc, char** argv) {
    // Keep inspection modes deliberately simple: one mode flag plus one input.
    // Native compile concatenates one or more inputs then parses once:
    // `nexc a.nexs b.nexs -o out`.
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
        options.inputPaths = {argv[2]};
        return options;
    }

    // Native compile: nexc <file>... -o <output>
    if (argc >= 4 && std::string(argv[argc - 2]) == "-o") {
        Options options;
        options.mode = Mode::CompileExecutable;
        options.outputPath = argv[argc - 1];
        for (int i = 1; i < argc - 2; ++i) {
            options.inputPaths.push_back(argv[i]);
        }
        if (options.inputPaths.empty()) {
            throw std::invalid_argument("compile mode requires at least one input file");
        }
        return options;
    }

    throw std::invalid_argument("invalid command line");
}

#if NEXC_CONFIGURED_NATIVE_LINK

std::filesystem::path writeTemporaryLlvmIr(const nexc::ir::Module& module) {
    // Native compilation writes LLVM IR to a temp file, then runs `llc` to an
    // object file. The final link is OS-specific (see compileExecutable). A real
    // temp file keeps subprocess error messages actionable.
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
#ifdef __APPLE__
    // macOS has no /proc/self/exe. dyld exposes the main executable path; it may be
    // relative if the process was started via a relative argv[0]. Canonicalize so
    // sibling resources (libnexrt.a next to nexc) resolve regardless of cwd.
    std::uint32_t bufSize = PATH_MAX;
    std::vector<char> buffer(bufSize, '\0');
    if (_NSGetExecutablePath(buffer.data(), &bufSize) != 0) {
        buffer.resize(static_cast<std::size_t>(bufSize) + 1, '\0');
        bufSize = static_cast<std::uint32_t>(buffer.size());
        if (_NSGetExecutablePath(buffer.data(), &bufSize) != 0) {
            throw std::runtime_error("failed to locate nexc executable (_NSGetExecutablePath)");
        }
    }
    const std::filesystem::path raw(buffer.data());
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(raw, ec);
    return ec ? raw : canonical;
#else
    // Linux: /proc/self/exe is a symlink to the inode of the running binary.
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

        buffer.resize(buffer.size() * 2, '\0');
    }
#endif
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

// Run a subprocess given a NULL-terminated argv. argv[0] must be the executable
// path as execvp expects (absolute paths are fine).
int runChildProcess(char* const argv[]) {
    const pid_t child = ::fork();
    if (child == -1) {
        throw std::runtime_error(std::string("failed to fork subprocess: ") +
                                 std::strerror(errno));
    }

    if (child == 0) {
        ::execvp(argv[0], argv);
        ::_exit(127);
    }

    int status = 0;
    if (::waitpid(child, &status, 0) == -1) {
        throw std::runtime_error(std::string("failed to wait for subprocess: ") +
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

std::filesystem::path writeTemporaryObjectFilePath() {
    std::filesystem::path pattern =
        std::filesystem::temp_directory_path() / "nexc-obj-XXXXXX.o";
    std::string path = pattern.string();
    std::vector<char> writablePath(path.begin(), path.end());
    writablePath.push_back('\0');

    const int fd = ::mkstemps(writablePath.data(), 2);
    if (fd == -1) {
        throw std::runtime_error(std::string("failed to create temporary object path: ") +
                                 std::strerror(errno));
    }
    ::close(fd);
    std::error_code ignored;
    std::filesystem::remove(writablePath.data(), ignored);

    return writablePath.data();
}

#endif // NEXC_CONFIGURED_NATIVE_LINK

void compileExecutable(const nexc::ir::Module& module,
                       const std::string& outputPath) {
#if !NEXC_CONFIGURED_NATIVE_LINK
    (void)module;
    (void)outputPath;
    throw std::runtime_error(
        "native compilation was disabled at CMake configure time: on Linux install "
        "llc and ld.lld; on macOS install LLVM (llc) and use a clang-based toolchain "
        "for the C compiler, then re-run cmake");
#else
    const std::filesystem::path llvmIrPath = writeTemporaryLlvmIr(module);
    const std::filesystem::path objectPath = writeTemporaryObjectFilePath();
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
    } cleanupLl{llvmIrPath};
    TemporaryCleanup cleanupObj{objectPath};

    // 1) LLVM IR -> relocatable object with llc (machine code for this host).
    std::string llvmIrString = llvmIrPath.string();
    std::string objectString = objectPath.string();
    std::vector<std::string> llcStorage = {
        std::string(NEXC_LLC_PATH),
        "-filetype=obj",
        "-relocation-model=pic",
        std::move(llvmIrString),
        "-o",
        std::move(objectString),
    };
    std::vector<char*> llcArgv;
    llcArgv.reserve(llcStorage.size() + 1);
    for (std::string& piece : llcStorage) {
        llcArgv.push_back(piece.data());
    }
    llcArgv.push_back(nullptr);

    int llcExit = runChildProcess(llcArgv.data());
    if (llcExit == 127) {
        throw std::runtime_error(std::string("failed to execute llc at ") + NEXC_LLC_PATH);
    }
    if (llcExit != 0) {
        throw std::runtime_error("llc failed while compiling LLVM IR to an object file");
    }

    // 2) Link the relocatable object with libnexrt.a into an executable.
    //
    // Linux uses ld.lld with explicit ELF PIE layout and glibc CRT objects (same
    // shape as a hosted C program). Darwin uses the host clang driver so Mach-O
    // link rules, SDK paths, and libSystem are applied consistently.
    std::string objPath = objectPath.string();
    std::string rtPath = runtimeLibraryPath.string();
    std::string outPath = outputPath;
    std::vector<std::string> linkStorage;

#if NEXC_HOST_LINK_USE_LINUX_LD_LLD
    linkStorage = {
        std::string(NEXC_LD_LLD_PATH),
        "-pie",
        "-dynamic-linker",
        std::string(NEXC_LINUX_DYNAMIC_LINKER),
        std::string(NEXC_SCRT1_PATH),
        std::string(NEXC_CRTI_PATH),
        std::move(objPath),
        std::move(rtPath),
        "-L",
        std::string(NEXC_LIBDIR_FOR_LC),
        "-lc",
        std::string(NEXC_CRTN_PATH),
        "-o",
        std::move(outPath),
    };
#elif NEXC_HOST_LINK_USE_DARWIN_CLANG
    linkStorage = {
        std::string(NEXC_DARWIN_CLANG_PATH),
        std::move(objPath),
        std::move(rtPath),
        "-o",
        std::move(outPath),
    };
#else
#error "native link enabled but neither Linux nor Darwin link backend is set"
#endif

    std::vector<char*> linkArgv;
    linkArgv.reserve(linkStorage.size() + 1);
    for (std::string& piece : linkStorage) {
        linkArgv.push_back(piece.data());
    }
    linkArgv.push_back(nullptr);

    const int linkExit = runChildProcess(linkArgv.data());
#if NEXC_HOST_LINK_USE_LINUX_LD_LLD
    const char* const linkToolPath = NEXC_LD_LLD_PATH;
    const char* const linkFailWhat = "ld.lld";
#elif NEXC_HOST_LINK_USE_DARWIN_CLANG
    const char* const linkToolPath = NEXC_DARWIN_CLANG_PATH;
    const char* const linkFailWhat = "clang (Darwin link driver)";
#endif
    if (linkExit == 127) {
        throw std::runtime_error(std::string("failed to execute ") + linkFailWhat + " at " +
                                 linkToolPath);
    }
    if (linkExit != 0) {
        throw std::runtime_error(std::string(linkFailWhat) + " failed while linking executable");
    }
#endif // !NEXC_CONFIGURED_NATIVE_LINK
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        const auto [mergedLabel, mergedText] = mergeSourceFiles(options.inputPaths);
        nexc::SourceFile source(mergedLabel, mergedText);
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
