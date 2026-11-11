#include "core/geometry/arc.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>

namespace pf {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
/// Smallest sweep worth drawing (about 0.006 degrees), and how close to a full
/// turn an arc may come before it would close on itself.
constexpr double kMinSweep = 1e-4;

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool inRange(const Vec3& v) {
    return finite(v) && std::abs(v.x) <= tol::kMaxCoordinate && std::abs(v.y) <= tol::kMaxCoordinate &&
           std::abs(v.z) <= tol::kMaxCoordinate;
}

/// The normal flipped so it faces +Z, or for a vertical plane +Y, then +X.
Vec3 canonicalNormal(Vec3 n) {
    constexpr double eps = 1e-12;
    if (n.z < -eps || (std::abs(n.z) <= eps && (n.y < -eps || (std::abs(n.y) <= eps && n.x < 0.0)))) n = -n;
    return n;
}

std::optional<Vec3> unitNormal(const Vec3& n) {
    const double len = glm::length(n);
    if (!(len > 0.0) || !std::isfinite(len)) return std::nullopt;
    return n / len;
}

/// Projects `v` onto the plane with unit normal `n`.
Vec3 inPlane(const Vec3& v, const Vec3& n) { return v - n * glm::dot(v, n); }

std::optional<std::string> checkArc(const ArcGeometry& arc) {
    if (!inRange(arc.center) || !std::isfinite(arc.radius) || !std::isfinite(arc.sweep)) {
        return "The arc must stay within the supported range of +/-10 km.";
    }
    if (arc.radius < tol::kMinDimension) return "The arc is too small; its radius must be at least 0.1 mm.";
    if (arc.radius > tol::kMaxCoordinate) return "The arc is too large; its radius must be at most 10 km.";
    if (std::abs(arc.sweep) < kMinSweep) return "The arc has no bulge. Move the pointer off the line.";
    if (std::abs(arc.sweep) > kTwoPi - kMinSweep) return "The arc would close on itself; use the Circle tool for a full circle.";
    for (const Vec3& p : {arcStartPoint(arc), arcEndPoint(arc)}) {
        if (!inRange(p)) return "The arc must stay within the supported range of +/-10 km.";
    }
    return std::nullopt;
}

Expected<ArcGeometry> checked(const ArcGeometry& arc) {
    if (auto e = checkArc(arc)) return Error{*e};
    return arc;
}

/// The arc on the circle centred at `center` with unit normal `n`, from
/// `start` to `end` and turning the way `sweepSign` says (+1 counter-clockwise).
ArcGeometry arcBetween(const Vec3& center, const Vec3& n, const Vec3& start, const Vec3& end, double sweepSign) {
    ArcGeometry arc;
    arc.center = center;
    arc.normal = n;
    const Vec3 r0 = inPlane(start - center, n);
    arc.radius = glm::length(r0);
    arc.xAxis = arc.radius > 0.0 ? r0 / arc.radius : Vec3{1.0, 0.0, 0.0};
    arc.start = 0.0;
    double a = signedAngle(r0, end - center, n); // (-pi, pi]
    if (sweepSign > 0.0 && a <= 0.0) a += kTwoPi;
    if (sweepSign < 0.0 && a >= 0.0) a -= kTwoPi;
    arc.sweep = a;
    return arc;
}

} // namespace

Vec3 arcPoint(const ArcGeometry& arc, double angle) {
    const Vec3 y = glm::cross(arc.normal, arc.xAxis);
    return arc.center + arc.radius * (std::cos(angle) * arc.xAxis + std::sin(angle) * y);
}

Vec3 arcStartPoint(const ArcGeometry& arc) { return arcPoint(arc, arc.start); }
Vec3 arcEndPoint(const ArcGeometry& arc) { return arcPoint(arc, arc.start + arc.sweep); }
Vec3 arcMidPoint(const ArcGeometry& arc) { return arcPoint(arc, arc.start + 0.5 * arc.sweep); }

Vec3 arcStartTangent(const ArcGeometry& arc) {
    const Vec3 radial = arcStartPoint(arc) - arc.center;
    const Vec3 t = glm::cross(arc.normal, radial);
    return glm::normalize(arc.sweep >= 0.0 ? t : -t);
}

Vec3 arcEndTangent(const ArcGeometry& arc) {
    const Vec3 radial = arcEndPoint(arc) - arc.center;
    const Vec3 t = glm::cross(arc.normal, radial);
    return glm::normalize(arc.sweep >= 0.0 ? t : -t);
}

double arcBulge(const ArcGeometry& arc) {
    // Sagitta r (1 - cos(sweep / 2)); a counter-clockwise arc bulges to the right
    // of its chord (away from the centre, which is on the left).
    const double h = arc.radius * (1.0 - std::cos(0.5 * arc.sweep));
    return arc.sweep >= 0.0 ? -h : h;
}

std::vector<Vec3> arcPoints(const ArcGeometry& arc, int segments) {
    if (segments < 1) segments = 1;
    std::vector<Vec3> out;
    out.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        out.push_back(arcPoint(arc, arc.start + t * arc.sweep));
    }
    return out;
}

int defaultArcSegments(double sweep) {
    const double turns = std::abs(sweep) / kTwoPi;
    const int n = static_cast<int>(std::ceil(24.0 * turns - 1e-6));
    return std::max(2, n);
}

std::optional<std::string> checkArcSegments(int segments) {
    if (segments < 2 || segments > kMaxCurveSegments) {
        return "The segment count must be between 2 and " + std::to_string(kMaxCurveSegments) + ".";
    }
    return std::nullopt;
}

double signedAngle(const Vec3& from, const Vec3& to, const Vec3& normal) {
    const Vec3 a = inPlane(from, normal);
    const Vec3 b = inPlane(to, normal);
    return std::atan2(glm::dot(glm::cross(a, b), normal), glm::dot(a, b));
}

