# Changelog

All notable changes to Pullform are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). Until 1.0, minor versions may
include breaking changes, but project files will keep a migration path.

## [Unreleased]

### Added

- **Freehand tool** (Tools > Freehand, or the toolbar). Press and drag to draw a curve; on
  release it is smoothed (within 3 screen pixels of the stroke) into a polyline of straight
  edges and committed as one undo entry. It draws on the horizontal plane through the press
  point: the ground, or the top of the geometry it starts on, which it then becomes part of.
  Ending the stroke back on its start closes it into a loop that makes a face. A click or
  Esc draws nothing. The core gains `drawPolyline` and `freehand::smoothStroke`.
- **Inference engine.** Besides endpoints, midpoints and axis guides, the pointer now
  infers On Edge, On Face, Intersection (edge with edge, or with an inference line), Center
  of a true arc or circle, Parallel and Perpendicular to an edge (magenta guides, with the
  reference edge highlighted), lines along X, Y and Z from the start point, and lines from a
  remembered point along an axis. A tooltip beside the pointer names the inference
  ("Perpendicular to Edge"). Hold Shift to lock the current inference's line or plane; a
  corner hovered while locked is projected onto it. The tape measure and dimension tools
  infer from their first point, Move snaps its start and destination, the rectangle tool
  also snaps to edges, and snapping now works on lines drawn into mesh groups.
- **Arc tools.** Arc (A): click the start and the end, then move to set the bulge and
  click, or type the chord length and then the bulge. Starting from the open end of a line
  or arc, the bulge snaps to the tangent arc (drawn cyan), and double-clicking the end point
  takes it right away; the bulge also snaps to a half circle. 3 Point Arc: start, a point on
  the arc, end. Pie: centre, start of the arc (or type the radius), end (or type the angle);
  the pie closes into a face. Type `12s` with any arc tool for 12 segments (default 24 per
  full turn). Arcs are stored as true arcs (a curve record) plus their segment edges, and a
  loop closed by an arc becomes a face, so a rounded rectangle drawn from lines and tangent
  arcs is one face. Tools > Arcs and the toolbar list all three.
- **Draw on any face and plane.** The Rectangle tool draws on the face under the pointer,
  including walls and tilted faces, or on the ground; once the first corner is placed the
  plane stays fixed, so the second corner can be anywhere on screen. Arrow keys lock the
  plane: Right faces the red axis, Left the green axis, Up is horizontal (each through the
  point under the pointer), Down holds the plane of the face last hovered; the same key
  releases it. Typed sizes follow the pointer on that plane.
- **Rotated Rectangle tool:** click the first corner, the end of the first side (any
  direction on the plane), then the depth. Type the side's length after the first click and
  the depth after the second.
- **Push/pull a rectangle either way:** pushing a flat rectangle backwards (a window drawn
  on a wall, pushed in) extrudes it behind its face; typed negative distances do the same.
  A rectangle drawn on a face is still its own object, so it overlaps the wall rather than
  cutting it (cutting comes with push/pull on any face).
- **Circle (C) and Polygon (Shift+C) tools.** Click the center on a face or the ground (arrow keys
  lock a plane, as for Rectangle), then a point on the circle, or type the radius. Type a count such as `48s` (before or while drawing)
  for the number of segments (circle, default 24) or sides (polygon, default 6). Push/Pull
  extrudes them into cylinders and prisms (either way); a cylinder's sides are smooth, without a line per
  segment. Circles and polygons keep their true shape in the model, so their center snaps
  (a green circle marker), also on the top of a cylinder. The properties panel shows the
  radius and segment count. Saved in project files as profile kind `"circle"` or
  `"polygon"` with their curves.

- **Line tool (L).** Click a start point, then each next point, to draw a chain of edges
  (one undo entry per segment). Type a length to draw the next segment toward the pointer,
  or `x, y, z` for an offset; the arrow keys (or X, Y, Z) lock an axis, and the pointer
  infers the X, Y and Z axes through the last point. A segment that closes a planar loop
  makes a face and ends the chain; Esc ends it and keeps what was drawn. Drawn groups now
  snap to all their vertices and edges, including loose ones.
- **Push/pull of drawn faces.** A face standing on its own extrudes into a closed solid
  (either direction), and the end face of a drawn prism moves with its walls stretching.
- **Multi-selection.** With the Select tool, Shift-click adds or removes an object,
  Ctrl-click (Cmd on macOS) adds one, clicking empty space clears, and Ctrl+A / Cmd+A
  (Edit > Select all) selects everything. Drag a box left to right to select objects
  entirely inside it (window), or right to left to also select objects it touches
  (crossing); Shift and Ctrl work with boxes too, and Esc cancels a box. Move, Rotate,
  Scale, Duplicate and Delete act on the whole selection as one undo entry: grab any
  selected object to move, copy, turn or resize the group. Undo and redo restore the
  selection. The properties panel shows the group's overall size.
- **Edge/face mesh in the core** (`core/geometry/mesh.*`, design in `docs/GEOMETRY.md`):
  vertices, edges and faces with holes, with stable ids that are never reused. Drawing an
  edge welds its ends to nearby vertices (1 micrometer), splits edges it touches or crosses,
  makes a face when it closes a planar loop and splits a face it runs across; a loop drawn
  inside a face becomes a hole plus an inner face. Erasing the edge between two coplanar
  faces merges them. Meshes are validated on every commit and triangulated with holes.
  The `drawEdge` and `eraseEdge` operations commit these as single undo entries (no Line
  tool yet). Each object is now a group that owns its geometry.
- **Project file version 3:** every object is written as a `"group"` with its `"mesh"`.
  Rectangles and boxes keep their profile and height, and their mesh uses fixed ids
  (corner i is vertex i + 1), so dimensions stay attached. Version 1 and 2 files open
  through a migration and look exactly as before; saving writes version 3, which Pullform
  0.2.0 and earlier cannot open.
