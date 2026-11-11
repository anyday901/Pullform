# TODO

Planned updates, grouped into the pull requests they should ship in. PRs 2 to 6 come from
the code review of PR #1 (the native rewrite); each item names the file and the failure
it fixes. Feature milestones follow, with details in [docs/ROADMAP.md](docs/ROADMAP.md).
Suggested order is top to bottom.

## PR 2: Input and editing correctness (small, do first)

- [x] **Backspace deletes the selection after clearing a typed value.**
      `src/app/app.cpp` (`handleShortcuts`). Type `12`, press Backspace three times: the
      third press deletes the selected object. Make Backspace erase-only for a short time
      after typing, or bind delete to the Delete key only.
- [x] **Pressing Enter in Total height without typing changes the height.**
      `src/app/ui/ui.cpp`. The field is pre-filled with the rounded display string
      (for example `3.937` in), and Enter commits that rounded value as a new height with an
      undo entry. Commit only when the text was edited.
- [x] **Typed dimensions are not stored exactly.** `src/core/geometry/rectangle.cpp`
      (`rectangleFromSignedSize`). Width is converted to a second corner and re-derived by
      subtraction, so `2000` can become 1.9999999999999998 m, and a typed 0.1 mm minimum
      can be rejected. Pass the typed sizes straight through.
- [x] **A first corner near the horizon starts a rectangle that can never finish.**
      `src/app/tools/tool_controller.cpp` (`rectDown`). Validate the first corner against
      the 10 km range and refuse it with a message.
- [x] Tests for each of the above in `tests/tools_tests.cpp` and `tests/geometry_tests.cpp`.

## PR 3: File safety

- [x] **Saves are not durable across power loss.** `src/core/document/project_file.cpp`
      (`writeFileAtomically`). Add `fsync` (POSIX) / `FlushFileBuffers` (Windows) on the
      temporary file before the rename; keep the existing file's permissions and do not
      replace a symlink with a regular file.
- [x] **Save As can overwrite an existing project without asking.**
      `src/app/platform/file_dialog.cpp`. The `.pullform.json` extension is appended after
      the native dialog has done its overwrite check. Check the final path ourselves and
      confirm before replacing; make sure the dialog cannot produce `name.json`.
- [x] **Non-ASCII project path on the Windows command line fails to open.**
      `src/app/main.cpp`. Use the wide command line (`CommandLineToArgvW`) so double-clicking
      a project with any file name works once file association exists.
- [x] Tests: durable-write helper, extension handling, overwrite confirmation logic.

## PR 4: Interface polish

- [x] **UI sizes compound when the window moves between monitors with different scaling.**
      `src/app/app.cpp` (`applyScale`). Reset to a fresh `ImGuiStyle` before applying the
      theme and `ScaleAllSizes`.
- [x] **Tooltips never appear while the mouse is still, and the text caret stops blinking.**
      `src/app/app.cpp` (frame loop). Use `glfwWaitEventsTimeout` while an item is hovered
      or a text field is active.
- [ ] Test both on a real Windows display (not covered by CI).

## PR 5: Performance groundwork

- [x] **Hover picking is quadratic in object count.** `src/app/viewport/viewport.cpp`
      (`sync`). Skip the sync when the document has not changed, and look IDs up in a set.
- [x] Add a simple benchmark (for example 5,000 boxes) and record the numbers in
      `docs/VERIFICATION.md`.

## PR 6: Release hygiene

- [ ] Verify on real Windows 10/11: installer, app start, high-DPI, file dialogs. Record
      the results in `docs/VERIFICATION.md`.
- [ ] Run the macOS build on a Mac and record the result.
- [ ] Register the `.pullform.json` file association in the installer (depends on PR 3).
- [ ] Tag `v0.2.0` and attach the installer to a GitHub release.

## Feature milestones (one or more PRs each; see docs/ROADMAP.md)

- [x] **PR 7: Move tool** with typed distance and axis locking.
- [ ] **PR 8: Endpoint and midpoint snapping** with inference guides.
- [ ] **PR 9: Duplicate** objects (depends on Move).
- [ ] **PR 10: Tape measure** and dimension annotations (depends on snapping).
- [ ] **PR 11: Rotate and scale**, file format version 2 with a migration from version 1.
- [ ] Later: polygon profiles, drawing on faces, topology decision for general push/pull,
      groups and components, materials, export (glTF, OBJ, STL), autosave.
