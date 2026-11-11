# Roadmap

The planned work now lives in [TODO.md](../TODO.md), as numbered, PR-sized items with
dependencies and acceptance checks. This file keeps the record of completed milestones.

## Completed: Milestone 1, the first modeling workflow

- [x] Empty scene with grid and axes; orbit, pan, zoom, fit to model.
- [x] Rectangle on the ground plane by mouse (any direction) or exact width/depth; grid snap.
- [x] Push/pull a rectangle into a closed prism by pointer or exact distance.
- [x] Select the cap and change total height; the same object is updated.
- [x] Select and delete whole objects.
- [x] Undo/redo through one commit path; previews never enter history; Escape cancels.
- [x] Versioned JSON save and open via native dialogs, atomic writes, full validation and
      unsaved-changes prompts (New, Open, quit, window close).
- [x] Units: meters internally, millimeters by default, mm/cm/m/in/ft input.
- [x] Native C++20 application (GLFW, OpenGL 3.3 core, Dear ImGui) with a headless core.
- [x] Windows NSIS installer; Linux and macOS source builds; CI on all three plus ASan/UBSan.
- [x] Headless doctest suite for geometry, units, history, tools, camera and files.

## Completed: Milestone 2, move and precision basics

- [x] Move tool with typed distance, vector and axis locking; tap Ctrl to place a copy.
- [x] Endpoint and midpoint snapping with X/Y/Z inference guides and height locks.
- [x] Duplicate with a new stable ID.
- [x] Tape Measure and dimension annotations attached to corners and edge midpoints.
- [x] Rotate and Scale with typed angles and factors; file format version 2 with a
      migration from version 1.
