// Pullform: a free, open-source 3D modeler. Entry point.
#include "app/app.hpp"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        std::optional<std::filesystem::path> open;
        if (argc > 1) open = std::filesystem::path(argv[1]);
        pf::App app;
        return app.run(open);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Pullform stopped because of an unexpected error: %s\n", e.what());
        return 1;
    }
}
