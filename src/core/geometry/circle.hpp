#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"

#include <optional>
#include <string>
#include <vector>

// Circles and regular polygons (docs/ARCHITECTURE.md, "Circles and polygons").
//
// Both are profile groups whose boundary is a regular polygon centred on the
// object's local origin: vertex i sits at radius r and angle start + 2*pi*i/n.
// A circle is approximated by its segments (the vertices lie on the circle);
// a polygon is the same shape with its own kind, so it keeps hard edges when
// extruded. The mesh keeps the true curve beside the segments.

namespace pf {

/// Fewest and most segments (sides) a circle or polygon may have.
inline constexpr int kMinSegments = 3;
inline constexpr int kMaxSegments = 999;
/// Default segment counts when a tool is first used.
inline constexpr int kDefaultCircleSegments = 24;
inline constexpr int kDefaultPolygonSides = 6;

/// A circle or polygon on a horizontal plane, ready for createCircle.
struct CirclePlacement {
    Vec3 center{0.0};  ///< world space
    double radius = 0.0;
    double start = 0.0; ///< radians from +X (counter-clockwise seen from +Z) to the first vertex
    int segments = kDefaultCircleSegments;
};

/// The radius, start angle and segment count of a regular boundary.
struct RegularShape {
    double radius = 0.0;
    double start = 0.0;
    int segments = 0;
};

/// Counter-clockwise (seen from +Z) regular boundary centred on the origin.
std::vector<Vec2> regularBoundary(double radius, int segments, double start);

/// Checks a segment count. Returns a user-facing message when it is out of range.
std::optional<std::string> checkSegments(int segments);

/// Checks radius and segment count together: the radius is a valid length and
/// every side is long enough to be an edge.
std::optional<std::string> checkCircle(double radius, int segments);

/// The shape of a boundary made by regularBoundary (within a small tolerance),
/// or an error saying why it is not one.
Expected<RegularShape> regularShape(const std::vector<Vec2>& boundary);

/// A placement from the centre and a point on the circle (its radius and the
/// direction of the first vertex). `segments` is checked.
Expected<CirclePlacement> circleFromPoints(const Vec3& center, const Vec3& onCircle, int segments);

/// A placement from the centre, a typed radius and the direction (radians
/// from +X) the first vertex points to.
Expected<CirclePlacement> circleFromRadius(const Vec3& center, double radius, double start, int segments);

/// Parses a typed segment count such as "24s" (also "24 s", "24S"). Returns
/// nothing when the text is not a segment count at all (so the caller can try
/// a length), or an error when it is one but out of range.
std::optional<Expected<int>> parseSegmentCount(std::string_view text);

} // namespace pf
