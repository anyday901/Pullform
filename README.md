# Pullform

A free, open-source 3D modeler for dimensioned geometry: buildings, rooms, furniture,
woodworking and other objects. It follows an approachable "draw a shape, push/pull it into
a solid" workflow. Pullform is a native desktop application written in C++20 with OpenGL
and Dear ImGui. It runs fully offline, with no account, subscription, cloud service or
network access.

Pullform is named for its core move: draw a profile, then pull it into form. The project
is not affiliated with SketchUp and does not read or write `.skp` files.

## What works (milestone 1)

1. An empty scene with an adaptive ground grid and red/green/blue X/Y/Z axes.
2. Orbit, pan, zoom toward the cursor, and **Fit** the camera to the model.
3. Draw a rectangle on the ground plane with the mouse (click-click or press-drag) in any
   direction, or type an exact width and depth. Optional grid snap with a visible state.
4. Push/pull a rectangle into a closed rectangular prism by pointer or exact distance.
5. Select an extrusion's top cap and change its height: drag it, type an incremental
   distance, or set the absolute **Total height**. The same object is updated.
6. Select and delete a whole object.
7. Undo and redo every committed modeling operation.
8. Save an editable, versioned project file (`.pullform.json`), reopen it, and keep editing.

### Current limits

- Shapes are rectangles on the ground plane (z = 0) and their straight extrusions only.
- Push/pull works on a planar rectangle and on an extrusion's top cap. Side and base faces
  cannot be pushed yet. Heights must be positive: pushing below the base, holes, cutting
  through solids and merging touching geometry are not supported.
- Objects stay independent when they touch or overlap.
- No move, rotate, scale or copy tools yet; objects stay where they were drawn.
- Rectangle width and depth cannot be edited after drawing (only the height).

The same limits are listed in the app's Properties panel. See
[docs/ROADMAP.md](docs/ROADMAP.md) for what comes next.

## Prerequisites

All platforms: **CMake 3.25+**, **Git** (dependencies are fetched by CMake on the first
configure, so the first configure needs network access; later builds work offline), and a
GPU driver with **OpenGL 3.3** support (any GPU from roughly 2010 onward, including Intel
integrated graphics).

| Platform | Compiler | Other |
| --- | --- | --- |
| Windows 10/11 x64 | Visual Studio 2022 (17.x) with "Desktop development with C++" | [NSIS 3](https://nsis.sourceforge.io/) to build the installer |
| Linux | GCC 11+ or Clang 15+, Ninja | `sudo apt install ninja-build libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev libgtk-3-dev` (Debian/Ubuntu) |
| macOS 13+ | Xcode 15+ command line tools (Apple Clang) | `brew install ninja` |

## Build, test, run and package

Each platform has one command that configures, builds, tests (and on Windows packages):

```sh
cmake --workflow --preset windows-release   # Windows (run from a normal or Developer PowerShell)
cmake --workflow --preset linux-release     # Linux
cmake --workflow --preset macos-release     # macOS
```

The individual steps on **Windows**:

```powershell
git clone https://github.com/anyday901/pullform.git
cd pullform
cmake --preset windows                          # configure (Visual Studio 2022, x64)
cmake --build --preset windows-debug            # or windows-release
ctest --preset windows-debug                    # headless unit tests
.\build\windows\src\app\Release\Pullform.exe    # run (after a Release build)
cpack --preset windows-installer                # -> build\windows\Pullform-0.2.0-win64.exe
```

The installer installs `Pullform.exe` with Start menu and desktop shortcuts and an
uninstaller. The executable links the C++ runtime statically and embeds its font and icon,
so it needs nothing else installed besides a graphics driver.

On **Linux** and **macOS**, replace `windows` with `linux` or `macos`:

```sh
cmake --preset linux
cmake --build --preset linux-release
ctest --preset linux-release
./build/linux/src/app/Release/Pullform [optional/project.pullform.json]
```

Other presets:

| Preset | Purpose |
| --- | --- |
| `headless` | Core library and tests only (no window/GPU dependencies), e.g. for servers |
| `linux-sanitize` | Debug build with AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) |

