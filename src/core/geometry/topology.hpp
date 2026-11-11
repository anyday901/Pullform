#pragma once

#include "core/document/document.hpp"
#include "core/document/element_ref.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace pf {

/// The vertices, edges and faces of one object as the viewport sees them, in
/// world space, with the adjacency needed for picking, profile-edge detection
/// and double/triple-click expansion. Inside the structure elements refer to
/// each other by index; each element also carries its stable model id (the
/// mesh's VertexId/EdgeId/FaceId value), which is what selections store.
///
/// This is a derived view, like MeshData: it is rebuilt from the model and
/// never stored. It is built from the object's edge/face mesh
/// (mesh::objectMesh), in world space, with the mesh's element ids.
struct ElementTopology {
    struct Vertex {
        std::uint32_t id = 0;
        Vec3 position{0.0};
        std::vector<std::uint32_t> edges; ///< edges using this vertex
        bool hidden = false;              ///< every edge at it is hidden: not picked unless hidden geometry is shown
    };
    struct Edge {
        std::uint32_t id = 0;
        std::uint32_t a = 0;              ///< vertex index of one end
        std::uint32_t b = 0;              ///< vertex index of the other end
        std::vector<std::uint32_t> faces; ///< faces bordered by this edge (0, 1 or 2 for a manifold)
        bool soft = false;                ///< not drawn and not picked: a soft or hidden edge, unless hidden geometry is shown
        bool ghost = false;               ///< a soft or hidden edge shown because hidden geometry is shown: drawn faint, picked
    };
    struct Face {
        std::uint32_t id = 0;
        std::optional<FaceId> logical;    ///< semantic face of a profile object (push/pull, properties)
        Vec3 normal{0.0};
        std::vector<std::uint32_t> loop;  ///< outer loop, vertex indices, counter-clockwise from +normal
        std::vector<std::vector<std::uint32_t>> holes; ///< hole loops, vertex indices
        std::vector<std::uint32_t> edges; ///< bounding edges in loop order
        bool twoSided = false;            ///< not part of a closed shell: visible and pickable from both sides
        bool hidden = false;              ///< a hidden face: it has no triangles (not drawn or picked) unless hidden geometry is shown
    };
    struct Triangle {
        std::uint32_t face = 0;
        std::array<std::uint32_t, 3> v{0, 0, 0};
    };

    std::vector<Vertex> vertices;
    std::vector<Edge> edges;
    std::vector<Face> faces;
    std::vector<Triangle> triangles; ///< front-side triangulation of every face
    Bounds bounds;                   ///< of every vertex, for culling picks

    [[nodiscard]] bool empty() const { return vertices.empty(); }
    /// Index of the face whose semantic id is `logical`, if any.
    [[nodiscard]] std::optional<std::uint32_t> faceFor(const FaceId& logical) const;
    /// Index of the element of `kind` whose stable id is `id`, if any.
    [[nodiscard]] std::optional<std::uint32_t> indexOf(ElementKind kind, std::uint32_t id) const;
    /// Stable id of the element of `kind` at `index`.
    [[nodiscard]] std::uint32_t idAt(ElementKind kind, std::uint32_t index) const;
};

/// Builds the topology of a mesh placed by `object`'s transform. Element ids
/// are the mesh's ids. A face is two-sided when any of its edges does not
/// border exactly two faces (a sheet, or a face with an open boundary).
/// Soft and hidden edges and hidden faces are kept (adjacency stays whole) but
/// are not drawn or picked; with `showHidden` (View > Hidden Geometry) they are
/// drawn faint and picked like the rest, so they can be selected and unhidden.
ElementTopology buildTopology(const mesh::Mesh& mesh, const ProfileObject& object, bool showHidden = false);

/// Builds the topology from logical face polygons. Corners that coincide
/// (within `tolerance` meters) are merged into one vertex, and every
/// undirected vertex pair along a loop becomes one shared edge. Ids are
/// indices + 1, in order of first appearance.
ElementTopology buildTopology(const std::vector<FacePolygon>& faces, double tolerance = 1e-9);

/// Topology of a document object: of its mesh (mesh::objectMesh).
ElementTopology buildTopology(const ProfileObject& object, bool showHidden = false);

/// Edges that outline the shape from the eye: border edges (one face, as on a
/// sheet) and silhouette edges, where one neighbor faces the eye and the other
/// faces away. These are the edges drawn thicker with the "profiles" style.
std::vector<std::uint32_t> profileEdges(const ElementTopology& topology, const Vec3& eye);

/// Expansions take and return stable ids.
/// Double-click expansion. A face gives the face and its bounding edges; an
/// edge gives the edge and the faces it borders; a vertex gives its edges.
std::vector<ElementRef> expandBounded(ObjectId object, const ElementTopology& topology, ElementKind kind, std::uint32_t id);

/// Triple-click expansion: every vertex-connected edge and face reachable from
/// the element (all of it, for one of today's single-solid objects).
std::vector<ElementRef> expandConnected(ObjectId object, const ElementTopology& topology, ElementKind kind, std::uint32_t id);

} // namespace pf
