#pragma once

#include "core/document/document.hpp"

#include <utility>
#include <vector>

// Face-face intersection (Intersect Faces, roadmap item 19). Pure geometry on
// planar polygons with holes; the operation that adds the result to meshes is
// pf::intersectFaces (core/commands/operations.hpp).

namespace pf::mesh {

class Mesh;
struct FaceId;

/// A planar face in some common frame (usually world space): its loops (outer
/// first, then holes; orientation does not matter here) and its unit normal.
struct PlanarFace {
    std::vector<std::vector<Vec3>> loops;
    Vec3 normal{0.0, 0.0, 1.0};
};

using Segment = std::pair<Vec3, Vec3>;

/// The segments where two planar faces cross: the parts of the line where
/// their planes meet that lie inside (or on the boundary of) both faces.
/// Parallel and coplanar faces give nothing, and so do parts that lie on the
/// boundary of both faces (an edge they already share). Segments shorter than
/// the weld distance are dropped. Ends are in the faces' frame.
std::vector<Segment> faceIntersection(const PlanarFace& a, const PlanarFace& b);

/// Face `face` of `mesh` as a PlanarFace in the mesh's own frame.
PlanarFace planarFace(const Mesh& mesh, FaceId face);

} // namespace pf::mesh
