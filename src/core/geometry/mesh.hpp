#pragma once

#include "core/document/document.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Edge/face mesh ("sticky" geometry). See docs/GEOMETRY.md for the design and
// docs/ARCHITECTURE.md for how it fits the document.
//
// An index-based edge-use mesh, not a strict half-edge structure: an edge may
// have no face (a loose line) or more than two faces. Positions are local to
// the owning object (group); world = localToWorld(object, position).
//
// Every element has a stable id that is never reused within one mesh; the
// mesh remembers the next id of each kind, and files save them, so a selection
// or a dimension anchor can name (object id, element id).

namespace pf::mesh {

struct VertexId {
    std::uint32_t value = 0;
    friend auto operator<=>(const VertexId&, const VertexId&) = default;
};
struct EdgeId {
    std::uint32_t value = 0;
    friend auto operator<=>(const EdgeId&, const EdgeId&) = default;
};
struct FaceId {
    std::uint32_t value = 0;
    friend auto operator<=>(const FaceId&, const FaceId&) = default;
};
struct CurveId {
    std::uint32_t value = 0;
    friend auto operator<=>(const CurveId&, const CurveId&) = default;
};

struct Vertex {
    Vec3 position{0.0};
    friend bool operator==(const Vertex&, const Vertex&) = default;
};

struct Edge {
    VertexId v0;
    VertexId v1;
    std::optional<CurveId> curve; ///< the arc or circle this segment belongs to
    bool soft = false;            ///< hidden unless "show hidden geometry"
    bool smooth = false;          ///< shading is smoothed across it
    friend bool operator==(const Edge&, const Edge&) = default;
};

/// One use of an edge by a face loop. Walking the loop goes v0 -> v1, or
/// v1 -> v0 when `reversed`.
struct EdgeUse {
    EdgeId edge;
    bool reversed = false;
    friend bool operator==(const EdgeUse&, const EdgeUse&) = default;
};
using Loop = std::vector<EdgeUse>;

/// A planar face. loops[0] is the outer boundary, counter-clockwise seen from
/// the side `normal` points to; any further loops are holes, clockwise.
struct Face {
    std::vector<Loop> loops;
    Vec3 normal{0.0, 0.0, 1.0}; ///< unit; cached plane (with any loop vertex)
    friend bool operator==(const Face&, const Face&) = default;
};

enum class CurveKind { Arc, Circle };

/// A true arc or circle, kept beside the segments that approximate it. The
/// topology only ever sees the segment edges; a curve whose edges no longer
/// form a chain is dropped by validation-on-edit (Mesh::pruneCurves).
struct Curve {
    CurveKind kind = CurveKind::Arc;
    Vec3 center{0.0};
    Vec3 normal{0.0, 0.0, 1.0};
    Vec3 xAxis{1.0, 0.0, 0.0};
    double radius = 0.0;
    double start = 0.0; ///< radians from xAxis about normal
    double sweep = 0.0; ///< radians; 2*pi for a circle
    int segments = 0;
    std::vector<EdgeId> edges; ///< in order along the curve
    friend bool operator==(const Curve&, const Curve&) = default;
};

/// What one addEdge call did, so a tool can select or report it.
struct AddEdgeResult {
    std::vector<EdgeId> edges;        ///< the drawn edge, split into its pieces, in order from a to b
    std::vector<FaceId> createdFaces; ///< new faces (made by closing a loop or by a split)
    std::vector<FaceId> removedFaces; ///< faces that were split (replaced by createdFaces)
};

struct EraseEdgeResult {
    std::vector<FaceId> removedFaces;
    std::optional<FaceId> mergedFace; ///< set when two coplanar faces became this one
};

/// What one pushPull call did.
struct PushPullResult {
    FaceId face;                      ///< the pushed face where it ended up (the cap of an extrusion)
    std::vector<FaceId> createdFaces; ///< new side faces and the cap
    bool stretched = false;           ///< the face moved and its walls stretched (no new faces)
};

class Mesh {
public:
    // ---- Reading ----------------------------------------------------------------
    [[nodiscard]] const Vertex* vertex(VertexId id) const;
    [[nodiscard]] const Edge* edge(EdgeId id) const;
    [[nodiscard]] const Face* face(FaceId id) const;
    [[nodiscard]] const Curve* curve(CurveId id) const;

