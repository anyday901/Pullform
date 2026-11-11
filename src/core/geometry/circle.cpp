#include "core/geometry/circle.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/rectangle.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <numbers>

namespace pf {

namespace {

/// Shortest side allowed: well above the 1 um weld distance, so every
/// segment stays a real edge after rounding.
constexpr double kMinSide = 10.0 * tol::kWeldDistance;

/// Snaps values within 1e-15 of -1, 0 or 1 to them, so a vertex at a quarter
/// turn lands exactly on an axis (like rotationMatrix does for rotations).
double tidy(double v) {
    for (const double exact : {-1.0, 0.0, 1.0}) {
        if (std::abs(v - exact) < 1e-15) return exact;
    }
    return v;
}

/// Tolerance for recognising a regular boundary: rounding grows with the radius.
double shapeTolerance(double radius) { return std::max(1e-9, radius * 1e-12) * 16.0; }

} // namespace

std::vector<Vec2> regularBoundary(double radius, int segments, double start) {
    std::vector<Vec2> out;
    if (segments <= 0) return out;
    out.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const double a = start + 2.0 * std::numbers::pi * i / segments;
        out.emplace_back(radius * tidy(std::cos(a)), radius * tidy(std::sin(a)));
    }
    return out;
}

std::optional<std::string> checkSegments(int segments) {
    if (segments < kMinSegments || segments > kMaxSegments) {
        return "The number of segments must be between " + std::to_string(kMinSegments) + " and " +
               std::to_string(kMaxSegments) + ".";
    }
    return std::nullopt;
}

std::optional<std::string> checkCircle(double radius, int segments) {
    if (auto e = checkSegments(segments)) return e;
    if (auto e = checkLength(radius, "Radius")) return e;
    if (radius < 0.0) return std::string("Radius must be positive.");
    if (2.0 * radius * std::sin(std::numbers::pi / segments) < kMinSide) {
        return "The circle is too small for " + std::to_string(segments) + " segments. Use fewer segments or a larger radius.";
    }
    return std::nullopt;
}

Expected<RegularShape> regularShape(const std::vector<Vec2>& b) {
    const auto n = static_cast<int>(b.size());
    if (auto e = checkSegments(n)) return Error{"a circle or polygon boundary needs 3 to 999 points"};
    for (const auto& p : b) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > tol::kMaxCoordinate || std::abs(p.y) > tol::kMaxCoordinate) {
            return Error{"boundary points must be finite and in range"};
        }
    }
    RegularShape shape{glm::length(b[0]), std::atan2(b[0].y, b[0].x), n};
    if (auto e = checkCircle(shape.radius, n)) return Error{"circle is invalid: " + *e};
    const auto expected = regularBoundary(shape.radius, n, shape.start);
    const double tolerance = shapeTolerance(shape.radius);
    for (int i = 0; i < n; ++i) {
        if (glm::length(b[static_cast<std::size_t>(i)] - expected[static_cast<std::size_t>(i)]) > tolerance) {
            return Error{"must be a regular counter-clockwise polygon centred on the local origin"};
        }
    }
    return shape;
}

Expected<CirclePlacement> circleFromRadius(const Vec3& center, double radius, double start, int segments) {
    if (auto e = checkRectangleCorner(center)) return Error{"Center: " + *e};
    if (!std::isfinite(start)) return Error{"The circle's direction is not valid."};
    if (auto e = checkCircle(radius, segments)) return Error{*e};
    if (std::abs(center.x) + radius > tol::kMaxCoordinate || std::abs(center.y) + radius > tol::kMaxCoordinate) {
        return Error{"The circle would extend beyond the supported range of +/-10 km."};
    }
    return CirclePlacement{center, radius, std::remainder(start, 2.0 * std::numbers::pi), segments};
}

Expected<CirclePlacement> circleFromPoints(const Vec3& center, const Vec3& onCircle, int segments) {
    const Vec2 d{onCircle.x - center.x, onCircle.y - center.y};
    const double r = glm::length(d);
    if (!std::isfinite(r)) return Error{"The circle's points must be finite."};
    if (r < tol::kMinDimension) {
        return Error{"The radius must be at least " + formatLength(tol::kMinDimension, LengthUnit::Millimeter) + "."};
    }
    return circleFromRadius(center, r, std::atan2(d.y, d.x), segments);
}

std::optional<Expected<int>> parseSegmentCount(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if (text.empty() || (text.back() != 's' && text.back() != 'S')) return std::nullopt;
    text.remove_suffix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '+' && c != '.') return std::nullopt;
    }
    int value = 0;
    const auto* first = text.data();
    if (*first == '+') ++first;
    const auto [end, ec] = std::from_chars(first, text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return Expected<int>{Error{"Type a whole number of segments, such as 24s."}};
    }
    if (auto e = checkSegments(value)) return Expected<int>{Error{*e}};
    return Expected<int>{value};
}

} // namespace pf
