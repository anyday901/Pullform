#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pf {

/// A ground-plane rectangle normalized to its minimum corner and positive size.
struct RectanglePlacement {
    Vec3 corner{0.0}; ///< minimum corner in world space
    double width = 0.0; ///< along +X
    double depth = 0.0; ///< along +Y
};

/// Counter-clockwise (seen from +Z) boundary anchored at the local origin.
std::vector<Vec2> rectangleBoundary(double width, double depth);

/// Checks that a rectangle corner is finite and inside the 10 km model range.
/// Returns a user-facing message when it is not.
std::optional<std::string> checkRectangleCorner(const Vec3& p);

/// Normalizes two opposite corners, drawn in any direction, into a placement
/// with positive width and depth. Rejects non-finite, out-of-range and
/// degenerate (thinner than 0.1 mm) rectangles.
Expected<RectanglePlacement> rectangleFromCorners(const Vec3& a, const Vec3& b);

/// Builds a placement from a start corner and signed width/depth; the signs
/// choose the drawing direction so typed sizes follow the pointer. The
/// placement's width and depth are the typed magnitudes, exactly.
Expected<RectanglePlacement> rectangleFromSignedSize(const Vec3& start, double width, double depth);

} // namespace pf