    [[nodiscard]] const std::map<std::uint32_t, Vertex>& vertices() const { return vertices_; }
    [[nodiscard]] const std::map<std::uint32_t, Edge>& edges() const { return edges_; }
    [[nodiscard]] const std::map<std::uint32_t, Face>& faces() const { return faces_; }
    [[nodiscard]] const std::map<std::uint32_t, Curve>& curves() const { return curves_; }
    [[nodiscard]] bool empty() const { return vertices_.empty(); }

    /// Ids the next element of each kind will get.
    [[nodiscard]] std::uint32_t nextVertexId() const { return nextVertex_; }
    [[nodiscard]] std::uint32_t nextEdgeId() const { return nextEdge_; }
    [[nodiscard]] std::uint32_t nextFaceId() const { return nextFace_; }
    [[nodiscard]] std::uint32_t nextCurveId() const { return nextCurve_; }

    /// Start vertex of a use (v0, or v1 when reversed) and its end.
    [[nodiscard]] VertexId useStart(const EdgeUse& use) const;
    [[nodiscard]] VertexId useEnd(const EdgeUse& use) const;
    /// The loop's vertices in walking order (each once; the loop closes back to the first).
    [[nodiscard]] std::vector<VertexId> loopVertices(const Loop& loop) const;
    [[nodiscard]] std::vector<Vec3> loopPositions(const Loop& loop) const;

    [[nodiscard]] std::optional<EdgeId> findEdge(VertexId a, VertexId b) const;
    [[nodiscard]] std::vector<EdgeId> edgesOfVertex(VertexId id) const;
    [[nodiscard]] std::vector<EdgeId> edgesOfFace(FaceId id) const;
    [[nodiscard]] std::vector<FaceId> facesOfEdge(EdgeId id) const;
    [[nodiscard]] std::optional<VertexId> findVertex(const Vec3& position) const; ///< within kWeldDistance

    // ---- Raw construction (file reading, migration, tests) -------------------------
    // No topology inference and no validation: call validateMesh afterwards.
    void insertVertex(VertexId id, const Vec3& position);
    void insertEdge(EdgeId id, const Edge& edge);
    void insertFace(FaceId id, const Face& face);
    void insertCurve(CurveId id, const Curve& curve);
    VertexId appendVertex(const Vec3& position);
    EdgeId appendEdge(VertexId a, VertexId b);
    /// Builds a face from vertex loops (outer CCW about `normal`, holes CW); every
    /// consecutive pair must already have an edge. Returns nothing if one is missing.
    std::optional<FaceId> appendFace(const std::vector<std::vector<VertexId>>& loops, const Vec3& normal);
    /// Raises the next ids (never lowers them). Used when reading saved next ids.
    void reserveIds(std::uint32_t vertex, std::uint32_t edge, std::uint32_t face, std::uint32_t curve);

    // ---- Modeling ---------------------------------------------------------------------

    /// Returns the vertex at `position` (within kWeldDistance), splitting an edge
    /// it lies on, or a new loose vertex.
    VertexId weldVertex(const Vec3& position);

    /// Draws a straight edge from a to b. Endpoints weld to existing vertices,
    /// edges touched or crossed are split at the contact, and the new edge is split
    /// where it passes existing vertices. Then each new piece either splits the
    /// face it crosses (both ends on that face's boundary) or, when it closes a
    /// planar loop, makes a new face. A closed loop drawn inside a face becomes a
    /// hole in it plus an inner face. Returns nothing when a and b weld together.
    std::optional<AddEdgeResult> addEdge(const Vec3& a, const Vec3& b);

    /// Draws a curve as `curve.segments` straight edges through `points`
    /// (segments + 1 positions along it, ends included) with addEdge, then
    /// records `curve` (its edges filled in, in order) and tags those edges.
    /// Faces close and split exactly as for the same edges drawn one by one.
    /// Returns the drawn edges in order and the faces made, or nothing when
    /// the points do not give at least one edge.
    std::optional<AddEdgeResult> addCurve(Curve curve, const std::vector<Vec3>& points, CurveId* id = nullptr);

    /// Erases an edge. Two coplanar faces on either side merge into one (and a
    /// coplanar inner face merges into the face it is a hole of); any other face
    /// that used the edge is removed. Vertices left without edges are removed.
    std::optional<EraseEdgeResult> eraseEdge(EdgeId id);

