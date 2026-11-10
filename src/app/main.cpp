// Pullform: a free, open-source 3D modeler. Entry point.
#include "app/app.hpp"

#include <cstdio>
#include <exception>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

/// The project path given on the command line (for example when a project is
/// double-clicked), if any.
std::optional<std::filesystem::path> commandLinePath([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
#ifdef _WIN32
    // argv holds the path in the ANSI code page, which cannot represent most
    // non-English names. Read the UTF-16 command line instead.
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return std::nullopt;
    std::optional<std::filesystem::path> path;
    if (count > 1) path = std::filesystem::path(args[1]);
    LocalFree(args);
    return path;
#else
    if (argc > 1) return std::filesystem::path(argv[1]);
    return std::nullopt;
#endif
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto open = commandLinePath(argc, argv);
        pf::App app;
        return app.run(open);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Pullform stopped because of an unexpected error: %s\n", e.what());
        return 1;
    }
}
