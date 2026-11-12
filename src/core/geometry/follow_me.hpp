#pragma once

#include "core/document/result.hpp"
#include "core/geometry/mesh.hpp"

#include <optional>
#include <vector>

// Follow Me: sweeping a profile face along a path of edges (docs/GEOMETRY.md).

namespace pf::mesh {

/// A sweep path: its points in order. A closed path does not repeat its first
/// point at the end.
struct SweepPath {
    std::vector<Vec3> points;
    bool closed = false;
    /// Per point: the path bends smoothly there (the point is inside one arc or
    /// circle), so the edges the sweep makes across it are softened.
    std::vector<bool> smooth;
};

/// Orders `edges` of `mesh` into one chain from end to end (or once around a
/// loop). Fails when the edges branch, are not connected, or there are none.
Expected<SweepPath> chainPath(const Mesh& mesh, const std::vector<EdgeId>& edges);

/// The edges a click on `edge` picks as a path: the whole arc or circle it is
/// part of, else the run of edges it continues through vertices that have
/// exactly two edges (a polyline, or a loop of loose edges).
std::vector<EdgeId> pathThrough(const Mesh& mesh, EdgeId edge);

struct FollowMeResult {
    std::vector<FaceId> createdFaces; ///< every new face (sides and end cap)
    std::optional<FaceId> endCap;     ///< the far end of an open sweep
};

/// Sweeps face `profile` along `path` (in the mesh's own coordinates).
///
/// Each profile point travels parallel to each path segment and turns at each
/// path point on the mitre plane (the plane halfway between the segments), so
/// the cross-section keeps its shape on straight runs and every side face is
/// planar. The profile need not touch the path:
///  - an open path starts at its end nearer the profile; the profile face stays
///    as the start cap (turned to face away) and the far end, cut square to the
///    last segment, is a new cap;
///  - a closed path starts at its point nearest the profile and goes all the
///    way round; the profile face is removed when it stood on its own (its edges
///    go too unless the sweep uses them).
/// Profile points on the sweep's axis (a lathe profile touching the axis of a
/// circle) stay put, so their side faces become triangles, and a profile edge
/// whose triangles all lie in one plane becomes one face (a vase's base). A new
/// face with the same corners as an existing face reuses it. Edges along a
/// profile curve and across a path curve are made soft and smooth.
///
/// Fails, with the mesh possibly changed (work on a copy), when the face is
/// missing, the path has no segment, the profile lies along the path, the path
/// turns back on itself, or a closed path twists so the sweep does not meet.
Expected<FollowMeResult> followMe(Mesh& mesh, FaceId profile, const SweepPath& path);

} // namespace pf::mesh
