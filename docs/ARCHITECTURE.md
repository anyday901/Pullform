# Architecture

Pullform is a native C++20 desktop application. The editable model, geometry and history
live in a core library with no windowing, GPU or UI dependency; the application layers
interaction, rendering and interface on top.

```
src/
  core/                    pullform_core (static library; GLM + nlohmann/json only)
    document/              units, tolerances, entities, stable IDs, validation, project files
    geometry/              rectangles, faces, render meshes, solid checks, CPU ray picking
    commands/              modeling operations, selection, Editor (commit path + undo/redo)
  app/
    tools/                 pullform_app_logic: tool state machines, snapping, numeric entry
    viewport/camera.*      pullform_app_logic: orbit camera and pick rays (pure math)
    viewport/renderer.hpp  rendering interface
    viewport/gl_*.{hpp,cpp} OpenGL 3.3 core implementation (RAII GL handles)
    viewport/mesh_cache.*  pullform_app_logic: derived meshes per object, CPU picking
    viewport/viewport.*    picking glue, grid/axes/highlights/previews
    ui/                    Dear ImGui menus, toolbar, properties, measurements, status
    platform/              native file dialogs behind a small interface
    app.*, main.cpp        window, frame loop, input routing, file actions
tests/                     doctest unit tests (link pullform_app_logic only; headless)
tests/bench/               hover benchmark for large models (built, not run by CTest)
third_party/glad/          generated OpenGL 3.3 core loader
```

Dependencies point one way: `core` <- `app_logic` (tools, camera) <- application
(viewport rendering, UI, platform). `pullform_core` and `pullform_app_logic` build and
their tests run without a display or GPU (`headless` preset), so CI tests them on every
platform. Data flows **document -> derived meshes -> GPU buffers**. Tools receive pointer
input from the viewport, update a transient *preview*, and on completion call
`Editor::commit`.

## Coordinates, units and tolerances

