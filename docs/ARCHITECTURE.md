# Architecture

3D Modeler is a TypeScript + React + Vite + Three.js browser application. Modeling logic
does not depend on React or Three.js; React renders the interface and Three.js renders and
picks the viewport.

```
src/
  core/         units, tolerances, vectors, ID helpers                (pure)
  model/        document types, validation, modeling operations      (pure)
  geometry/     rectangle construction, faces, render data, checks   (pure)
  history/      snapshot undo/redo                                   (pure)
  editor/       Editor store: state, the single commit path, selection
  tools/        ToolController: pointer/numeric interaction state machines
  persistence/  versioned JSON project files
  viewport/     Three.js scene, camera navigation, picking, highlights
  ui/           React components, actions, keyboard shortcuts
```

Data flows one way: **document → derived render data → Three.js objects**. Tools read
pointer input from the viewport, update a transient *preview*, and on completion call
`editor.commit()`.

## Coordinates and units

- Right-handed, **Z-up**. The ground and initial drawing plane is XY at z = 0.
- The internal length unit is the **meter**, stored as ordinary JavaScript doubles.
- Units are converted only at the input/display boundary (`src/core/units.ts`). Unitless
  input uses the document's display unit (millimeters by default).
- Tolerances live in `src/core/tolerance.ts`: the smallest accepted width/depth/height is
  0.1 mm and the largest coordinate or dimension is 10 km. Coordinates are compared with a
  1 nm epsilon. This is the supported scale range.

## Document model

`src/model/types.ts`. The canonical document holds editable modeling data only:

```ts
ModelDocument {
  lengthUnit: 'm';           // always meters
  displayUnit: 'mm' | 'cm' | 'm' | 'in' | 'ft';
  nextId: number;            // allocator for stable object IDs, saved with the file
  objects: ProfileObject[];
}
ProfileObject {
  id: 'obj-<n>';             // stable; never reused, survives save/reload and undo
  type: 'profile';
  name: string;
  profile: {
    kind: 'rectangle';
    plane: { origin, xAxis, yAxis };   // local plane; normal = xAxis × yAxis
    boundary: Vec2[];                  // ordered, counter-clockwise loop in plane coordinates
  };
  extrusion: { height } | null;        // total height along the plane normal; > 0
  transform: { translation: Vec3 };    // world placement
}
```

Milestone 1 restricts this to an axis-aligned rectangle `[0,0],[w,0],[w,d],[0,d]` on the
local XY plane with a translation-only transform. The fields are already shaped for a
general polygon profile (`kind: 'polygon'`), other planes (a different `plane` basis) and
full transforms, so these can be added without changing what existing files mean.

Documents are immutable. Operations in `src/model/operations.ts` (`createRectangle`,
`extrudeProfile`, `setExtrusionHeight`, `deleteObject`) return a new document that shares
unchanged objects, or an error. Each produced object is validated
(`src/model/validate.ts`) before it can be committed: non-finite values, out-of-range
values, zero-area rectangles, zero/negative heights and references to missing objects are
rejected.

## Geometry and render mapping

`src/geometry/solid.ts` derives everything the viewport draws:

- `buildFaces(obj)` produces **logical faces** with semantic IDs: `profile` for a planar
  rectangle; `base`, `cap` and `side-0..3` for an extrusion (`side-i` is generated from
  boundary edge i). Every face loop is counter-clockwise seen from outside, so normals point
  outward.
- `buildRenderData(obj)` triangulates each face (fan triangulation, valid for the convex
  profiles supported now) and records `triangleFaces[t]`, the logical face of triangle *t*.
  Faces do not share vertices, giving flat normals.
- Edges are emitted from face boundary loops, so only real profile/solid edges are drawn,
  never triangulation diagonals.
- `checkSolid()` verifies a closed mesh: every directed edge is matched by exactly one
  opposite edge (closed, consistently oriented, manifold), stored normals agree with the
  winding, and the signed volume is positive.
- Solids are rendered single-sided (`FrontSide`), so a wrongly wound face would be visible
  as a hole rather than hidden. A planar rectangle has no inside; it is an explicit
  two-sided sheet (a front and a reversed back triangle set), not a double-sided material.

Picking (`src/viewport/Viewport.ts`) raycasts the object meshes; the hit's `faceIndex`
maps through `triangleFaces` to `(objectId, faceId)`. Highlighting draws every triangle of
the logical face, so a rectangle selects as one face even though it is two triangles.

Render meshes are rebuilt only for objects whose document entry changed (object identity),
and replaced geometries are disposed. Materials are shared and disposed with the viewport.

## Commands, history and previews

- **Single commit path:** `Editor.commit(label, op)` runs a pure operation. On success it
  records one history entry and clears the redo branch; on failure it shows the error and
  changes nothing. Delete, draw, push/pull and height edits all use it.
- **Previews are transient:** pointer movement only calls `editor.setPreview()`. A preview
  is rendered (the push/pull preview temporarily hides the real object) but is never put in
  the document or history. Escape (`ToolController.cancel`) clears it.
- **One gesture, one entry:** click-click and press-drag-release each commit exactly once.
  A zero-distance push/pull is a no-op, not an edit.
- **Snapshot history** (`src/history/history.ts`): entries store the previous document and
  selection. Because documents share unchanged objects, a snapshot costs the object array
  plus changed objects. The undo stack is bounded at 200 entries (oldest dropped). Undo and
  redo restore selection and sanitize it against the restored document (e.g. a selected
  cap becomes the planar face again when an extrusion is undone).
- Camera navigation, hover, tool changes, snap settings and the display unit are not model
  history.
- **New/Open** clear history.

## Tools and input

`src/tools/ToolController.ts` contains the select, rectangle and push/pull state machines.
It receives a pick ray, the logical face under the cursor and screen coordinates from the
viewport, so it is unit-testable without WebGL.

- Rectangle: ground-plane intersection, optional grid snap, normalization of any drawing
  direction to a positive-size, counter-clockwise rectangle.
- Push/pull: projects the pointer ray onto the vertical line through the clicked point
  (closest point between two lines). The *total* height snaps to the grid when snap is on.
- Numeric entry calls the same operations as the mouse.

Mouse ownership is split so tools and navigation never consume the same drag: the left
button belongs to the active tool (or to orbiting with the Orbit tool), middle orbits,
right pans, and the wheel zooms (`OrbitControls`).

## Persistence

`src/persistence/projectFile.ts`. Files are UTF-8 JSON:

```json
{
  "format": "3d-modeler-project",
  "version": 1,
  "generator": "3D Modeler 0.1.0",
  "document": { "lengthUnit": "m", "displayUnit": "mm", "nextId": 2, "objects": [ ... ] },
  "view": { "position": [x, y, z], "target": [x, y, z] }
}
```

- `document` is exactly the editable model above; `view` (camera) is optional and ignored
  if invalid.
- Opening validates the entire file first (format, version, every object, unique IDs,
  `nextId` greater than every used ID) and only then replaces the open project.
- An unknown `format` or a `version` other than 1 is rejected; a higher version tells the
  user to update the app. Future versions should add explicit migrations from older
  versions here.
- Saving uses a Blob download, which works in all browsers. The File System Access API can
  be added later as an enhancement where available.

## Geometry boundaries (what this design does not do yet)

There is no boundary-representation topology (shared vertices/edges between faces),
no boolean operations, no face splitting and no general polygon triangulation. These are
deliberately deferred; see the roadmap. The `RenderData`/`FaceId` mapping is the seam a
richer topology model would replace: as long as it still produces faces with stable IDs and
triangle-to-face maps, the viewport, picking and tools keep working.
