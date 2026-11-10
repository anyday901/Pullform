#include "core/geometry/rectangle.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"

#include <algorithm>
#include <cmath>

namespace pf {

std::vector<Vec2> rectangleBoundary(double width, double depth) {
    return {{0.0, 0.0}, {width, 0.0}, {width, depth}, {0.0, depth}};
}

std::optional<std::string> checkRectangleCorner(const Vec3& p) {
    for (double v : {p.x, p.y, p.z}) {
        if (!std::isfinite(v)) return std::string("Corner coordinates must be finite numbers.");
        if (std::abs(v) > tol::kMaxCoordinate) return std::string("Corner is outside the supported model range (10 km).");
    }
    return std::nullopt;
}

Expected<RectanglePlacement> rectangleFromCorners(const Vec3& a, const Vec3& b) {
    if (auto e = checkRectangleCorner(a)) return Error{*e};
    if (auto e = checkRectangleCorner(b)) return Error{*e};
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
    if (auto e = checkRectangleCorner(start)) return Error{*e};
    if (auto e = checkRectangleCorner({start.x + width, start.y + depth, start.z})) return Error{*e};
    // Keep the typed sizes exactly: re-deriving them from the far corner by
    // subtraction would round (2 m typed off-grid could store as 1.9999999999999998).
    const Vec3 corner{width < 0 ? start.x + width : start.x, depth < 0 ? start.y + depth : start.y, start.z};
    return RectanglePlacement{corner, std::abs(width), std::abs(depth)};
}

} // namespace pf
