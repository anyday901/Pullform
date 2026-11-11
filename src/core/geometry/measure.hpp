#pragma once

#include "core/document/document.hpp"
#include "core/geometry/pick.hpp"

#include <optional>
#include <vector>

// Measurement and dimension geometry: corner numbering, anchor resolution and
// the layout of a dimension line. Pure functions on the document, no UI.

namespace pf {

/// Number of corners of an object: one per boundary point on the base, and
/// as many again on the cap when extruded. Corner i < n is base point i;
/// corner n + i is the cap point above it.
int vertexCount(const ProfileObject& object);

/// World position of corner `index`, or nothing when it does not exist.
std::optional<Vec3> objectVertex(const ProfileObject& object, int index);

/// The object's real edges as corner index pairs: base loop, cap loop, then
/// the vertical edges (only the base loop for a planar profile).
std::vector<std::pair<int, int>> objectEdges(const ProfileObject& object);

/// World position of an anchor, or nothing when it refers to a missing object
/// or corner.
std::optional<Vec3> resolveAnchor(const Document& document, const DimensionAnchor& anchor);

/// Turns a picked world point into an anchor. A point on a corner or an edge
/// midpoint (within `tolerance` meters) of `preferred`, or else of any object,
/// is attached to it so the dimension follows the object; any other point is free.
DimensionAnchor anchorAt(const Document& document, const Vec3& point, std::optional<ObjectId> preferred = std::nullopt,
                         double tolerance = 1e-7);

/// True when the anchor refers to `id`.
bool anchorUses(const DimensionAnchor& anchor, ObjectId id);

/// A tape measurement between two world points.
struct Measurement {
    Vec3 delta{0.0}; ///< b - a
    double length = 0.0;
};
Measurement measureBetween(const Vec3& a, const Vec3& b);

/// Component of `v` perpendicular to the segment a-b. For a degenerate segment
/// the vector is returned unchanged.
Vec3 perpendicularTo(const Vec3& a, const Vec3& b, const Vec3& v);

/// Offset for a dimension of a-b from a pointer ray: the ray meets the plane
/// that contains a-b and faces the viewer best, and the offset is the part of
/// (hit - a) perpendicular to a-b. Nothing when the ray misses that plane.
std::optional<Vec3> offsetFromRay(const Vec3& a, const Vec3& b, const Ray& ray);

/// Direction used for a typed offset when the pointer has not given one: a
/// horizontal perpendicular for a sloped or level segment, +X for a vertical one.
Vec3 defaultOffsetDirection(const Vec3& a, const Vec3& b);

/// Where a dimension is drawn: anchors, the dimension line ends (anchors plus
/// offset) and the label point (the line's middle).
struct DimensionLayout {
    Vec3 a{0.0};
    Vec3 b{0.0};
    Vec3 lineA{0.0};
    Vec3 lineB{0.0};
    Vec3 label{0.0};
    double length = 0.0;
};
DimensionLayout layoutDimension(const Vec3& a, const Vec3& b, const Vec3& offset);
std::optional<DimensionLayout> layoutDimension(const Document& document, const Dimension& dimension);

} // namespace pf
