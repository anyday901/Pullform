# Contributing to Pullform

Thanks for your interest in improving Pullform.

## Getting set up

1. Install the prerequisites for your platform listed in the [README](README.md):
   CMake 3.25+, Git, a C++20 compiler (Visual Studio 2022, GCC 11+, Clang 15+ or
   Apple Clang 15+) and Ninja on Linux/macOS.
2. Clone, configure, build and test:
   ```sh
   git clone https://github.com/anyday901/pullform.git
   cd pullform
   cmake --workflow --preset linux-release   # or windows-release / macos-release
   ```
3. For quick iteration on the model and tools without a window, use the `headless`
   preset: `cmake --preset headless && cmake --build build/headless && ctest --test-dir build/headless`.

## Guidelines

- Keep `src/core` free of windowing, GPU and UI code; it must build with the `headless`
  preset. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- Every model change goes through `Editor::commit` with a pure operation. Previews never
  enter the document or history.
- Add a doctest for new behavior in `tests/`. Tests must not need a display.
- The build uses warnings as errors in the presets; fix warnings rather than silencing
  them. Run the `linux-sanitize` preset for changes to memory-sensitive code.
- Project file changes need a new format version and a migration with tests.
- Keep the interface original; do not copy other products' assets, icons or text.

## Pull requests

Describe what changed and how you verified it (automated tests, manual steps, and what you
could not check). CI must pass on Windows, Linux and macOS.

By contributing you agree that your contributions are licensed under the MIT License.
