#include "core/geometry/rectangle.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/plane.hpp"

#include <glm/geometric.hpp>

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

std::array<Vec3, 4> rectangleCorners(const RectanglePlacement& r) {
    Transform t;
    t.rotation = r.rotation;
    const Mat3 m = rotationMatrix(t);
    const Vec3 x = m * Vec3{r.width, 0.0, 0.0};
    const Vec3 y = m * Vec3{0.0, r.depth, 0.0};
    return {r.corner, r.corner + x, r.corner + x + y, r.corner + y};
}

namespace {

std::optional<std::string> checkCorners(const RectanglePlacement& r) {
    for (const auto& c : rectangleCorners(r)) {
        if (auto e = checkRectangleCorner(c)) return e;
    }
    return std::nullopt;
}

std::string degenerateMessage() {
    return "Rectangle is degenerate: width and depth must each be at least " +
           formatLength(tol::kMinDimension, LengthUnit::Millimeter) + ".";
}

} // namespace

Expected<RectanglePlacement> rectangleOnPlane(const PlaneBasis& plane, const Vec3& a, const Vec3& b) {
    if (auto e = checkRectangleCorner(a)) return Error{*e};
    if (auto e = checkRectangleCorner(b)) return Error{*e};
    const Vec2 pa = toPlane(plane, a);
    const Vec2 pb = toPlane(plane, b);
    const double width = std::abs(pb.x - pa.x);
    const double depth = std::abs(pb.y - pa.y);
    if (width < tol::kMinDimension || depth < tol::kMinDimension) return Error{degenerateMessage()};
    if (width > tol::kMaxCoordinate || depth > tol::kMaxCoordinate) return Error{"Rectangle exceeds the supported maximum size."};
    RectanglePlacement r{fromPlane(plane, {std::min(pa.x, pb.x), std::min(pa.y, pb.y)}), width, depth, planeRotation(plane)};
    if (auto e = checkCorners(r)) return Error{*e};
    return r;
}

Expected<RectanglePlacement> rectangleOnPlaneFromSize(const PlaneBasis& plane, const Vec3& start, double width, double depth) {
    if (auto e = checkLength(width, "Width")) return Error{*e};
    if (auto e = checkLength(depth, "Depth")) return Error{*e};
    if (auto e = checkRectangleCorner(start)) return Error{*e};
    const Vec2 s = toPlane(plane, start);
    const Vec2 corner{width < 0 ? s.x + width : s.x, depth < 0 ? s.y + depth : s.y};
    RectanglePlacement r{fromPlane(plane, corner), std::abs(width), std::abs(depth), planeRotation(plane)};
    if (auto e = checkCorners(r)) return Error{*e};
    return r;
}

Expected<RectanglePlacement> rotatedRectangle(const Vec3& a, const Vec3& b, const Vec3& normal, double depth) {
    if (auto e = checkRectangleCorner(a)) return Error{*e};
    if (auto e = checkRectangleCorner(b)) return Error{*e};
    if (!std::isfinite(depth)) return Error{std::string("Depth must be a finite number.")};
    const double width = glm::length(b - a);
    if (width < tol::kMinDimension || std::abs(depth) < tol::kMinDimension) return Error{degenerateMessage()};
    if (width > tol::kMaxCoordinate || std::abs(depth) > tol::kMaxCoordinate) {
        return Error{"Rectangle exceeds the supported maximum size."};
    }
    PlaneBasis frame;
    frame.xAxis = (b - a) / width;
    frame.yAxis = glm::normalize(glm::cross(normal, frame.xAxis));
    frame.origin = a;
    // A negative depth starts from the far side, so the sides stay positive
    // and the rectangle still faces `normal`.
    const Vec3 corner = depth < 0.0 ? a + frame.yAxis * depth : a;
    RectanglePlacement r{corner, width, std::abs(depth), planeRotation(frame)};
    if (auto e = checkCorners(r)) return Error{*e};
    return r;
}

} // namespace pf
