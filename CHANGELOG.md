# Changelog

All notable changes to Pullform are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0, minor versions may
include breaking changes, but project files will keep a migration path.

## [Unreleased]

### Fixed

- Saves are flushed to the disk (`fsync`, `F_FULLFSYNC` on macOS, `FlushFileBuffers` on
  Windows) before the rename, so a power cut right after saving cannot leave an empty or
  truncated project. Saving keeps the file's permissions (and its ACL on Windows) and no
  longer replaces a symbolic link with a regular file.
- Save As could replace an existing project without asking when the `.pullform.json`
  extension was added after the dialog closed. Pullform now checks the final name and asks
  first. Typing `house` no longer saves `house.json`; it saves `house.pullform.json`.
- Opening a project from the Windows command line (or by double-clicking it, once the file
  association exists) works with non-English file names.

## [0.2.0] - 2026-11-10

A native rewrite. The browser prototype is replaced by a C++20 desktop application with
the same milestone 1 workflow and the same project file format.

### Changed

- Rewritten in C++20 with GLFW, OpenGL 3.3 core, Dear ImGui, GLM (double precision),
  nlohmann/json and nativefiledialog-extended, built with CMake presets. No web
  technology, runtime downloads or network access.
- The model, geometry and history live in a headless core library (`pullform_core`) with
  doctest unit tests that run without a display.
- Save and Open use native file dialogs; saves are atomic (temporary file, then rename).
- Unsaved-changes prompts now also cover quitting and closing the window.
- Files saved by 0.1.0 (format version 1) open unchanged.

### Added

- Windows installer (NSIS) with Start menu and desktop shortcuts and an uninstaller;
  the executable has no runtime dependencies besides the graphics driver.
- Linux and macOS source builds; CI on Windows (MSVC), Linux (GCC, Clang, ASan+UBSan)
  and macOS, uploading the Windows installer on every pull request.
- High-DPI scaling from the monitor content scale, with a `PULLFORM_UI_SCALE` override.
- Original application icon.

### Removed

- The TypeScript/React/three.js web application and its browser tests.

## [0.1.0] - 2026-11-10

The first modeling milestone: draw, push/pull, edit, undo, save and reopen.

### Added

- Modeling viewport with a ground grid (100 mm minor, 1 m major) and X/Y/Z axes, in a
  Z-up coordinate system.
- Camera navigation: orbit (middle drag or the Orbit tool), pan (right drag or
  Shift+middle drag), zoom toward the cursor, and Fit to model (F).
- Rectangle tool (R): draw on the ground plane by click-click or drag in any direction, or
  type an exact width and depth. Optional grid snap (G) with a selectable increment.
- Push/Pull tool (P): extrude a rectangle into a closed rectangular solid, and move an
  extrusion's top cap, by pointer or an exact distance.
- Properties panel with selection details and an absolute Total height field.
- Select tool (Space) with object and face selection, hover highlights, and whole-object
  delete (Delete/Backspace).
- Undo and redo (Ctrl/⌘+Z, Ctrl+Y or Ctrl/⌘+Shift+Z) for every modeling operation, with
  Escape cancelling live previews.
- Length input in mm, cm, m, in and ft (unitless values use the display unit, millimeters
  by default) with clear messages for unsupported formats.
- Native project files (`.pullform.json`, format `pullform-project` version 1): New, Open
  and Save as a download, with full validation before opening and prompts before
  discarding unsaved work.
- Unit tests (Vitest) and browser tests (Playwright, Chromium) covering geometry, units,
  history, tools, file handling and the full interface workflow.
- README, architecture notes, roadmap, verification report, MIT license and third-party
  notices.

### Known limitations

- Only rectangles on the ground plane and their extrusions; heights must be positive.
- Push/pull works on a rectangle or a top cap; side and base faces are not supported yet.
- No move, rotate, scale or copy tools yet.
- Firefox and Safari have not been tested yet.

[Unreleased]: https://github.com/anyday901/pullform/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/anyday901/pullform/releases/tag/v0.1.0
