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
Clang on macOS (arm64, CMake 4). All of these passed on pull request #1. A local build
with CMake 4.4 also passed, alongside the CMake 3.28 builds above.

## Performance (benchmark; run by hand)

`pullform_bench` times the CPU work done on every mouse move over the viewport
(`Viewport::pointerAt`): syncing the derived-mesh cache with the document, then ray casting
every mesh. GPU uploads go to a counting stub, so driver time is not included. It is built
with the tests but not run by CTest.

```
cmake -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DPULLFORM_BUILD_APP=OFF
cmake --build build/rel --target pullform_bench
build/rel/tests/pullform_bench          # 5,000 boxes; pass a number for another count
```

Release build, GCC 13, Linux x64, one core of a 2.1 GHz Xeon (cloud VM):

| Step (average) | 5,000 boxes | 20,000 boxes |
| --- | --- | --- |
| First sync (build every mesh) | 15.7 ms | 65.6 ms |
| Sync, document unchanged | 0.002 ms | 0.009 ms |
| Sync, document unchanged, 0.2.0 algorithm | 14.7 ms | 243.6 ms |
| Sync after one object changed | 0.5 ms | 3.3 ms |
| Ray cast against every mesh | 0.8 ms | 3.5 ms |
| **Hover total** | **0.8 ms** | **3.2 ms** |
| Hover total in 0.2.0 (sync + ray cast) | 15.5 ms | 247 ms |

0.2.0 ran that sync on every hover and again on each redraw, so hovering a 5,000-box model
cost about 30 ms of CPU per mouse move. The sync is now skipped when the object list is
unchanged and uses an ID set otherwise. What remains is linear: the ray cast tests every
triangle, and an edit still compares every object pointer once. A bounding-volume check
before the per-triangle test is the next step if larger models need it. Not measured: a
real GPU, the GPU upload cost on first load, and frame rate with a 5,000-box model on
screen.

## Manual (performed once; not automated)

Run on Linux under Xvfb with Mesa llvmpipe (software OpenGL 3.3/4.5), driving the app
with `xdotool` and inspecting screenshots:

- Empty scene with adaptive grid and axes; orbit, pan, zoom toward the cursor, Fit.
- Rectangle by click-click and by typing `2000,1000`; push/pull by drag and by typing `750`.
- Select the cap, type a total height of `900`; Ctrl+Z restores 750.
- Second object, Delete, undo.
- Move (M): press X, type `500`, Enter moves the box 500 mm along X (Position X 500 mm);
  click the box and move the pointer for a live preview with a faint outline at the start;
  the Right arrow locks the preview to X (red guide, "300 mm along X" in Measurements); Esc
  cancels without an undo entry.
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

- Native Windows: the installer actually installing and the app running on Windows 10/11,
  high-DPI behavior there, and the Windows file dialogs (CI only builds and runs tests).
- macOS: running the app (CI only builds and runs tests); Retina scaling.
- A real GPU and driver, and a physical display and mouse (only software rendering).
- The window close button path (it uses the same code path as Ctrl+Q, which was tested).
- Frame rate and GPU upload time with large models on real hardware (only the CPU side of
  hovering is benchmarked; see above).
