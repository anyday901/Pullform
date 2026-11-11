#pragma once

#include "core/geometry/pick.hpp"
#include "core/geometry/snap.hpp"

#include <optional>
#include <span>
#include <vector>

// The inference engine: where a pointer ray should land, given the model's
// snap features, the point a drawing gesture started from, and the points and
// edges the user has recently pointed at. Pure geometry (no tool state), so
// every tool reaches it through the shared PointSnapper and tests can drive it
// directly.
//
// In priority order it finds:
//  1. Endpoint, Midpoint, Center: a point feature near the pointer.
//  2. Intersection: two edges, or an edge and an inference line, crossing
//     near the pointer.
//  3. On Edge: the point of an edge nearest the pointer.
//  4. A line inference: from the start point along X, Y or Z (Axis); parallel
//     or perpendicular to a reference edge (the "magenta" guides); or from a
//     remembered point along an axis (From Point). Two lines crossing near the
//     pointer give their crossing point.
// When nothing applies the caller falls back to the face under the pointer
// (On Face), the drawing plane, or the ground grid.
//
// A locked constraint (Shift) keeps the result on its line or plane: point
// features are projected onto it, with a guide from the feature.

namespace pf {

/// The plane a tool draws on (the ground for the rectangle tool).
struct InferencePlane {
    Vec3 point{0.0};
    Vec3 normal{0.0, 0.0, 1.0}; ///< unit
};

struct InferenceQuery {
    Ray ray;
    double radius = 0.0;                 ///< angular snap radius (radians); 0 turns inference off
    std::optional<double> occluder;      ///< distance to the face under the pointer (hides what is behind it)
    std::optional<ObjectId> exclude;     ///< object being edited (its features are skipped)
    std::optional<Vec3> from;            ///< drawing start point: enables Axis, Parallel and Perpendicular lines
    std::optional<InferencePlane> plane; ///< keep results on this plane (lines not in it are skipped)
    std::span<const Vec3> references;    ///< remembered points (From Point lines along each axis)
    std::span<const SnapEdge> referenceEdges; ///< remembered edges (parallel/perpendicular references)
    std::optional<SnapConstraint> lock;  ///< Shift-locked constraint
};

/// The strongest inference near the pointer, or nothing (the caller then uses
/// the face, the plane or the grid). Parallel/perpendicular references are the
/// query's referenceEdges plus every edge through `from`.
std::optional<SnapResult> inferPoint(const SnapIndex& index, const InferenceQuery& query);

/// The point on a locked constraint for `ray`: the closest point of a line,
/// or where the ray meets a plane. Nothing when the ray is parallel to it.
std::optional<Vec3> pointOnConstraint(const SnapConstraint& constraint, const Ray& ray);

/// Closest approach of two lines (unit directions). Nothing when parallel.
/// Returns the parameters along each line.
std::optional<std::pair<double, double>> closestLineParameters(const Vec3& o1, const Vec3& d1, const Vec3& o2, const Vec3& d2);

} // namespace pf
