# Geometry model decision

Status: **accepted** by the project owner on 2026-11-11 (Phase 1, step 3). This document has no product code. It sets the
model that Phase 1 step 4 (edge/face mesh and file format version 3) and step 5 (render
and pick edges and faces) build on.

## Recommendation

**Build our own edge/face mesh in `pullform_core`. Do not adopt a geometry kernel now.**

- The model is an index-based **edge-use mesh**: vertices, edges and faces with holes,
  where each face loop is an ordered list of edge uses. It is half-edge-like, but an edge
  may have zero face uses (a loose line) or more than two (a fin, a T-junction of walls),
  which a strict half-edge structure cannot represent and which "sticky" drawing produces
  all the time.
- Every existing profile object becomes a **group holding its own mesh**. Geometry welds
  and splits only inside one mesh, so nothing merges across objects unexpectedly.
- Curves are stored as **true arcs plus their segment edges**. Topology only ever sees the
  segments; the arc record is kept for snapping, dimensions, re-segmenting and export.
- Weld tolerance is **1 um** (`1e-6` m), not the 1 nm coordinate equality used today.
- **OpenCascade is ruled out.** **Manifold** (Apache-2.0) was the candidate for solid
  booleans later (Intersect, Union, Subtract, Outer Shell on closed groups). When they
  were scheduled (item 20) the owner chose to write them on our own mesh instead; see
  [Solid booleans](#solid-booleans). It is never the model of record.

The main trade-off: we write and maintain the topology code ourselves (face creation,
splitting, merging, validation, robust 2D predicates). That is a few thousand lines of
careful code and a large test suite. In exchange we keep a small binary, a permissive
license, exact control over the sticky behaviour that defines the product, and cheap
immutable snapshots for undo.

## What "sticky" geometry needs

These are the behaviours the model must make natural, because they are what the user
feels:

1. Edges and faces that touch share vertices. Moving a vertex moves every edge and face
   that uses it.
2. Drawing edges that close a planar loop creates a face automatically.
3. An edge drawn across a face from boundary to boundary splits it into two faces. An edge
   that touches an existing edge mid-way splits that edge.
4. A closed loop drawn inside a face becomes a hole in that face plus a new inner face (so
   push/pull of the inner face makes an opening or a boss).
5. Erasing the edge between two coplanar faces merges them into one. Erasing a boundary
   edge of a face removes the face.
6. Loose edges (no face) are allowed and are ordinary geometry: guides for later faces.
7. An edge can bound more than two faces (two boxes sharing a wall line, an interior
   partition). Not every mesh is a closed manifold, and the model must not refuse to
   represent that.
8. Geometry in one group never sticks to geometry in another group.

Points 6 and 7 rule out a strict manifold half-edge mesh (OpenMesh, CGAL `Surface_mesh`,
a textbook half-edge). Point 4 rules out polygon-mesh libraries without faces with holes.
Points 1 to 5 are not something a B-rep kernel does for us either: OpenCascade shapes are
immutable topology trees, and the sticky edits would still be our code, written against a
much larger API.

## Options compared

| | Own edge-use mesh | Manifold | OpenCascade (OCCT) |
| --- | --- | --- | --- |
| What it is | Our planar polygon mesh with explicit topology | Robust triangle-mesh booleans on closed manifolds | Full B-rep kernel: NURBS, solids, booleans, fillets, STEP/IGES |
| Sticky edits (weld, auto-face, split, merge) | Native: this is what it is for | No: only closed triangle solids, no edges or n-gon faces | Possible through `BRepBuilderAPI`/`BOPAlgo`, but each edit rebuilds shapes; still our logic |
| Loose edges, 3+ faces per edge, faces with holes | Yes, by design | No (manifold triangles only) | Yes (wires, compounds, shells), awkwardly |
| Curves | Arc record plus segment edges | None (triangles) | Exact curves and surfaces |
| Booleans | Not ours to write; delegate | Its core strength; fast and robust | Yes, slow on large faceted models, needs tolerance tuning |
| Undo snapshot cost | Share immutable meshes per group; copy only the edited group | n/a (not a model) | Shapes are shared handles, but every edit creates new shapes and history is our job anyway |
| License | MIT (ours) | Apache-2.0 (plus Clipper2, BSL-1.0) | LGPL-2.1 with an OCCT exception |
| Binary size (Windows, release) | Tens of KB of code | About 1 to 2 MB (estimate, static, without TBB) | Tens of MB of DLLs for the modelling toolkits alone (estimate); far larger than today's 1.3 MB installer |
| Build | Part of `pullform_core`, builds headless everywhere | CMake, builds with our toolchain; FetchContent-friendly | Long build, many toolkits; usually a prebuilt SDK |
| Fit with our static MSVC runtime | Yes | Yes | LGPL relinking means shipping it as DLLs; static linking needs care to stay compliant |

Sizes in the table are estimates from typical builds, not measurements in this repository.
The Manifold figure was never measured: the booleans were written on our own mesh instead (see [Solid booleans](#solid-booleans)).

**Why not OpenCascade:** it solves problems we do not have (exact NURBS surfaces, fillets,
STEP) and does not solve the one we do (sticky editing feel). It would multiply the
installer size many times over, bring an LGPL obligation into a project that is otherwise
MIT/zlib/Apache, slow down every CI build, and its tolerance model (per-shape tolerances
that grow during booleans) is a poor match for a modeller whose users type exact sizes. If
STEP import/export is ever needed, it can be a separate optional converter process, not
the core.

**Why not Manifold as the model:** it represents closed triangle solids only. It is the
right tool for booleans between closed groups and nothing else, so it stays an optional
helper.

**Why not an existing half-edge library:** OpenMesh (BSD) and CGAL `Surface_mesh`
require manifold connectivity and have no faces with holes; CGAL is also GPL for most
packages. The data structure is the easy part; the operations are the work, and no
library ships SketchUp-style operations.

## Data model

All coordinates are `double`, meters, in the **group's local frame** (the group's rigid
`Transform` places it in the world, exactly as today). Working locally keeps precision
far from the origin and makes moving a group free (no vertex changes).

```cpp
namespace pf::mesh {

using VertexId = std::uint32_t;   // ids are per mesh, start at 1, never reused in a mesh
using EdgeId   = std::uint32_t;
using FaceId   = std::uint32_t;
using CurveId  = std::uint32_t;

struct Vertex { Vec3 position; };

struct EdgeFlags { bool soft = false; bool smooth = false; bool hidden = false; };

struct Edge {
    VertexId v0, v1;              // v0 != v1, no zero-length edges
    EdgeFlags flags;
    CurveId curve = 0;            // 0 = straight edge, not part of a curve
};

struct EdgeUse {                  // one side of an edge, as used by a face loop
    EdgeId edge;
    bool reversed;                // false: walks v0 -> v1
};

struct Loop { std::vector<EdgeUse> uses; };  // closed, consecutive uses share a vertex

struct Face {
    Loop outer;                   // counter-clockwise seen from the front (normal side)
    std::vector<Loop> holes;      // clockwise seen from the front, inside outer, disjoint
    Plane plane;                  // cached: unit normal + offset; derived, re-fit on edit
    // material, layer/tag: later, per face
};

struct Curve {
    enum class Kind { Arc, Circle } kind;
    Vec3 center, normal, xAxis;   // the curve's plane frame
    double radius;
    double startAngle, sweep;     // radians; Circle: sweep = 2*pi
    int segments;                 // the count used to build the edges
    std::vector<EdgeId> edges;    // in order along the curve
};

class Mesh {                      // immutable once in a document, like ProfileObject today
    // element storage: slot arrays with tombstones, behind accessors (see Undo)
    // adjacency (derived, rebuilt on load): vertex -> edges, edge -> face uses
    // next ids: nextVertexId, nextEdgeId, nextFaceId, nextCurveId (saved in files)
};

} // namespace pf::mesh
```

Notes:

- **Stable element ids.** Ids are allocated from per-mesh counters that are saved, so an
  id survives undo, redo, save and reload and is never reused within that mesh. Anything
  that names geometry (selection, hover, dimension anchors, later materials) names
  `(ObjectId group, VertexId|EdgeId|FaceId)`. When an operation splits an element, one
  piece keeps the old id (the larger face, the edge piece from `v0`), and the others get
  new ids, so selection and anchors degrade gracefully.
- **Loose edges** are edges with no uses. **Non-manifold edges** have three or more uses.
  Both are legal. "Is this a closed solid" is a query (`checkSolid` generalised: every
  edge has exactly two uses in opposite directions), not an invariant.
- **Derived data is never stored as truth.** The face plane, adjacency, the triangulation
  for rendering and the snap feature list are rebuilt from the elements.
- **Faces are planar polygons.** No face is ever non-planar. If moving vertices would make
  a face non-planar, the operation either triangulates it (SketchUp's behaviour, marking
  the new diagonals soft) or is refused. Moving vertices, edges and faces (roadmap item
  15, `Mesh::transformVertices`) folds: the face splits along soft edges into the
  largest planar pieces of its triangulation. A face with holes is still refused.
- **Rendering** triangulates each face with holes (ear clipping on the face plane, or a
  constrained triangulation if ear clipping proves fragile) and keeps the existing
  `MeshData` contract: triangles map to a face id, a face has a contiguous index range, and
  edges are drawn from real edges only (soft/hidden edges are skipped or dashed). This is
  the seam `ARCHITECTURE.md` already names, so the mesh cache, viewport and picking change
  only in what id type they report.

## Document and groups

```cpp
struct Group {
    ObjectId id;                       // the same obj-N ids as today
    std::string name;
    Transform transform;               // rigid, as today
    std::shared_ptr<const mesh::Mesh> mesh;
};
```

`Document::objects` holds groups. Nested groups, components (shared definitions with an
instance transform that may then carry a scale) and loose geometry at the document root
are later steps; the shapes above leave room for them (a root mesh is just a mesh without
a group; a component definition is a group whose mesh is shared by several instances).

The **rule that keeps geometry from sticking unexpectedly**: an operation acts on exactly
one mesh. Snapping still finds points in every group (so you can draw from one box's
corner), but the new edge is added to the group being edited and is welded only against
that group's vertices. Two boxes that touch stay two boxes.

Which mesh a drawing tool writes to is decided by the edit context: until "open group for
editing" exists, a tool that starts on a face writes into that face's group, and a tool
that starts on empty ground creates a new group (which is exactly what the Rectangle tool
does today). That keeps today's behaviour and tests intact.

## Migration of today's profile objects (file format 2 -> 3)

As proposed: **each profile object becomes a group holding its own mesh.** The migration
runs on the parsed JSON in `migrateProject`, like 1 -> 2, so the reader only knows the
version 3 schema.

For each `"type": "profile"` object:

1. Build its faces with today's `buildFaces` logic (in local space, before the transform).
2. Create vertices with **deterministic ids that equal today's corner numbering plus
   one**: boundary point *i* on the base is vertex `i + 1`, the cap point above it is
   vertex `n + i + 1` (`objectVertex` numbering). A planar profile has vertices `1..n`.
3. Create edges in a fixed order: base edges *i -> i+1* get ids `1..n`, cap edges
   `n+1..2n`, vertical edges `2n+1..3n`. A planar profile has edges `1..n`.
4. Create faces with fixed ids: `Profile` or `Base` = 1, `Cap` = 2, `Side i` = `3 + i`.
5. Keep `id`, `name` and `transform` unchanged. Write `next` counters one past the last id.
6. Dimension anchors keep their corner indices (`"vertices": [i]`), which equal vertex
   id - 1 under this numbering (see the Phase 1 transition below). When anchors move to
   vertex ids, that migration is `i -> i + 1`; a two-vertex anchor is still an edge
   midpoint.

Because the ids are deterministic, a migrated file renders the same, its dimensions attach
to the same points, and the old `FaceId` kinds have an exact mapping (useful for keeping
the existing tests readable: a test helper can say `capFace(obj)` and get face 2).

Existing operations are re-expressed on the group's mesh:

| Today | On a group mesh |
| --- | --- |
| `createRectangle` | new group with 4 vertices, 4 edges, 1 face |
| `extrudeProfile` (planar -> prism) | push/pull of the single face: new cap vertices, side faces |
| `setExtrusionHeight` | move a face along its normal; valid while its neighbouring faces stay planar (always true for a migrated prism's cap) |
| `scaleObject` | scale the group's vertices about the base centre (sizes stay true lengths, as today) |
| `moveObject`, `rotateObject`, `duplicateObject`, `deleteObject` | unchanged: they act on the group's transform or the group as a whole; duplicate shares the immutable mesh |

### Phase 1 transition: parametric groups

Rewriting push/pull, scale and dimensions onto meshes is Phase 2 work, so Phase 1 (step 4)
lands an intermediate state that keeps every existing tool and test working:

- A group is one of two kinds. A **profile group** (`"kind": "profile"`) keeps today's
  `profile` and `extrusion` as its source of truth; its mesh is **derived** from them with
  the deterministic ids above (and also written to the version 3 file, so readers can use
  it, but on load it is rebuilt from the profile and must match). A **mesh group**
  (`"kind": "mesh"`) has no profile, and its mesh is the truth.
- Rectangle, push/pull, scale and the height edit keep operating on profile groups
  parametrically, exactly as today.
- The first mesh operation applied to a profile group (drawing an edge on it, erasing an
  edge, and later push/pull of a side face) converts it to a mesh group in the same
  commit: the profile is dropped and the derived mesh becomes the truth. This is one-way,
  so there is never more than one source of truth at a time.
- Dimension anchors stay corner-index based in memory for now; for a migrated or profile
  group, index = vertex id - 1. Switching anchors to `{"object", "vertex": id}` is a
  follow-up, done together with the end of profile groups in Phase 2.

The table above is the Phase 2 end state, when profile groups are retired and every
operation is a mesh operation.

Saving writes version 3, which Pullform 0.2.x cannot open (the same policy as version 2).
Every future version adds one migration and a test that opens a file of every previous
version; the v1 and v2 test files must open and render the same after migration.

## File format version 3

Only the object entry changes; `format`, `generator`, `view`, `displayUnit`, `nextId` and
`dimensions` keep their meaning.

```json
{
  "id": "obj-1", "type": "group", "kind": "mesh", "name": "Box 1",
  "transform": { "translation": [0,0,0], "rotation": [0,0,0,1] },
  "mesh": {
    "vertices": [[1, 0,0,0], [2, 2,0,0], [3, 2,1,0], [4, 0,1,0]],
    "edges":    [[1, 1,2], [2, 2,3], [3, 3,4], [4, 4,1]],
    "faces":    [{ "id": 1, "loops": [[1, 2, 3, 4]] }],
    "curves":   [],
    "next":     { "vertex": 5, "edge": 5, "face": 2, "curve": 1 }
  }
}
```

- `kind`: `"mesh"` (the mesh is the truth) or, during the Phase 1 transition, `"profile"`
  (the entry also carries today's `profile` and optional `extrusion`, and `mesh` is the
  derived copy, rebuilt and checked on load).
- `vertices`: `[id, x, y, z]`. `edges`: `[id, v0, v1]`, with an optional fourth element
  written only when needed, an object such as `{"soft": true, "smooth": true, "curve": 3}`,
  so typical files stay compact.
- `faces[].loops`: the first loop is the outer boundary, the rest are holes. Each loop is
  a list of signed edge ids: `+e` walks the edge from `v0` to `v1`, `-e` walks it
  reversed. The face plane is not stored; it is re-fitted on load and validated.
- `curves`: `{"id", "kind": "arc"|"circle", "center", "normal", "xAxis", "radius",
  "start", "sweep", "segments", "edges": [...]}`.
- Ids are positive integers unique per kind within the mesh and below the matching `next`.
- Load validation (in `validateDocument`, the same path used for every commit) checks:
  ids unique and in range; every edge references existing distinct vertices with length at
  least the weld distance; no two vertices closer than the weld distance; every loop is
  closed and consecutive uses share a vertex; every face's vertices lie on its fitted
  plane within the planarity tolerance; the outer loop has positive area; holes lie inside
  the outer loop and do not overlap; no edge crosses another edge of the same mesh except
  at a shared vertex; curve edges exist, are in order and still match the arc within the
  weld distance (a curve that fails this is dropped with a warning, not a load failure).
- Size: about 40 to 60 bytes of JSON per vertex and per edge. A 100k-edge model is a few
  MB, well inside the 50 MB read limit.

## Operations

All operations are pure functions over an immutable mesh that return a new mesh or an
`Error`, like today's `operations.*`, and validation runs on the result before commit.

- **addEdge(a, b)**: weld `a` and `b` to existing vertices within the weld distance;
  split any existing edge that `a` or `b` lies on; split this edge and any edges it crosses
  at their intersection points (coplanar crossings only; skew lines never intersect);
  collinear overlap merges into shared edges. Then, for each new edge, look for the
  smallest closed planar cycle through it on each side and **create a face** if that cycle
  bounds no face yet. If the new loop lies inside an existing coplanar face, it becomes a
  hole in that face plus a new inner face. If the new edge runs across a face from boundary
  to boundary, the face is **split** in two.
- **eraseEdge(e)**: if the two faces that use it are coplanar (normals agree within the
  angle tolerance and all vertices are within the planarity distance), **merge** them into
  one face (the larger keeps its id); otherwise remove the faces that use it. Remove
  vertices left with no edges.
- **pushPull(face, distance)**: offset the face along its normal, creating side faces from
  its loops; when neighbouring faces are perpendicular (prism walls), move the face and
  stretch the walls instead of creating new ones (SketchUp's behaviour, and what today's
  height edit does). Negative distance through the solid cuts an opening, later.
- **transformVertices**: maps a set of vertices by an affine map: the general case behind
  scale, mirroring and the move, rotate and scale of vertices, edges and faces. Faces that
  would bend fold along soft edges (above); a mirror reverses the loops of the faces that
  move whole.
- **validate(mesh)**: the load checks above, also usable in tests and as a debug assert
  after every operation.

The 2D work (point in polygon, segment intersection, cycle finding on the face plane) uses
robust orientation predicates (Shewchuk-style adaptive `orient2d`, a few hundred lines,
public domain) so that results do not flip under rounding. Points are projected onto the
face plane's 2D frame before these tests.

## Curves: true arcs plus segments

A circle or arc is drawn as `segments` straight edges (default 24 for a full circle,
proportional for arcs) and a `Curve` record that keeps the exact definition.

- Topology, faces, push/pull and rendering see only the segments. A cylinder is a prism
  over a 24-gon, with the vertical edges marked **soft** and **smooth** so it draws as a
  smooth surface with a silhouette.
- The arc record gives exact snapping (centre, quadrants), exact radius dimensions and a
  later "change segment count" edit (rebuild the edges, keep the face).
- Selecting one segment of a curve selects the whole curve, as in SketchUp.
- If an operation splits a curve edge, the curve splits into two curves at that vertex.
  If an operation moves curve vertices off the arc (for example a non-uniform scale), the
  record no longer matches and validation drops it: the segments remain as ordinary edges.
  Uniform scale and rigid transforms update the record.
- Pushing/pulling a face bounded by a curve produces soft/smooth side edges and a curve
  record on the new cap loop.

The curve record was in the version 3 schema from the start, so the Arc tools (Phase 2)
needed no format change: `Mesh::addCurve` draws an arc's segments with `addEdge` and then
records the curve over the chain of edges they became.

## Precision and tolerance merging

Lengths stay `double` meters. Add to `core/document/tolerance.hpp`:

| Constant | Value | Use |
| --- | --- | --- |
| `kWeldDistance` | `1e-6` m (1 um) | two points are the same vertex; a point is on an edge; minimum edge length |
| `kPlanarity` | `1e-6` m | maximum distance of a face vertex from its plane |
| `kCoplanarCos` | `1 - 1e-12` | `dot(n1, n2)` above this (about 1.4e-6 rad) counts as coplanar for merging, together with `kPlanarity` |

Why 1 um and not today's 1 nm `kLengthEpsilon`: after a rotation or two and a
plane fit, coordinates of a 10 m model carry errors around 1e-15 relative, so absolute
errors are far below 1 nm; but intersection points, arc segment endpoints and typed values
converted from inches routinely differ by 1e-10 to 1e-8. Welding at 1 nm would leave
hairline gaps that break face creation, which is the failure users notice most. 1 um is
still 100 times smaller than the smallest dimension we accept (0.1 mm, `kMinDimension`), so
two distinct points the user can draw never weld by accident. `kLengthEpsilon` stays for
exact-equality checks on typed values.

Tolerances are absolute and apply in group-local coordinates, where coordinates are at
most 10 km (`kMaxCoordinate`). At 10 km a double resolves about 2e-12 m, so 1 um is
always representable with ample margin.

Welding uses a uniform spatial hash with cells of a few weld distances, built per
operation from the mesh's vertices, so it is near linear. A new point snaps to the
existing vertex, never the other way round, so existing geometry does not drift.

## Undo snapshot cost

Today a snapshot costs about 16 bytes per object plus the changed object, because objects
are immutable shared pointers. The same scheme carries over one level down:

- A snapshot shares every unchanged `Group`, and a group shares its `Mesh`. An edit in one
  group copies that group's mesh only. Moving, rotating or duplicating a group copies no
  mesh at all.
- Rough element sizes: vertex 24 B, edge about 16 B, edge use 8 B, face about 80 B plus
  its loops, plus adjacency. A mesh with 10k edges is on the order of 1 MB. 200 undo
  entries of edits to that one group would be about 200 MB, which is too much for a
  worst case.

So:

1. Phase 1 stores elements in plain vectors **behind accessors**, so storage can change
   without touching callers.
2. History gains a **memory budget** (for example 256 MB) next to the 200-entry limit,
   dropping the oldest entries first; the budget counts unshared meshes only.
3. When a benchmark of edits in a 100k-edge single mesh shows the copy cost matters (time
   per edit over about 5 ms, or history over budget within a normal session), switch the
   element arrays to **chunked copy-on-write** (fixed chunks of, say, 1024 elements, each a
   shared pointer; an edit copies only the chunks it touches). Snapshots then cost a few KB
   per edit regardless of mesh size. Adjacency is derived and rebuilt lazily per mesh, so it
   is not copied into history.

Add `tests/bench/mesh_edit_bench.cpp` (built, not run by CTest, like the hover benchmark)
with step 4 to measure this.

## Solid booleans

Item 20 (Solid tools) was scheduled after this document was accepted, and the project
owner chose then to keep the booleans in our own code rather than add Manifold, so the
core stays one small, dependency-free mesh. The code is `core/geometry/solid_boolean.*`
(mesh level) and `core/commands/solid_tools.*` (the six tools on document objects).

- Booleans apply only to meshes that pass the closed-solid query, `mesh::solidProblem`:
  faces, every edge used by exactly two faces walking it in opposite directions, no loose
  edges, outward faces (positive volume). `checkSolid` on `mesh::solidMeshData` gives the
  same answer on triangles.
- Both meshes are brought into the first object's frame. Every pair of faces whose boxes
  overlap is intersected: crossing planes give the segment of their common line that lies
  in both polygons; coplanar faces imprint each other's edges.
- Each face is split by its cut segments into regions (a planar arrangement: segments are
  split at crossings and at points on them, spurs are pruned, cycles are traced, clockwise
  cycles become holes of the smallest region around them). Points are welded with
  `kWeldDistance` in one pool, so a point shared by two faces is one vertex.
- A region is classified by a point well inside it: on a coplanar face of the other solid
  (facing the same or the opposite way), or inside or outside the other solid by its
  generalised winding number, which needs no ray and has no degenerate directions. Union
  keeps what is outside the other solid, Intersect what is inside, Subtract keeps a's
  outside and b's inside turned around; of two coincident same-facing regions only a's
  is kept.
- The kept regions are welded into a fresh mesh, with T-junctions filled in so neighbours
  share their edges. Then coplanar faces are merged across edges that are not a split
  the user drew (an edge between two coplanar faces in its source solid is kept), straight
  runs left with a vertex in the middle are joined, soft and smooth flags follow from the
  source edges, and the result is validated and checked to be a closed solid.
- Outer Shell is a union followed by dropping every shell that lies inside another.
- Known limits: the result has fresh element ids and no curve records (a drilled hole is
  its segments, not a circle); faces that would touch themselves at a single corner, or two
  solids touching along only an edge, are refused rather than represented; the face pair
  search and the T-junction pass are quadratic in the worst case, fine for the solids
  drawn today but a candidate for a spatial index with large models.
- Intersecting faces with each other (SketchUp's "Intersect Faces", which also works on
  open geometry) is item 19 and stays a separate operation on one mesh (`addEdge` with the
  intersection segments). It can share the face-pair intersection above.

## Risks and how we handle them

- **Robustness of our own operations.** Mitigated by robust 2D predicates, one weld
  tolerance used everywhere, validation after every operation in debug builds and tests,
  and a fuzz test that draws random edges in a plane and checks the mesh stays valid and
  the faces tile the area exactly.
- **Scope creep.** Phase 1 does only planar faces, holes, auto-face, split, merge and
  migration. No auto-fold, no booleans, no arc tool.
- **Performance of face creation on large meshes.** Cycle search is local to the new
  edges' vertices on one plane, so cost scales with the size of the faces touched, not the
  model.

## Acceptance checks this design must pass (step 4 and 5)

- Four edges drawn in a loop make exactly one face; a line across it splits it into two;
  erasing that line merges them back into one face with the original id.
- A rectangle drawn inside a face makes a hole plus an inner face; pushing the inner face
  down through a box later makes an opening.
- Two boxes drawn touching remain two groups with no shared vertices.
- Every version 1 and version 2 test file opens, renders the same, and keeps its
  dimensions attached to the same points.
- Undo and redo of every operation restore element ids exactly.
