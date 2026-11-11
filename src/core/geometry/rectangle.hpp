#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace pf {

/// A rectangle normalized to one corner and positive size. On the ground the
/// rotation is the identity: `corner` is the minimum corner and the sides run
/// along +X and +Y. On any other plane (a wall, a tilted face, a rotated
/// rectangle) the sides run along the rotated X and Y axes and the rectangle
/// faces the rotated Z axis.
struct RectanglePlacement {
    Vec3 corner{0.0}; ///< the corner at local (0, 0), in world space
    double width = 0.0; ///< along the rotated +X axis
    double depth = 0.0; ///< along the rotated +Y axis
    Quat rotation{1.0, 0.0, 0.0, 0.0}; ///< local axes to world (the object's rotation)
};

/// The four world corners in boundary order: (0,0), (w,0), (w,d), (0,d).
std::array<Vec3, 4> rectangleCorners(const RectanglePlacement& placement);

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

/// Like rectangleFromCorners, on any plane: `a` and `b` are opposite corners
/// (projected onto the plane), and the sides run along the plane's axes. The
/// rectangle faces the plane's normal.
Expected<RectanglePlacement> rectangleOnPlane(const PlaneBasis& plane, const Vec3& a, const Vec3& b);

/// Like rectangleFromSignedSize, on any plane: signed sizes along the plane's
/// x and y axes from `start`, stored exactly.
Expected<RectanglePlacement> rectangleOnPlaneFromSize(const PlaneBasis& plane, const Vec3& start, double width, double depth);

/// A rotated rectangle: its first side runs from `a` to `b`, and it extends
/// `depth` (signed) across that side in the plane with unit `normal`
/// (positive towards normal x (b - a)). It faces `normal`.
Expected<RectanglePlacement> rotatedRectangle(const Vec3& a, const Vec3& b, const Vec3& normal, double depth);

} // namespace pf