    /// Pushes or pulls a face by `distance` along its normal (negative goes
    /// back). Two cases are supported for now:
    ///  - a free face (no edge or vertex shared with other geometry) is
    ///    extruded: a cap at the offset, one side face per loop edge (holes make
    ///    inner walls), and the face stays as the base, turned to face away;
    ///  - the end face of a prism (every other edge at its vertices runs along
    ///    the normal) moves, and its walls stretch, while they keep a length.
    /// Returns nothing, with the mesh unchanged, in any other case.
    std::optional<PushPullResult> pushPull(FaceId id, double distance);

    /// Removes a face only (its edges stay).
    bool eraseFace(FaceId id);

    /// Drops curves whose edges are missing or no longer form a chain.
    void pruneCurves();

    /// Moves every vertex by `f` (component-wise scale about the origin) and
    /// recomputes face normals. Factors must be non-zero.
    void scale(const Vec3& factors);
    /// Moves every vertex (and curve) by `offset`.
    void translate(const Vec3& offset);

    friend bool operator==(const Mesh&, const Mesh&) = default;

private:
    void splitEdge(EdgeId id, VertexId at);
    std::optional<EdgeId> connect(VertexId a, VertexId b, AddEdgeResult& result);
    void faceEdge(EdgeId id, AddEdgeResult& result);
    std::optional<std::vector<EdgeUse>> traceLeft(const EdgeUse& start, const Vec3& normal, const Vec3& origin,
                                                  const std::vector<EdgeId>& allowed) const;
    Loop makeLoop(const std::vector<VertexId>& vertices) const;

    std::map<std::uint32_t, Vertex> vertices_;
    std::map<std::uint32_t, Edge> edges_;
    std::map<std::uint32_t, Face> faces_;
    std::map<std::uint32_t, Curve> curves_;
    std::uint32_t nextVertex_ = 1;
    std::uint32_t nextEdge_ = 1;
    std::uint32_t nextFace_ = 1;
    std::uint32_t nextCurve_ = 1;
};

using MeshPtr = std::shared_ptr<const Mesh>;

/// Checks a mesh: finite, in-range positions; edges between two distinct live
/// vertices (at least kWeldDistance apart) and no duplicate edges; faces with
/// closed, connected loops of at least three live edges, planar within
/// kPlanarity, outer loop counter-clockwise with positive area, holes clockwise
/// and inside the outer loop; curves referring to live edges. `path` prefixes messages.
std::optional<std::string> validateMesh(const Mesh& mesh, std::string_view path);

/// Signed area of a planar loop about `normal` (positive when counter-clockwise).
double signedArea(const std::vector<Vec3>& loop, const Vec3& normal);
/// Unit normal of a planar loop (Newell's method), or zero when degenerate.
Vec3 loopNormal(const std::vector<Vec3>& loop);

/// Triangulates one face (ear clipping, holes bridged into the outer loop).
/// Triangles wind counter-clockwise about the face normal.
std::vector<std::array<VertexId, 3>> triangulateFace(const Mesh& mesh, FaceId face);

/// Every face triangulated: indices into `positions` (one per mesh vertex, in
/// id order; `vertexIds[i]` is the id of positions[i]) and the face of each triangle.
struct Triangulation {
    std::vector<Vec3> positions;
    std::vector<VertexId> vertexIds;
    std::vector<std::uint32_t> indices;
    std::vector<FaceId> triangleFaces;
};
Triangulation triangulate(const Mesh& mesh);

/// Everything connected to an element through shared vertices (SketchUp's
/// triple-click): the edges and faces of the connected component.
struct Connected {
    std::vector<VertexId> vertices;
    std::vector<EdgeId> edges;
    std::vector<FaceId> faces;
};
Connected connectedElements(const Mesh& mesh, VertexId start);

// ---- Profile objects --------------------------------------------------------------

/// Mesh of a profile object, in object-local space, with deterministic ids:
/// vertex id = corner index + 1 (objectVertex numbering: base corners, then cap
/// corners); edge ids = base edges 1..n, cap edges n+1..2n, vertical edges
/// 2n+1..3n; face ids = Profile/Base 1, Cap 2, Side i 3 + i.
Mesh meshFromProfile(const ProfileObject& object);

/// Mesh face of a semantic face of a profile object, and back.
FaceId faceIdFor(pf::FaceId face);
std::optional<pf::FaceId> semanticFace(const ProfileObject& object, FaceId face);

/// The object's mesh: its stored mesh for a mesh group, or one built from the
/// profile (meshFromProfile) for a rectangle or box.
MeshPtr objectMesh(const ProfileObject& object);

} // namespace pf::mesh