- **Edges and faces you can pick:** the Select tool highlights the vertex, edge or face under
  the pointer (hidden edges are never picked). Click selects that edge or face, double-click
  a face with its edges, triple-click everything connected. Selected edges draw wide and
  blue; Move, Rotate, Scale, Duplicate and Delete act on the objects they belong to.
- **Profile edges** (View > Profile edges): outline and silhouette edges drawn thicker.

- **Duplicate** (Edit > Duplicate, Ctrl+D / Cmd+D, or the Duplicate buttons): copies the
  selected object with a new stable ID and places it beside the original along +X. The copy
  becomes the selection, so repeating it builds a row. Editing either object leaves the
  other unchanged. One undo entry per duplicate.
- **Copy while moving:** with the Move tool, tap Ctrl (Option on macOS) to switch to copy
  mode, before the first click or in the middle of a move. Committing then places a
  duplicate at the destination (dragged or typed, with axis locks) and leaves the original
  where it was, as one undo entry. Tap again to go back to a plain move.

### Changed

- Internal: each tool is now its own class behind a small interface (`app/tools/tool.hpp`),
  with snapping, axis locks and numeric entry in shared helpers. No behavior change.

### Fixed

- An extra Backspace after clearing a typed value no longer deletes the selected object.
  Backspace is erase-only for one second after typing; Delete always deletes.
- Pressing Enter in Total height without editing it no longer replaces the height with the
  rounded display value (and no longer adds an undo entry).
- Typed rectangle sizes are stored exactly: `2000` from an off-grid first corner is 2 m,
  not 1.9999999999999998 m, and the 0.1 mm minimum is accepted.
- A first rectangle corner beyond the 10 km model range (clicked near the horizon) is
  refused with a message instead of starting a rectangle that could not be finished.
- Saves are flushed to the disk (`fsync`, `F_FULLFSYNC` on macOS, `FlushFileBuffers` on
  Windows) before the rename, so a power cut right after saving cannot leave an empty or
  truncated project. Saving keeps the file's permissions (and its ACL on Windows) and no
  longer replaces a symbolic link with a regular file.
- Save As could replace an existing project without asking when the `.pullform.json`
  extension was added after the dialog closed. Pullform now checks the final name and asks
  first. Typing `house` no longer saves `house.json`; it saves `house.pullform.json`.
- Opening a project from the Windows command line (or by double-clicking it, once the file
  association exists) works with non-English file names.
- Hovering no longer slows down quadratically with the number of objects. The viewport
  skips its mesh sync when the document has not changed and looks up object IDs in a set
  (hover over 5,000 boxes: about 15 ms to 0.8 ms of CPU per mouse move).

### Added

- Move tool (M). Drag an object, or click it and click the destination; X, Y, Z or the
  Right, Left and Up arrows lock the move to an axis. Type a distance to move exactly along
  the locked axis or the pointer direction, or `x, y, z` to move by a vector. Each move is
  one undo entry.
- **Rotate tool** (Q): click the object, a centre, a reference direction and the angle, or
  type degrees. Arrow keys pick the axis (Right X, Left Y, Up Z); grid snap gives 15 degree
  steps. Quarter turns about grid points keep corners exact.
- **Scale tool** (S): drag from a grip point or type a uniform factor (`2`, `150%`) or
  width, depth, height factors (`2, 1, 1`), about the centre of the base. Scaling changes
  the object's real dimensions, so sizes shown and typed stay true lengths.
- The Properties panel shows an object's true width, depth and rotation.
- Push/pull follows a rotated object's own direction.
- Endpoint and midpoint snapping. Rectangle corners and push/pull heights snap to the
  corners and edge midpoints of existing objects (within 12 pixels; corners win over
  nearby midpoints, and points hidden behind the face under the pointer are ignored). A
  green square marks an endpoint and a blue triangle a midpoint. A box's top corner snaps a
  rectangle to the ground point straight below it, with a vertical guide.
- Inference guides. Points you hover are remembered (the last four); moving in line with
  one along X or Y locks that coordinate and shows a dashed guide in the axis color. A
  push/pull height locks to the height of another object's corner (for example its cap)
  with a guide to that corner. The snap in use is shown at the start of the status hint.
- **Point snap** toggle in the toolbar and the Tools menu, next to Grid snap.
- `SnapIndex`, `snapFeatures` and `resolveSnapFeature` in `core/geometry/snap.*`, and
  `ToolController::snapPointer`, so other tools can snap to the same points and keep a
  stable (object, feature) reference.
- **Tape Measure** tool (T). Click two points to see the distance and its X, Y and Z parts
  in the measurements box, the status bar and a label in the view. Points snap to corners
  and edge midpoints, so a corner-to-corner distance is exactly the model's. Nothing is
  added to the model or the undo history.
- **Dimension** tool (D). Click two points, move to pull the dimension line away from them
  and click, or type the offset and press Enter. One undo entry. Ends placed on a corner or
  an edge midpoint stay attached: the dimension follows when the object's height changes
  or the object is moved. Dimensions are listed in the Properties
  panel, where each can be deleted, and are deleted with their object.
- Projects store dimensions in an optional `document.dimensions` array (omitted when there
  are none, so other files are unchanged). See `docs/ARCHITECTURE.md`.
- `pullform_bench`, a hover benchmark for large models; results in `docs/VERIFICATION.md`.

### Changed

- **Project file format version 2**: object transforms gain `rotation`, a unit quaternion
  `[x, y, z, w]`. Version 1 files open through a migration (identity rotation) and the
  status bar says so; saving writes version 2, which Pullform 0.2.0 cannot open.

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
