#pragma once

#include "core/document/result.hpp"
#include "core/geometry/mesh.hpp"

#include <optional>
#include <vector>

// Offset (F): a face's boundary moved in or out by a distance, in the face's
// plane. Every loop of the face moves to its left (into the face) by the
// distance, so a positive distance shrinks the outer loop and grows the holes
// and a negative one does the opposite. See docs/ARCHITECTURE.md.

namespace pf::mesh {

/// One offset loop, in the same space as the mesh (object-local).
struct OffsetLoop {
    std::size_t source = 0;  ///< index of the face loop it comes from (0 = outer)
    std::vector<Vec3> points; ///< closed (the last point joins the first), same winding as the source
    /// Index of the source loop's edge that each offset edge (points[i] ->
    /// points[i + 1]) runs along. Edges that collapse on the way are dropped,
    /// so this skips their indices.
    std::vector<std::size_t> sourceEdges;
};

/// The offset loops of face `face` at `distance` (positive goes into the
/// face). Corners are mitred: each corner moves to where the two neighbouring
/// edges, both moved by the distance, meet. Edges that shrink to nothing on the
/// way (a short side of a concave corner, a small chamfer) are dropped and
/// their neighbours meet instead. A hole that closes up on an outward offset is
/// left out. Fails, with a message for the user, when the distance is zero or
/// not finite, when the outer loop closes up, or when the offset loops would
/// cross themselves or each other (the distance is too large for the shape).
Expected<std::vector<OffsetLoop>> offsetLoops(const Mesh& mesh, FaceId face, double distance);

/// Distance from `point` (object-local, projected onto the face's plane) to
/// the nearest edge of the face: positive inside the face, negative outside it
/// (including inside a hole). The Offset tool measures the pointer with it.
/// Zero when the face does not exist.
double signedBoundaryDistance(const Mesh& mesh, FaceId face, const Vec3& point);

/// What offsetFace did.
struct OffsetResult {
    /// The face between the face's outer loop and its offset: the original
    /// face id for an inward offset, a new face for an outward one.
    std::optional<FaceId> ring;
    std::vector<FaceId> createdFaces;
    std::vector<EdgeId> createdEdges;
};

/// Draws the offset loops of `face` into the mesh (Mesh::addEdge, so they stick
/// to the geometry they meet). Inward, the face becomes a ring around a new
/// inner face, and each hole gets a ring of its own; outward, a ring face forms
/// around the face and around each shrunken hole, and a hole that closes up is
/// filled. Faces made in the face's
/// plane face the same way as it. Arcs, circles and polygons stay curves (with
/// the offset radius). On failure the mesh is unchanged.
Expected<OffsetResult> offsetFace(Mesh& mesh, FaceId face, double distance);

} // namespace pf::mesh
