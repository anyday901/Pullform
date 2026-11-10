#include "core/geometry/rectangle.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"

#include <algorithm>
#include <cmath>

namespace pf {

std::vector<Vec2> rectangleBoundary(double width, double depth) {
    return {{0.0, 0.0}, {width, 0.0}, {width, depth}, {0.0, depth}};
}

Expected<RectanglePlacement> rectangleFromCorners(const Vec3& a, const Vec3& b) {
    for (double v : {a.x, a.y, a.z, b.x, b.y, b.z}) {
        if (!std::isfinite(v)) return Error{"Corner coordinates must be finite numbers."};
        if (std::abs(v) > tol::kMaxCoordinate) return Error{"Corner is outside the supported model range (10 km)."};
    }
    const double width = std::abs(b.x - a.x);
    const double depth = std::abs(b.y - a.y);
    if (width < tol::kMinDimension || depth < tol::kMinDimension) {
        return Error{"Rectangle is degenerate: width and depth must each be at least " +
                     formatLength(tol::kMinDimension, LengthUnit::Millimeter) + "."};
    }
    if (width > tol::kMaxCoordinate || depth > tol::kMaxCoordinate) {
        return Error{"Rectangle exceeds the supported maximum size."};
    }
    return RectanglePlacement{{std::min(a.x, b.x), std::min(a.y, b.y), a.z}, width, depth};
}

Expected<RectanglePlacement> rectangleFromSignedSize(const Vec3& start, double width, double depth) {
    if (auto e = checkLength(width, "Width")) return Error{*e};
    if (auto e = checkLength(depth, "Depth")) return Error{*e};
    return rectangleFromCorners(start, {start.x + width, start.y + depth, start.z});
}

} // namespace pf
