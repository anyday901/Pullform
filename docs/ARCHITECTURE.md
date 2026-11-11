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
    std::vector<Guide> guides;         // construction guide lines and points (small values)
};
struct ProfileObject {
    ObjectId id;                       // stable: never reused; survives undo/redo/save/load
    std::string name;
    Profile profile;                   // kind, plane basis, ordered CCW boundary (plane coords)
    std::optional<Extrusion> extrusion;// total height along the plane normal; > 0
    Transform transform;               // rigid: translation + rotation (unit quaternion)
};
```

The profile lies on the local XY plane and is one of three kinds (`ProfileKind`): an
axis-aligned rectangle `[0,0],[w,0],[w,d],[0,d]`, or a **circle** or **polygon**, both a
regular polygon centred on the local origin (vertex i at radius r and angle
start + 2*pi*i/n; `core/geometry/circle.*`). The object's translation is the circle's
center, so its base centre is exactly the origin. The fields are already shaped for a
general polygon profile and other planes (a different `PlaneBasis`), so those can be added
without changing what existing files mean. Validation (`core/document/validate.*`) enforces the current restrictions
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

A `Guide` (`guide-<n>`, also numbered from `nextId`) is an infinite line (`point`, unit
`direction`) or a single point in world space. Guides are not geometry: they belong to no
object, never make edges or faces, and stay put when objects move. `addGuide` and
`deleteGuides` commit them like any other edit, so they undo and redo.

Objects are immutable once in a document. Operations (`core/commands/operations.*`:
`createRectangle`, `createCircle`, `extrudeProfile`, `setExtrusionHeight`, `rotateObject`, `scaleObject`,
`duplicateObject`, `deleteObject`,
`setDisplayUnit`) are pure functions that return a new document sharing unchanged objects,
or an `Error` with a user-facing message.

## Edge/face mesh and groups

`core/geometry/mesh.*` (design and the alternatives considered: `docs/GEOMETRY.md`). Every
object is a **group** that owns its geometry, so nothing merges across objects. During the
Phase 1 transition a group is one of two kinds:

- a **profile group** (`ProfileObject::mesh` empty): a rectangle or box. Its profile and
  extrusion stay the source of truth for push/pull, scale and dimensions, and
  `mesh::objectMesh` derives its mesh with fixed ids: vertex id = corner index + 1 (the
  `objectVertex` numbering), edges = base 1..n, cap n+1..2n, vertical 2n+1..3n, faces =
  Profile/Base 1, Cap 2, Side i 3 + i (`mesh::faceIdFor`, `mesh::semanticFace`).
- a **mesh group** (`mesh` set, empty profile, no extrusion): free edges and faces in
  object-local space. The first mesh edit of a profile group (`drawEdge` / `eraseEdge`
  with its id) converts it in the same commit, keeping the ids above, so corner-indexed
  dimension anchors keep their meaning. Corner index i of a mesh group is vertex id i + 1.

`mesh::Mesh` is an index-based edge-use mesh (not strict half-edge: loose edges and edges
with three or more faces are allowed). Vertices, edges, faces and curves live in id-keyed
maps; ids are `uint32` and never reused, and the mesh keeps the next id of each kind. A
face is an outer loop (counter-clockwise about its cached unit normal) plus hole loops
(clockwise); a loop is a list of edge uses `(edge, reversed)`. Curves (arc/circle with
center, normal, x axis, radius, start, sweep, segments and their edges) are stored beside
the segments; topology only sees segments, and a curve whose edges no longer chain is
dropped (`pruneCurves`).

- `addEdge(a, b)`: ends weld to an existing vertex within `tol::kWeldDistance` (1 um) or
  split the edge they land on; existing edges crossed and vertices passed split the new
  edge. Each new piece then either lies across a face (coplanar within `tol::kPlanarity`,
  midpoint inside): the planar graph inside that face is traced on both sides of the
  piece (always taking the sharpest turn, after pruning dangling edges); two loops split
  the face, one loop that touches nothing is an island (a hole in the face plus an inner
  face). Otherwise every plane spanned with a neighbouring edge is tried, and a closed
  loop that does not overlap an existing coplanar face becomes a face, taking any
  coplanar faces wholly inside it as holes. Horizontal faces face +Z.
- `eraseEdge(id)`: two coplanar faces sharing the edge merge (outer loops spliced,
  leftover back-and-forth spurs dropped); a coplanar inner face is absorbed by the face it
  is a hole of; any other face using the edge is removed. Vertices left without edges go.
- `validateMesh` (run by `validateObject`, so on every commit and file open): in-range
  finite positions, edges between distinct vertices at least 1 um apart, no duplicate
  edges, closed loops without repeated vertices, planar faces, outer CCW with area, holes
  CW and inside, ids below the next ids.
- `triangulateFace` is ear clipping with holes bridged in. `buildMesh` renders a mesh
  group's faces two-sided like a planar profile, with every mesh edge as a line;
  `buildFaces` gives each face's outer loop (FaceKind::Mesh with the mesh face id), so
  picking, selection, snapping and bounds work unchanged.
- Undo cost: a mesh is immutable and shared like an object, so a snapshot still costs one
  pointer per unchanged object; an edit copies the edited group's mesh only. Adjacency
  queries (`facesOfEdge`, `edgesOfVertex`) scan and are fine at today's sizes; an index
  can be added behind the same functions.

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

**Elements** (vertices, edges, faces) are picked through `core/geometry/topology.*` and
`element_pick.*`. `buildTopology` turns an object's edge/face mesh (`mesh::objectMesh`)
into a world-space `ElementTopology`: shared vertices, edges and faces with adjacency,
a triangulation and the mesh's stable element ids. The mesh cache keeps one per object.
`pickElement` takes the pointer ray and the pixel angle and returns, in priority order, a
vertex within 7 px, an edge within 5 px, or the nearest face; a vertex or edge counts only
if no face other than its own lies between it and the eye, so hidden edges are never
picked. Objects whose bounds the ray misses (with the pixel margin) are skipped.

The Select tool hovers and selects elements (`app/tools/element_select.*`): a click selects
the element, a double-click a face with its edges (or an edge with its faces, a vertex with
its edges), a triple-click everything connected through shared vertices. The app counts
clicks (`ClickCounter`: 0.4 s, 4 px) and passes the count in `PointerInput::clicks`.
`SelectionSet` keeps element entries beside whole objects; the owner of a selected element
is always in `objects()`, so Move, Rotate, Scale, Duplicate and Delete act on it, and the
primary keeps the clicked face for push/pull. Selecting an object whole drops its element
entries. The viewport draws selected faces with the blue overlay, selected edges wide and
blue, the hovered element in orange, and, with View > Profile edges, outline edges
(borders and silhouettes, `profileEdges`) 2.5 px wide.

**Rendering** goes through `app/viewport/renderer.hpp` (create/destroy mesh, draw mesh,
draw face overlay, draw lines, draw wide lines). Core-profile OpenGL has no dependable
line widths, so wide lines are expanded on the CPU into screen-aligned quads in clip space
(clipped at the near plane first). The OpenGL 3.3 core implementation compiles its shaders
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
`orbit_tool`, `measure_tool` (tape measure and dimension are two instances of it) and
`eraser_tool`.
Shared parts live in `ToolContext` (editor, snap settings, the point snapper, the snap
indicator, ground and pointer snapping) and `app/tools/tool_support.*` (axis keys and
locks, grid steps, numeric-entry trimming, drag threshold, `baseCenter`).

**Inference.** Every tool snaps through the one `PointSnapper` (`app/tools/snapper.*`),
whose `infer` runs the pure engine in `core/geometry/inference.*` against the
`SnapIndex` of the document (`core/geometry/snap.*`). In priority order it finds a point
feature (Endpoint, Midpoint, Center of a mesh curve), an Intersection (two edges, or an
edge and an inference line), On Edge, then a line inference: from the gesture's start
point along X/Y/Z (Axis), parallel or perpendicular to a reference edge (the magenta
guides), or from a remembered point along an axis (From Point); two lines crossing near
the pointer give their crossing. Otherwise the face under the pointer (On Face), the
drawing plane or the ground grid. Reference edges are the edges the pointer last visited
plus every edge through the start point. A result carries the line or plane it lies on
(`SnapConstraint`); holding Shift locks it until Shift is released, and point features are
then projected onto it. `snapLabel` names the inference for the tooltip beside the pointer
and the status bar. A tool passes its start point (`InferOptions::from`), its drawing plane
and the object being edited; today the tape measure, dimension and Move use the full
engine, the rectangle tool keeps its ground snap (now with On Edge), and push/pull its
height snaps.

`app/tools/tool_controller.*` owns the tools and routes events, keys and typed values to the
current one; its tool-specific entry points (`submitMoveDistance`, `toggleAxisLock`, ...)
forward to the matching tool. Adding a tool means a new `Tool` value with its `toolName`,
a new file pair, and one `add<...>(Tool::...)` registration line in the
`ToolController` constructor. The controller receives a `PointerInput` (world ray,
resolved pick, screen position), so tests drive it with synthetic events and no window.

- Drawing planes (`app/tools/drawing_plane.*`, math in `core/geometry/plane.*`): a
  `DrawingPlane` picks the plane a drawing tool draws on and is meant for every drawing
  tool (Rectangle now; Line and the curve tools). Before the first click it follows the
  pointer: the face under it (`pickedPlane`: the face's plane through one of its corners,
  facing the viewer), else the ground. Arrow keys lock a plane facing world X (Right) or
  Y (Left), a horizontal plane (Up), each through the snapped 3D point under the pointer,
  or (Down) the plane last hovered; the same key releases. `begin` fixes the plane at the
  first click and `point` snaps later points onto it; an arrow during the gesture turns
  the plane about the first point. `snapOnPlane` is the ground snap on any plane: a point
  snap projected onto the plane (guide from the feature), else ray/plane intersection
  with inference along the plane's axes and the grid in plane coordinates. Plane frames
  are `PlaneBasis` values with axes chosen by `planeThrough` (horizontal planes use world
  X; others use the horizontal in-plane direction and uphill), and an origin at the
  plane's point nearest the world origin, so axis-aligned planes keep the world grid and
  exact coordinates. On the ground this reproduces the previous ground snapping exactly.
- Circle and Polygon (`app/tools/circle_tool.*`, one class for both): click the center on
  the drawing plane (`DrawingPlane`, as for Rectangle), then a point on the circle, or
  type the radius; the first vertex points at the second click, so that point is on the
  shape. A typed count such as `24s` sets the segments (circle, default 24) or sides
  (polygon, default 6), before or during drawing, and is kept for the next one. Both call
  `createCircle`; a `CirclePlacement` carries the plane rotation like a
  `RectanglePlacement`, and `flippedProfile` turns a circle over by mirroring its start
  angle (corner i becomes (n - i) mod n, `flippedCorner`). **True curves:**
  `meshFromProfile` gives a circle or polygon a `Curve` per loop (curve 1 = base edges
  1..n, curve 2 = cap edges n+1..2n; kind Circle or Polygon), so the shape survives
  conversion to a mesh group and its center snaps (`SnapKind::Center`, from every curve in
  `objectMesh`, so drawn arcs get it too). A cylinder's side edges are soft and smooth:
  not drawn or picked, and `buildMesh` shades the sides with radial normals. Scale keeps a
  circle or polygon regular (equal width and depth factors only).
- Rectangle: a rectangle on the drawing plane, normalized to a positive-size,
  counter-clockwise rectangle along the plane's axes (`rectangleOnPlane`). A
  `RectanglePlacement` carries a rotation (`planeRotation`): the object's transform turns
  its local XY rectangle onto the plane, so no new profile kind or file field is needed.
  Typed sizes follow the pointer's direction from the first corner on the plane. The
  Rotated Rectangle tool is the same class in a three-click mode (`rotatedRectangle`).
  Mouse and numeric entry call the same `createRectangle` operation.
- Coplanar objects: a rectangle drawn on a face lies exactly on it. Picks at equal
  distance go to the later object (`nearerHit`, and the same rule in element picking), and
  the renderer offsets flat sheets less than solids so the sheet draws in front.
- Push/pull: the pointer ray is projected onto the line through the clicked point along
  the face normal (closest point between two lines). A flat rectangle may go either way:
  a negative height flips it first (`flippedProfile`: half a turn about its local X axis,
  same footprint, corner i becomes corner 3 - i with dimension anchors renumbered), so the
  stored height stays positive. The **total** height snaps to the grid increment when
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
- Guides in snapping (`core/geometry/guide_snap.*`): a guide point (`GuidePoint`) and a
  crossing of a guide line with another guide line or an object edge (`Intersection`) are
  point candidates that compete with endpoints and midpoints on the same weighted angular
  score (a guide point weighs like an endpoint, a crossing between an endpoint and a
  midpoint). The inference engine takes them through `InferenceQuery::guides` and also
  adds each guide line (in the drawing plane, if there is one) to its inference lines as
  `OnGuide`, so a guide line crosses edges, axis lines and parallel/perpendicular lines
  like any other line, and Shift locks it. `PointSnapper::point` (push/pull, the rectangle
  tool's ground snap) checks the same point candidates, and the ground snap falls back to
  the nearest guide line after edges. `SnapSettings::guides` (View > Guides) hides guides
  and turns their snapping off together.
- Arcs (`app/tools/arc_tool.*`, geometry in `core/geometry/arc.*`): one `ArcTool` class
  in three modes (Arc, 3 Point Arc, Pie). An arc is an `ArcGeometry` (centre, normal, x axis,
  radius, start, signed sweep: the fields of `mesh::Curve`), built by `arcFromBulge`,
  `arcThroughPoints`, `arcFromCenter` or `tangentArc`. The drawing plane is the face under
  the first point (when the point is on it) or the horizontal plane through it; later
  points are the pointer on that plane, taking endpoint and midpoint snaps that lie on it.
  When the first point is the open end of a line or arc (`openEndTangent`: a vertex with
  one edge; for an arc segment the arc's exact tangent), the bulge snaps to the tangent
  arc within the snap radius and the preview turns cyan. The pie's sweep is unwrapped
  as the pointer moves (`unwrapSweep`), so it can pass half a turn. `drawArc` commits one
  entry: `Mesh::addCurve` draws the segments with `addEdge` (so loops close into faces
  as for lines) and records the curve over the resulting chain of edges.
- Tape measure and dimensions (`app/tools/measure_tool.*`): `MeasureTool` snaps each
  pointer event with `ToolContext::measureSnap` (built on `snapPointer`). An endpoint or
  midpoint snap becomes an attached anchor through `anchorAt`, which matches the point
  against the object's corners and edge midpoints, so the anchor does not depend on the
  snap index's feature order. The dimension line offset comes from `offsetFromRay`: the
  pointer ray meets the plane through the measured segment that faces the viewer, and the
  offset is the part of that hit perpendicular to the segment. The viewport draws
  dimensions and the live measurement on top of the model; their labels are drawn by the
  UI at `Camera::project` of the line's middle. With guide creation on (Ctrl or Option
  tap), a tape started on an edge (`PointerInput::element`) or on a guide line adds a
  parallel guide line through the second point, and one started on a point adds a guide
  point; the live guide is in `MeasurePreview::guide`.
- Protractor (`app/tools/protractor_tool.*`): centre, baseline, angle, like Rotate, but the
  plane is the face under the pointer (or the ground, or the arrow-key axis lock) and the
  result is a guide line through the centre (`ProtractorPreview`, `protractorDirection`).
- The viewport draws guide lines as dashes of constant screen size out to a far
  distance, and guide points as small three-axis crosses, depth-tested like edges.
- Eraser (`app/tools/eraser_tool.*`): a press starts a stroke, and every edge the
  pointer passes over until release (`edgeUnder`: the picked edge, or for a picked vertex
  its edge nearest the pointer ray; plus `edgesCrossed`: visible edges whose screen image
  crosses the pointer's path since the last event, so fast drags miss nothing) is collected into an `ErasePreview`, drawn like the
  hover. The release commits once: `eraseEdges` (per group, `Mesh::eraseEdge` for each
  edge, so coplanar faces merge and a group left empty is deleted), or with Shift
  `setEdgesSoft(soft)` (hide) and with Ctrl `setEdgesSoft(soft, smooth)` (soften). The
  keys held at release decide. Soft edges are not drawn or picked; faces keep using them.
  There is no separate "hidden" edge flag (the file format has soft and smooth only), so
  hide and soften differ only in the smooth flag, which shading does not use yet.
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
  "version": 3,
  "generator": "Pullform 0.2.0",
  "document": {
    "lengthUnit": "m", "displayUnit": "mm", "nextId": 2,
    "objects": [{
      "id": "obj-1", "type": "group", "name": "Box 1",
      "transform": { "translation": [0,0,0], "rotation": [0,0,0,1] },
      "profile": { "kind": "rectangle",
                   "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
                   "boundary": [[0,0],[2,0],[2,1],[0,1]] },
      "extrusion": { "height": 0.75 },
      "mesh": {
        "vertices": [[1, 0,0,0], [2, 2,0,0], ...],      // [id, x, y, z], object-local
        "edges":    [[1, 1,2, 0], ...],                // [id, v0, v1, flags: 1 soft, 2 smooth]
        "faces":    [{ "id": 1, "loops": [[-4,-3,-2,-1]] }, ...], // +edge / -edge (reversed); loops[0] outer
        "curves":   [],
        "next":     [9, 13, 7, 1] } }],               // next vertex, edge, face, curve id
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
  files without annotations are unchanged. `document.guides` works the same way: each
  entry is `{"id": "guide-N", "kind": "line", "point": [x,y,z], "direction": [x,y,z]}` or
  `{"id": "guide-N", "kind": "point", "point": [x,y,z]}`. It is additive within version 3:
  earlier readers skip the key, so they still open such a file (without its guides). An anchor is `{"point": [x,y,z]}` or
  `{"object": "obj-N", "vertices": [i]}` (a corner) / `[i, j]` (an edge midpoint). It is
  part of file format version 2 together with rotation. Version 1 files saved while
  dimensions were new can carry the key; the migration keeps it, and readers that predate
  dimensions ignore it.
- Opening reads the file (max 50 MB), parses it, checks structure and types, then runs
  the same `validateDocument` used for every commit (ranges, rectangles, unique IDs,
  `nextId` greater than every used ID). Only then does it replace the open project.
- `transform.rotation` is a unit quaternion in glTF order `[x, y, z, w]`. Values within
  1e-4 of unit length (hand-edited files) are accepted and normalized.
- **Version 3 objects** are `"type": "group"` with a `mesh` (schema above; a face's
  normal is not stored, it follows from the outer loop). A group with `profile` and
  `extrusion` is a profile group: the profile is the truth, and the stored mesh must be
  exactly the one `meshFromProfile` makes (ids, loops, positions within 1 um), or the file
  is rejected. A group without them is a mesh group whose mesh is the truth.
- An unknown `format` or a version newer than 3 is rejected (a newer version asks the user
  to update). Older versions are migrated on the parsed JSON, one version at a time
  (`migrateProject`), before the reader runs, so the reader only knows the current schema.
  **Version 1 -> 2:** every object gains the identity rotation `[0,0,0,1]` (version 1
  transforms were translation only). The app says when it opened an older file; saving
  writes the current version. **Version 2 -> 3:** each object is read as a version 2
  profile, validated, and becomes `"type": "group"` with the mesh `meshFromProfile` makes
  (fixed ids above), so it renders exactly as before and its dimensions stay attached.
  Saving writes version 3, which Pullform 0.2.0 and earlier cannot open. Each future
  version adds one migration; `tests/data/` holds files of every previous version and
  `format_v3_tests.cpp` opens each of them, checks every corner against the file and
  checks the v3 round trip.
- Saving writes to `<file>.tmp-N` beside the target, flushes, and renames over the target
  (`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows,
  `rename(2)` elsewhere).
- Files written by the earlier TypeScript prototype are version 1 and open through the
  migration.

## Geometry boundaries (what this design does not do yet)

The edge/face mesh exists in the core. Push/pull works on any planar face
(`mesh::Mesh::pushPull`): a free face is extruded; otherwise each edge either stretches the
one neighbouring face that contains the push direction or gets a new side face, the face
lands on coplanar faces to cut an opening or fill a hole, and collinear edges left at the
old outline are joined. It does not intersect the moved face with geometry it passes
(that is refused) or with faces it lands on only partly. There are no booleans, and no
healing of collinear edges left after an erase. Arcs are drawn by the Arc tools; a curve record gives
centre snapping but is not yet used for selecting a whole curve. Dimension anchors still name corners by index
(vertex id - 1) rather than by vertex id. See docs/GEOMETRY.md and the roadmap.
