# Changelog

All notable changes to Pullform are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0, minor versions may
include breaking changes, but project files will keep a migration path.

## [Unreleased]

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