- Right-handed, **Z-up**. The ground and initial drawing plane is XY at z = 0.
- The internal length unit is the **meter**, stored as `double` (`glm::dvec3`).
  Conversion to `float` happens only when uploading to the GPU, relative to a local origin
  (each mesh's minimum corner; lines relative to the eye), so precision does not degrade
  far from the world origin.
- Units are converted only at input and display (`core/document/units.*`). Unitless input
  uses the document's display unit (millimeters by default). Parsing is locale-independent.
- Tolerances live in `core/document/tolerance.hpp`: smallest width/depth/height 0.1 mm,
  largest coordinate or dimension 10 km, coordinate equality 1 nm. This is the supported
  scale range. Non-finite values, zero-area rectangles, non-positive heights, unknown
  objects and out-of-range values are rejected before anything is committed.

## Document model

`core/document/document.hpp`. The canonical document holds editable modeling data only:

```cpp
struct Document {
    LengthUnit displayUnit;            // display/input unit; storage is always meters
    uint64_t nextId;                   // allocator for stable object and dimension IDs (saved in files)
    std::vector<ObjectPtr> objects;    // shared_ptr<const ProfileObject>
    std::vector<Dimension> dimensions; // annotations (small values)
};
struct ProfileObject {
    ObjectId id;                       // stable: never reused; survives undo/redo/save/load
    std::string name;
    Profile profile;                   // kind, plane basis, ordered CCW boundary (plane coords)
    std::optional<Extrusion> extrusion;// total height along the plane normal; > 0
    Transform transform;               // rigid: translation + rotation (unit quaternion)
};
```

The profile is restricted to an axis-aligned rectangle `[0,0],[w,0],[w,d],[0,d]` on the
local XY plane. The fields are already shaped for a general polygon profile and other
planes (a different `PlaneBasis`), so those can be added without changing what existing
files mean. Validation (`core/document/validate.*`) enforces the current restrictions
explicitly, including that every placed corner stays inside the 10 km range.

**Transforms are rigid.** A local point `p` maps to `translation + R * p`, where `R` comes
from a unit quaternion (`localToWorld`, `profileToWorld`, `profileNormalWorld` in
`document.hpp`; `buildFaces` and therefore meshes, picking, bounds and snapping all go
through them). `rotationMatrix` makes entries within 1e-15 of -1, 0 or 1 exact, so quarter
turns keep grid-aligned corners exact. **Scale is not stored in the transform:**
`scaleObject` bakes factors into the real dimensions (rectangle width/depth, extrusion
height) and shifts the translation so the base centre stays put. Sizes shown and typed
therefore stay true lengths, push/pull heights need no unscaling, and vertex indices (used
by dimension anchors) do not change. A scale field belongs with component instances
later, when a definition is shared and cannot be resized in place.

A `Dimension` (`dim-<n>`, numbered from the same `nextId`, so a number is never used
twice) measures between two `DimensionAnchor`s and is drawn displaced by a world `offset`.
An anchor is either a free world point or attached to an object by corner index: one index
is a corner, two are the midpoint of the edge between them. Corners are numbered by
`objectVertex` (`core/geometry/measure.*`): boundary point i on the base is corner i, and
the cap point above it is corner n + i, so the numbering is independent of face order and
stays valid when the height changes or a planar profile is extruded. Attached ends are
resolved from the object every time, so a dimension follows its object. Deleting an object
deletes its dimensions in the same commit. A zero-length dimension is refused when it is
added, but not by validation, so an edit that brings a free end onto an attached one is
never rejected because of an annotation.

Objects are immutable once in a document. Operations (`core/commands/operations.*`:
`createRectangle`, `extrudeProfile`, `setExtrusionHeight`, `rotateObject`, `scaleObject`,
`duplicateObject`, `deleteObject`,
`setDisplayUnit`) are pure functions that return a new document sharing unchanged objects,
or an `Error` with a user-facing message.

## Geometry and render mapping

`core/geometry/solid.*` derives everything that is drawn and picked:

- `buildFaces(object)` produces **logical faces** with semantic IDs: `Profile` for a planar
  rectangle; `Base`, `Cap` and `Side i` for an extrusion (side *i* is generated from
  boundary edge *i*). Every face loop is counter-clockwise seen from outside, so normals
  point outward.
- `buildMesh(object)` triangulates each face (fan triangulation, valid for the convex
  profiles supported now) and records `triangleFaces[t]`, the logical face of triangle *t*,
  plus a contiguous index range per face. Faces do not share vertices (flat shading).
- Edges come from face boundary loops (deduplicated), so only real profile/solid edges are
  drawn, never triangulation diagonals.
- `checkSolid()` verifies a closed mesh: every directed edge has exactly one opposite
  twin (closed, manifold, consistently oriented), stored normals agree with winding, and
  the signed volume is positive.
- Back-face culling stays on for everything. A planar rectangle has no inside, so it is an
  explicit two-sided sheet (a front and a reversed back triangle set, both mapped to the
  `Profile` face) rather than a reason to disable culling. A wrongly wound face would show
  as a hole.

**Picking** is a CPU ray cast (Moller-Trumbore, back faces culled like rendering) against
the cached meshes; the hit triangle maps through `triangleFaces` to `(ObjectId, FaceId)`.
Highlighting draws the face's whole index range, so a rectangle selects and highlights as
one face even though it is two triangles. GPU meshes are rebuilt only for objects whose
document entry (pointer) changed, and are released when objects disappear.

**Rendering** goes through `app/viewport/renderer.hpp` (create/destroy mesh, draw mesh,
draw face overlay, draw lines). The OpenGL 3.3 core implementation compiles its shaders
from strings in the binary and owns every GL object through move-only RAII handles that
are released before the context is destroyed. A Vulkan or other backend can implement the
same interface.

## Commands, history and previews

- **Single commit path:** `Editor::commit(label, operation, selectAfter)` runs a pure
  operation, validates the *whole* resulting document, and only then makes it current with
  exactly one history entry, clearing the redo branch. On failure nothing changes and the
  error is shown in the status bar. Drawing, push/pull, moves, duplicates, height edits,
  delete and unit changes all use it.
- **Previews are transient:** pointer movement only calls `Editor::setPreview`. The viewport
  draws the preview (a push/pull or move preview temporarily replaces the real object's drawing),
  but it is never in the document or history. Escape (`ToolController::cancel`) clears it.
- **One gesture, one entry:** click-click and press-drag-release each commit once.
  A zero-distance push/pull or move is a no-op. Setting the same height again is a no-op.
- **Snapshot history:** entries store the previous `Document` and selection. Because
  objects are shared immutable pointers, a snapshot costs about 16 bytes per object plus
  the objects that changed. Each stack is bounded at 200 entries (oldest dropped), so
  history memory is at most 200 x (object count x 16 bytes + one changed object).
  Undo/redo restore selection and sanitize it (a selected cap becomes the planar face when
  an extrusion is undone).
- **Dirty state:** every committed state has an ID; the document is "unsaved" when the
  current state ID differs from the last saved one, so undoing back to the saved state is
  clean again.
- **Multi-selection** (`core/commands/selection.*`): a `SelectionSet` holds the selected
  objects in the order they were added, plus a *primary* entry (the object last clicked and
  the face under the pointer). `Editor::selection()` returns the primary, so single-object
  code (push/pull, the properties panel's height field) is unchanged; `selectionSet()` and
  `selectedObjects()` give the whole set. Click rules are pure functions: `applyClick`
  (plain click replaces, Shift toggles, Ctrl/Cmd adds, empty space clears unless a modifier
  is held) and `applyRegion` (the same for a box). Snapshots store the whole set, so undo
  and redo bring back a multi-selection. Selection changes are never history.
- **Window and crossing selection** (`core/geometry/region_select.*`): the Select tool's box
  drag projects each object's real corners and edges through `PointerInput::project` (the
  camera, filled by the viewport). Left to right is a *window*: every corner inside the
  rectangle (a corner behind the eye never counts). Right to left is a *crossing*: also an
  edge that meets the rectangle, or a rectangle that lies inside one of the object's faces
  on screen. While dragging, a `SelectBoxPreview` carries the rectangle and the objects the
  release would select; the UI draws it (solid blue window, dashed green crossing).
- **Operations on a selection:** `moveObjects`, `rotateObjects`, `scaleObjects`,
  `duplicateObjects` and `deleteObjects` run the single-object operation on each object
  and fail as a whole, so a selection changes in one commit and one undo entry. Grabbing
  one of several selected objects with Move, Rotate or Scale acts on all of them
  (`transformGroup`); grabbing an unselected object acts on it alone. Several objects
  rotate and scale about the base centre of their combined bounding box
  (`selectionBaseCenter`); scaling a group also moves each object's base centre away from
  that point by the same factors, so a uniform factor scales the group rigidly.
- Camera navigation, hover, tool changes and snap settings are not model history.
- **New/Open** replace the project and clear history.

## Tools and input

Each tool is its own class behind the small `ToolBase` interface in `app/tools/tool.hpp`:
pointer down/move/up, `key` (arrow and X/Y/Z keys), `submit` (the typed value), `cancel`,
`active`, `hint` (status bar) and `measurementLabel`. Tools publish their live preview to the
editor (`Editor::setPreview`) and commit one operation per gesture. One file pair per tool:
`select_tool`, `rectangle_tool`, `push_pull_tool`, `move_tool`, `rotate_tool`, `scale_tool`,
`orbit_tool` and `measure_tool` (tape measure and dimension are two instances of it).
Shared parts live in `ToolContext` (editor, snap settings, the point snapper, the snap
indicator, ground and pointer snapping) and `app/tools/tool_support.*` (axis keys and
locks, grid steps, numeric-entry trimming, drag threshold, `baseCenter`).

`app/tools/tool_controller.*` owns the tools and routes events, keys and typed values to the
current one; its tool-specific entry points (`submitMoveDistance`, `toggleAxisLock`, ...)
forward to the matching tool. Adding a tool means a new `Tool` value with its `toolName`,
a new file pair, and one `add<...>(Tool::...)` registration line in the
`ToolController` constructor. The controller receives a `PointerInput` (world ray,
resolved pick, screen position), so tests drive it with synthetic events and no window.

- Rectangle: ground-plane intersection, optional grid snap, normalization of any drawing
  direction to a positive-size, counter-clockwise rectangle. Typed sizes follow the
  pointer's direction from the first corner. Mouse and numeric entry call the same
  `createRectangle` operation.
- Push/pull: the pointer ray is projected onto the vertical line through the clicked point
  (closest point between two lines). The **total** height snaps to the grid increment when
  snap is on. Invalid heights (at or below the base) show as a red preview and cannot be
  committed; the gesture stays active.
- Move: picks an object and drags it in the horizontal plane through the picked point, or,
  with an axis lock (X/Y/Z keys or Right/Left/Up arrows), along the world axis through
  that point (the same closest-point projection as push/pull). With snap on, the object's
  corner (its translation) lands on the grid along each moving component. Every move,
  dragged or typed, commits through `MoveTool::commit` and the pure
  `moveObject` operation, which changes only the translation. The lock is released after
  a commit or a tool change. Out-of-range positions preview red and cannot be committed.
- Snapping (`core/geometry/snap.*`, used through `app/tools/snapper.*` by `ToolController`): a `SnapIndex` holds each
  object's `snapFeatures` (unique corners in `buildFaces` order, and unique edges as
  corner index pairs), kept in step with the document like the mesh cache. Tolerances are
  in screen pixels: the viewport puts the angle of one pixel in `PointerInput::pixelAngle`,
  and a point counts when its angular distance from the pointer ray is within
  `radiusPx * pixelAngle` (0 turns point snapping off, as in most tests). Priority:
  endpoint, then midpoint (an endpoint wins unless the midpoint is clearly nearer; among
  points at the same screen position the one nearest the eye wins; points behind the face
  under the pointer are ignored), then inference, then the grid. The rectangle tool
  projects a snapped point to the ground. Inference aligns the ground point with one of the
  last four snapped points along X or Y (never with the rectangle's own first corner, which
  would give zero width); push/pull aligns the height with any other object's corner. The
  result (`SnapResult`: point, kind, object, feature index, guides) is shown by the
  viewport as a marker and dashed guides drawn on top. `snapPointer` and
  `resolveSnapFeature` let other tools (tape measure, dimensions) snap the same way and
  refer to a corner or midpoint by object and feature index.
- Tape measure and dimensions (`app/tools/measure_tool.*`): `MeasureTool` snaps each
  pointer event with `ToolContext::measureSnap` (built on `snapPointer`). An endpoint or
  midpoint snap becomes an attached anchor through `anchorAt`, which matches the point
  against the object's corners and edge midpoints, so the anchor does not depend on the
  snap index's feature order. The dimension line offset comes from `offsetFromRay`: the
  pointer ray meets the plane through the measured segment that faces the viewer, and the
  offset is the part of that hit perpendicular to the segment. The viewport draws
  dimensions and the live measurement on top of the model; their labels are drawn by the
  UI at `Camera::project` of the line's middle.
- Input ownership (`App::handleViewportInput`): Dear ImGui owns the pointer over panels,
  menus and popups (`WantCaptureMouse`); a drag that starts in the viewport belongs to the
  viewport until its button is released. Left button = active tool (or orbit with the
  Orbit tool), middle = orbit (Shift = pan), right = pan, wheel = zoom. One drag never
  drives both the camera and a tool. Keyboard shortcuts are ignored while a text field has
  focus (`WantTextInput`) or a popup is open.
- The frame loop sleeps in `glfwWaitEvents` when idle and redraws only after input or a
  model change.

## Persistence

`core/document/project_file.*`. Files are UTF-8 JSON:

```json
{
  "format": "pullform-project",
  "version": 2,
  "generator": "Pullform 0.2.0",
  "document": {
    "lengthUnit": "m", "displayUnit": "mm", "nextId": 2,
    "objects": [{
      "id": "obj-1", "type": "profile", "name": "Box 1",
      "profile": { "kind": "rectangle",
                   "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
                   "boundary": [[0,0],[2,0],[2,1],[0,1]] },
      "extrusion": { "height": 0.75 },
      "transform": { "translation": [0,0,0], "rotation": [0,0,0,1] } }],
    "dimensions": [{
      "id": "dim-2",
      "a": { "object": "obj-1", "vertices": [0] },
      "b": { "point": [3, 0, 0] },
      "offset": [0, -0.4, 0] }]
  },
  "view": { "position": [x, y, z], "target": [x, y, z] }
}
```

- `document` is exactly the editable model; `view` (camera) is optional and ignored if
  invalid.
- `document.dimensions` is optional and only written when the project has dimensions, so
  files without annotations are unchanged. An anchor is `{"point": [x,y,z]}` or
  `{"object": "obj-N", "vertices": [i]}` (a corner) / `[i, j]` (an edge midpoint). It is
  part of file format version 2 together with rotation. Version 1 files saved while
  dimensions were new can carry the key; the migration keeps it, and readers that predate
  dimensions ignore it.
- Opening reads the file (max 50 MB), parses it, checks structure and types, then runs
  the same `validateDocument` used for every commit (ranges, rectangles, unique IDs,
  `nextId` greater than every used ID). Only then does it replace the open project.
- `transform.rotation` is a unit quaternion in glTF order `[x, y, z, w]`. Values within
  1e-4 of unit length (hand-edited files) are accepted and normalized.
- An unknown `format` or a version newer than 2 is rejected (a newer version asks the user
  to update). Older versions are migrated on the parsed JSON, one version at a time
  (`migrateProject`), before the reader runs, so the reader only knows the current schema.
  **Version 1 -> 2:** every object gains the identity rotation `[0,0,0,1]` (version 1
  transforms were translation only). The app says when it opened an older file; saving
  writes version 2, which Pullform 0.2.0 and earlier cannot open. Each future version adds
  one migration and a test that opens a file of every previous version.
- Saving writes to `<file>.tmp-N` beside the target, flushes, and renames over the target
  (`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows,
  `rename(2)` elsewhere).
- Files written by the earlier TypeScript prototype are version 1 and open through the
  migration.

## Geometry boundaries (what this design does not do yet)

There is no boundary-representation topology (shared vertices/edges between faces), no
boolean operations, no face splitting and no general polygon triangulation. These are
deliberately deferred; see the roadmap. `MeshData` with stable `FaceId`s and
triangle-to-face maps is the seam a richer topology model would replace: as long as it
still produces logical faces with stable IDs, the viewport, picking and tools keep
working.
