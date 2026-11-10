# Verification

What was checked for the 0.2.0 native rewrite, how, and what was **not** checked. Nothing
below is claimed beyond what is listed.

## Automated (repeatable; run by CI on every push and pull request)

| Check | Command | Local result |
| --- | --- | --- |
| Headless unit tests (39 test cases) | `cmake --preset headless && cmake --build build/headless && ctest --test-dir build/headless` | Pass, GCC 13, Linux x64 |
| Full app build with warnings as errors | `cmake --preset linux && cmake --build --preset linux-debug` | Clean, GCC 13 |
| AddressSanitizer + UndefinedBehaviorSanitizer | `cmake --preset linux-sanitize && cmake --build --preset linux-sanitize && ctest --preset linux-sanitize` | 39/39 pass, no reports |
| Windows cross-build (MinGW-w64 GCC, Release, `-Werror`) | `cmake -B build/mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DPULLFORM_WARNINGS_AS_ERRORS=ON && cmake --build build/mingw` | Clean; `Pullform.exe` imports only Windows system DLLs |
| Windows installer | `cpack -G NSIS` in `build/mingw` | `Pullform-0.2.0-win64.exe` produced (NSIS 3) |

The tests cover the brief's scenarios: rectangle creation and normalization, extrusion
into a closed manifold solid (`checkSolid`), cap height edits keeping the same object ID,
delete, undo/redo including bounds and dirty state, failed operations leaving the
document untouched, unit parsing, save/load round trips, rejected malformed and
future-version files, compatibility with a file from the web prototype, camera rays, and
a full pointer-driven tool workflow with synthetic input.

CI (`.github/workflows/ci.yml`) additionally builds and tests with MSVC on Windows
(Debug and Release, then packages and uploads the installer), Clang on Linux, and Apple
Clang on macOS. **Those CI jobs have not run yet**, because the GitHub repository did not
exist when this was written.

## Manual (performed once; not automated)

Run on Linux under Xvfb with Mesa llvmpipe (software OpenGL 3.3/4.5), driving the app
with `xdotool` and inspecting screenshots:

- Empty scene with adaptive grid and axes; orbit, pan, zoom toward the cursor, Fit.
- Rectangle by click-click and by typing `2000,1000`; push/pull by drag and by typing `750`.
- Select the cap, type a total height of `900`; Ctrl+Z restores 750.
- Second object, Delete, undo.
- Save through the GTK native dialog, reopen through the Open dialog; camera view restored.
- A file with `"version": 7` is rejected with an error dialog; the open model is unchanged.
- Unsaved-changes prompt on New and on Ctrl+Q; "Don't save" quits.
- UI at 1.75x scale (`PULLFORM_UI_SCALE=1.75`).
- The same session with the ASan/UBSan app build: no ASan or UBSan reports. LeakSanitizer
  reports only allocations inside GTK's accessibility bridge and D-Bus, suppressed by
  `tools/lsan.supp`.

Some synthetic key events were occasionally lost after GTK dialogs in Xvfb without a
window manager; repeating the step worked. This looked like an Xvfb/xdotool artifact
rather than an app defect, but it was not proven.

## Not verified

- Native Windows: the MSVC build, the installer actually installing and the app running on
  Windows 10/11, high-DPI behavior there, and the Windows file dialogs.
- macOS: building and running (no Mac was available); Retina scaling.
- Clang builds locally (only GCC was used locally; CI covers Clang).
- A real GPU and driver, and a physical display and mouse (only software rendering).
- The window close button path (it uses the same code path as Ctrl+Q, which was tested).
- Performance with large models (no benchmark yet).