Run the sanitizer tests with `cmake --preset linux-sanitize && cmake --build --preset
linux-sanitize && ctest --preset linux-sanitize`. When running the sanitized *app* on
Linux, pass `LSAN_OPTIONS=suppressions=tools/lsan.supp` to silence small leaks inside
GTK's accessibility bridge (system libraries, not Pullform code).

### Dependencies

Dependencies are fetched with **CMake FetchContent**, each pinned to an exact release tag
(the resolved commit is recorded in [cmake/PullformDependencies.cmake](cmake/PullformDependencies.cmake)):
GLFW 3.4, Dear ImGui 1.91.9b, GLM 1.0.1, nlohmann/json 3.11.3, nativefiledialog-extended
1.2.1 and doctest 2.4.11 (tests). The glad OpenGL 3.3 core loader is generated and
committed in `third_party/glad`. To build without network access from an existing build
tree, configure with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.

## Controls

| Action | Mouse / key |
| --- | --- |
| Use the active tool | Left button |
| Orbit | Middle drag, or the Orbit tool (O) with left drag |
| Pan | Right drag, or Shift + middle drag |
| Zoom | Mouse wheel (toward the cursor) |
| Select tool | Space |
| Rectangle tool | R |
| Push/Pull tool | P |
| Orbit tool | O |
| Fit view to model | F |
| Toggle grid snap | G |
| Type an exact value | Start typing digits, then Enter (goes to the Measurements box) |
| Cancel the current operation, or clear the selection | Esc |
| Delete the selected object | Delete or Backspace |
| Undo | Ctrl+Z (Cmd+Z on macOS) |
| Redo | Ctrl+Y or Ctrl+Shift+Z (Cmd+Shift+Z on macOS) |
| New / Open / Save / Save As | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |

Single-key shortcuts never fire while a text field has keyboard focus or while a value is
being typed. The menus and Help > Controls show the same bindings.

### Measurements and units

Lengths are shown in **millimeters** by default (change the display unit in the
Properties panel). Typed values accept `mm`, `cm`, `m`, `in` (or `"`) and `ft` (or `'`)
suffixes, case-insensitive, with `.` as the decimal separator. **A value without a unit
uses the current display unit** (millimeters by default), so `2000`, `2000mm`, `200cm` and
`2m` are the same length. Compound values like `6'2"` and comma decimals are rejected
with an explanation.

- Rectangle tool: `width, depth` (for example `2000, 1000`). With a first corner placed,
  the rectangle is drawn from it toward the pointer; otherwise from the origin.
- Push/Pull tool: a distance *relative to the face's current position* (incremental).
- Select tool / Properties panel **Total height**: the absolute height from the base.

## Project files

`File > Save` writes a `.pullform.json` file: versioned JSON that stores the editable
model (units, stable object IDs, rectangle profiles, extrusion heights, placements) and
the camera view. It is not a mesh export. Files are written atomically (to a temporary
file that is flushed to the disk, then renamed), so a failed save or a power cut never
corrupts an existing project; the saved file keeps its permissions, and saving through a
symbolic link updates the linked file. Save As always uses the `.pullform.json`
extension and asks before replacing an existing file. Opening
validates the whole file first; a malformed file or an unsupported version shows an error
and leaves the open project untouched. Opening or starting a new project clears the undo
history. Pullform asks before discarding unsaved changes (New, Open, quit or closing the
window). Files saved by the earlier web prototype (format version 1) open unchanged.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): modules, document model, rendering,
  history, persistence, units and geometry boundaries.
- [docs/ROADMAP.md](docs/ROADMAP.md): completed and planned milestones.
- [docs/VERIFICATION.md](docs/VERIFICATION.md): what was verified, how, and what was not.
- [CONTRIBUTING.md](CONTRIBUTING.md) and [CHANGELOG.md](CHANGELOG.md).

## License

Pullform is MIT licensed ([LICENSE](LICENSE)). Bundled third-party components keep their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The icon, interface and
all first-party code are original to this project.
