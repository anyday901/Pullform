#include "core/document/validate.hpp"

#include "core/document/tolerance.hpp"

#include <cmath>
#include <set>
#include <sstream>

namespace pf {

namespace {

std::string mmText(double meters) { return formatLength(meters, LengthUnit::Millimeter); }

bool finiteInRange(double v) { return std::isfinite(v) && std::abs(v) <= tol::kMaxCoordinate; }

bool finiteInRange(const Vec3& v) { return finiteInRange(v.x) && finiteInRange(v.y) && finiteInRange(v.z); }

bool sameVec(const Vec3& a, const Vec3& b) {
    return tol::nearlyEqual(a.x, b.x) && tol::nearlyEqual(a.y, b.y) && tol::nearlyEqual(a.z, b.z);
}

std::string join(std::string_view path, std::string_view rest) {
    std::string out(path);
    out += rest;
    return out;
}

/// Rotation and placed extent: a finite unit quaternion, and every corner of the
/// placed object inside the supported range.
std::optional<std::string> validatePlacement(const ProfileObject& o, std::string_view path) {
    const Quat& q = o.transform.rotation;
    if (!std::isfinite(q.w) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z)) {
        return join(path, ".transform.rotation: values must be finite");
    }
    if (const double len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        std::abs(len - 1.0) > tol::kRotationLengthTolerance) {
        return join(path, ".transform.rotation: must be a unit quaternion [x, y, z, w]");
    }
    const double h = o.extrusion ? o.extrusion->height : 0.0;
    for (const auto& p : o.profile.boundary) {
        if (!finiteInRange(profileToWorld(o, p, 0.0)) || !finiteInRange(profileToWorld(o, p, h))) {
            return join(path, ": the object extends beyond the supported range of +/-10 km");
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> checkLength(double value, std::string_view label) {
    const std::string l(label);
    if (!std::isfinite(value)) return l + " must be a finite number.";
    if (std::abs(value) < tol::kMinDimension) return l + " must be at least " + mmText(tol::kMinDimension) + ".";
    if (std::abs(value) > tol::kMaxCoordinate) {
        return l + " exceeds the supported maximum of " + formatLength(tol::kMaxCoordinate, LengthUnit::Meter) + ".";
    }
    return std::nullopt;
}

Expected<RectangleSize> rectangleSize(const std::vector<Vec2>& b) {
    if (b.size() != 4) return Error{"a rectangle boundary must have exactly 4 points"};
    for (const auto& p : b) {
        if (!finiteInRange(p.x) || !finiteInRange(p.y)) return Error{"boundary points must be finite and in range"};
    }
    const double w = b[1].x;
    const double d = b[3].y;
    const auto eq = [](double x, double y) { return tol::nearlyEqual(x, y); };
    const bool isRect = eq(b[0].x, 0) && eq(b[0].y, 0) && eq(b[1].y, 0) && eq(b[2].x, w) && eq(b[2].y, d) && eq(b[3].x, 0);
    if (!isRect) return Error{"must be the counter-clockwise rectangle [0,0],[w,0],[w,d],[0,d]"};
    if (w < 0 || d < 0) return Error{"boundary must be counter-clockwise with positive width and depth"};
    if (auto e = checkLength(w, "Width")) return Error{"rectangle is invalid: " + *e};
    if (auto e = checkLength(d, "Depth")) return Error{"rectangle is invalid: " + *e};
    return RectangleSize{w, d};
}

std::optional<std::string> validateObject(const ProfileObject& o, std::string_view path) {
    if (o.id.value == 0) return join(path, ".id: must be a positive object ID");
    if (o.name.size() > 200) return join(path, ".name: must be at most 200 characters");
    if (o.profile.kind != ProfileKind::Rectangle) return join(path, ".profile.kind: unsupported profile kind");
    const auto& pl = o.profile.plane;
    if (!finiteInRange(pl.origin) || !finiteInRange(pl.xAxis) || !finiteInRange(pl.yAxis)) {
        return join(path, ".profile.plane: values must be finite and in range");
    }
    // Milestone 1: profiles live on the object's local XY plane.
    if (!sameVec(pl.origin, {0, 0, 0}) || !sameVec(pl.xAxis, {1, 0, 0}) || !sameVec(pl.yAxis, {0, 1, 0})) {
        return join(path, ".profile.plane: only the local XY plane (origin 0,0,0; xAxis 1,0,0; yAxis 0,1,0) is "
                          "supported in this version");
    }
    if (auto size = rectangleSize(o.profile.boundary); !size) {
        return join(path, ".profile.boundary: " + size.error());
    }
    if (o.extrusion) {
        const double h = o.extrusion->height;
        if (!std::isfinite(h)) return join(path, ".extrusion.height: must be a finite number");
        if (h < tol::kMinDimension) return join(path, ".extrusion.height: must be at least " + mmText(tol::kMinDimension));
        if (h > tol::kMaxCoordinate) return join(path, ".extrusion.height: exceeds the supported range");
    }
    if (!finiteInRange(o.transform.translation)) {
        return join(path, ".transform.translation: values must be finite and within +/-10 km");
    }
    return validatePlacement(o, path);
}

std::optional<std::string> validateDocument(const Document& doc) {
    if (doc.nextId < 1 || doc.nextId > (std::uint64_t{1} << 53)) return "document.nextId: must be a positive integer";
    std::set<std::uint64_t> seen;
    for (std::size_t i = 0; i < doc.objects.size(); ++i) {
        const std::string path = "document.objects[" + std::to_string(i) + "]";
        if (!doc.objects[i]) return path + ": missing object";
        const auto& o = *doc.objects[i];
        if (auto e = validateObject(o, path)) return e;
        if (!seen.insert(o.id.value).second) return path + ".id: duplicate ID " + idString(o.id);
        if (o.id.value >= doc.nextId) {
            return "document.nextId: must be greater than the number in every object ID (found " + idString(o.id) + ")";
        }
    }
    return std::nullopt;
}

} // namespace pf