double unwrapSweep(double previous, double wrapped) {
    double best = wrapped;
    for (const double k : {-2.0, -1.0, 1.0, 2.0}) {
        const double candidate = wrapped + k * kTwoPi;
        if (std::abs(candidate - previous) < std::abs(best - previous)) best = candidate;
    }
    if (best >= kTwoPi) best -= kTwoPi;
    if (best <= -kTwoPi) best += kTwoPi;
    return best;
}

Expected<ArcGeometry> arcThroughPoints(const Vec3& start, const Vec3& through, const Vec3& end) {
    for (const Vec3& p : {start, through, end}) {
        if (!inRange(p)) return Error{"Arc points must be finite and within the supported range of +/-10 km."};
    }
    if (glm::length(end - start) < tol::kMinDimension || glm::length(through - start) < tol::kMinDimension ||
        glm::length(end - through) < tol::kMinDimension) {
        return Error{"The arc's points must be at least 0.1 mm apart."};
    }
    // Circumcentre of the triangle (start, through, end), relative to `end`.
    const Vec3 a = start - end;
    const Vec3 b = through - end;
    const Vec3 axb = glm::cross(a, b);
    const double axb2 = glm::dot(axb, axb);
    // Collinear (or nearly): the bulge is below the weld distance relative to the chord.
    if (std::sqrt(axb2) <= tol::kWeldDistance * glm::length(start - end)) {
        return Error{"The three points are on one line. Move the middle point off the line."};
    }
    const Vec3 center = end + glm::cross(glm::dot(a, a) * b - glm::dot(b, b) * a, axb) / (2.0 * axb2);
    const Vec3 n = canonicalNormal(axb / std::sqrt(axb2));
    // Turn the way that passes `through`.
    const double toThrough = signedAngle(start - center, through - center, n);
    const double toEnd = signedAngle(start - center, end - center, n);
    const auto ccw = [](double v) { return v < 0.0 ? v + kTwoPi : v; };
    const double sign = ccw(toThrough) < ccw(toEnd) ? 1.0 : -1.0;
    return checked(arcBetween(center, n, start, end, sign));
}

Expected<ArcGeometry> arcFromBulge(const Vec3& start, const Vec3& end, double bulge, const Vec3& planeNormal) {
    const auto n = unitNormal(planeNormal);
    if (!n) return Error{"The drawing plane is not defined."};
    if (!inRange(start) || !inRange(end) || !std::isfinite(bulge)) {
        return Error{"Arc points must be finite and within the supported range of +/-10 km."};
    }
    const Vec3 chord = inPlane(end - start, *n);
    if (glm::length(chord) < tol::kMinDimension) return Error{"The arc's ends must be at least 0.1 mm apart."};
    if (std::abs(bulge) <= tol::kWeldDistance) return Error{"The arc has no bulge. Move the pointer off the line."};
    const Vec3 left = glm::normalize(glm::cross(*n, chord));
    const Vec3 apex = 0.5 * (start + end) + bulge * left;
    auto arc = arcThroughPoints(start, apex, end);
    if (!arc) return arc;
    // Keep the caller's plane orientation (a vertical wall facing -Y, say).
    ArcGeometry out = *arc;
    if (glm::dot(out.normal, *n) < 0.0) {
        out.normal = -out.normal;
        out.sweep = -out.sweep;
    }
    return out;
}

Expected<ArcGeometry> arcFromCenter(const Vec3& center, const Vec3& startPoint, double sweep, const Vec3& planeNormal) {
    const auto n = unitNormal(planeNormal);
    if (!n) return Error{"The drawing plane is not defined."};
    if (!inRange(center) || !inRange(startPoint)) return Error{"Arc points must be finite and within the supported range of +/-10 km."};
    const Vec3 r0 = inPlane(startPoint - center, *n);
    const double r = glm::length(r0);
    if (r < tol::kMinDimension) return Error{"The radius must be at least 0.1 mm."};
    ArcGeometry arc;
    arc.center = center;
    arc.normal = *n;
    arc.xAxis = r0 / r;
    arc.radius = r;
    arc.start = 0.0;
    arc.sweep = sweep;
    return checked(arc);
}

Expected<ArcGeometry> tangentArc(const Vec3& start, const Vec3& tangent, const Vec3& end, const Vec3& planeNormal) {
    const auto n = unitNormal(planeNormal);
    if (!n) return Error{"The drawing plane is not defined."};
    if (!inRange(start) || !inRange(end)) return Error{"Arc points must be finite and within the supported range of +/-10 km."};
    const Vec3 tPlane = inPlane(tangent, *n);
    if (!(glm::length(tPlane) > 0.0)) return Error{"The tangent direction is not in the drawing plane."};
    const Vec3 t = glm::normalize(tPlane);
    const Vec3 d = inPlane(end - start, *n);
    const double len2 = glm::dot(d, d);
    if (std::sqrt(len2) < tol::kMinDimension) return Error{"The arc's ends must be at least 0.1 mm apart."};
    // The centre is on the line through `start` perpendicular to the tangent,
    // at the same distance from `end`: |d - s * left|^2 = s^2.
    const Vec3 left = glm::cross(*n, t);
    const double side = glm::dot(left, d);
    if (std::abs(side) <= tol::kWeldDistance) {
        return Error{"The end point is in line with the tangent. Move the pointer off the line."};
    }
    const double s = len2 / (2.0 * side); // signed radius; positive: centre on the left, counter-clockwise
    const Vec3 center = start + s * left;
    return checked(arcBetween(center, *n, start, start + d, s > 0.0 ? 1.0 : -1.0));
}

} // namespace pf
