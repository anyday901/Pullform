#include "core/commands/operations.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"

#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <numbers>

namespace pf {

namespace {

Expected<OpResult> replaceObject(const Document& doc, ProfileObject changed) {
    const auto index = doc.indexOf(changed.id);
    if (index < 0) return Error{"Object " + idString(changed.id) + " no longer exists."};
    if (auto e = validateObject(changed, idString(changed.id))) return Error{*e};
    OpResult result{doc, changed.id};
    result.document.objects[static_cast<std::size_t>(index)] = std::make_shared<const ProfileObject>(std::move(changed));
    return result;
}

} // namespace

std::optional<std::string> checkHeight(double height) {
    if (!std::isfinite(height)) return "Height must be a finite number.";
    if (height <= 0.0) {
        return "Height must be positive. Pushing a face below its base is not supported yet.";
    }
    if (height < tol::kMinDimension) {
        return "Height must be at least " + formatLength(tol::kMinDimension, LengthUnit::Millimeter) + ".";
    }
    if (height > tol::kMaxCoordinate) return "Height exceeds the supported maximum of 10 km.";
    return std::nullopt;
}

Expected<OpResult> createRectangle(const Document& doc, const RectanglePlacement& placement) {
    ProfileObject o;
    o.id = ObjectId{doc.nextId};
    o.name = "Rectangle " + std::to_string(doc.nextId);
    o.profile.kind = ProfileKind::Rectangle;
    o.profile.boundary = rectangleBoundary(placement.width, placement.depth);
    o.transform.translation = placement.corner;
    if (auto e = validateObject(o, "rectangle")) return Error{*e};
    OpResult result{doc, o.id};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

Expected<OpResult> extrudeProfile(const Document& doc, ObjectId id, double height) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (o->extrusion) return Error{"This object is already extruded. Select its cap to change the height."};
    if (auto e = checkHeight(height)) return Error{*e};
    ProfileObject changed = *o;
    changed.extrusion = Extrusion{height};
    if (changed.name.rfind("Rectangle ", 0) == 0) changed.name = "Box " + changed.name.substr(10);
    return replaceObject(doc, std::move(changed));
}

Expected<OpResult> setExtrusionHeight(const Document& doc, ObjectId id, double height) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (!o->extrusion) return Error{"This object is not extruded yet. Use Push/Pull on its face first."};
    if (auto e = checkHeight(height)) return Error{*e};
    ProfileObject changed = *o;
    changed.extrusion->height = height;
    return replaceObject(doc, std::move(changed));
}

namespace {

/// Rotates `v` by `radians` about the unit `axis` (Rodrigues). Quarter turns use
/// exact sines and cosines, so (1, 0, 0) turned 90 degrees about Z is exactly (0, 1, 0).
Vec3 rotateVector(const Vec3& v, const Vec3& axis, double radians) {
    // Callers pass angles already reduced to (-pi, pi], so `quarters` is small.
    constexpr std::array<double, 4> kQuarterCos{1.0, 0.0, -1.0, 0.0};
    constexpr std::array<double, 4> kQuarterSin{0.0, 1.0, 0.0, -1.0};
    double c = std::cos(radians);
    double s = std::sin(radians);
    if (const double quarters = radians / (std::numbers::pi / 2.0); std::abs(quarters - std::round(quarters)) < 1e-12) {
        const auto k = static_cast<long long>(std::round(quarters));
        const auto m = static_cast<std::size_t>(((k % 4) + 4) % 4);
        c = kQuarterCos[m];
        s = kQuarterSin[m];
    }
    return v * c + glm::cross(axis, v) * s + axis * glm::dot(axis, v) * (1.0 - c);
}

} // namespace

Expected<OpResult> rotateObject(const Document& doc, ObjectId id, const Vec3& pivot, const Vec3& axis, double radians) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (!std::isfinite(radians)) return Error{"Angle must be a finite number."};
    const double len = glm::length(axis);
    if (!std::isfinite(len) || len < 1e-12) return Error{"The rotation axis is not valid."};
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        return Error{"The rotation centre is not valid."};
    }
    const Vec3 a = axis / len;
    // Reduce to (-pi, pi] once, so the quaternion and the pivot-relative
    // translation use the same angle and huge typed angles stay well defined.
    radians = std::remainder(radians, 2.0 * std::numbers::pi);
    ProfileObject changed = *o;
    changed.transform.rotation = glm::normalize(axisAngle(a, radians) * o->transform.rotation);
    changed.transform.translation = pivot + rotateVector(o->transform.translation - pivot, a, radians);
    return replaceObject(doc, std::move(changed));
}

Expected<OpResult> scaleObject(const Document& doc, ObjectId id, const Vec3& f) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(f[i]) || f[i] <= 0.0) {
            return Error{"Scale factors must be positive numbers. Mirroring (negative factors) is not supported."};
        }
    }
    const auto size = rectangleSize(o->profile.boundary);
    if (!size) return Error{size.error()};
    const double w = size->width * f.x;
    const double d = size->depth * f.y;
    if (auto e = checkLength(w, "Scaled width")) return Error{*e};
    if (auto e = checkLength(d, "Scaled depth")) return Error{*e};
    ProfileObject changed = *o;
    changed.profile.boundary = rectangleBoundary(w, d);
    if (o->extrusion) {
        const double h = o->extrusion->height * f.z;
        if (auto e = checkLength(h, "Scaled height")) return Error{*e};
        changed.extrusion->height = h;
    }
    // Keep the base centre fixed: the local origin moves by c - F*c, c = (w/2, d/2, 0).
    const auto& plane = o->profile.plane;
    const Vec3 shift = plane.xAxis * ((size->width - w) * 0.5) + plane.yAxis * ((size->depth - d) * 0.5);
    changed.transform.translation = o->transform.translation + rotationMatrix(o->transform) * shift;
    return replaceObject(doc, std::move(changed));
}

std::optional<std::string> checkMove(const ProfileObject& object, const Vec3& offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) {
        return "Move distance must be a finite number.";
    }
    if (const Vec3 t = object.transform.translation + offset;
        std::abs(t.x) > tol::kMaxCoordinate || std::abs(t.y) > tol::kMaxCoordinate || std::abs(t.z) > tol::kMaxCoordinate) {
        return "The move would place the object outside the supported range of +/-10 km.";
    }
    return std::nullopt;
}

Expected<OpResult> moveObject(const Document& doc, ObjectId id, const Vec3& offset) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (auto e = checkMove(*o, offset)) return Error{*e};
    if (std::abs(offset.x) < tol::kLengthEpsilon && std::abs(offset.y) < tol::kLengthEpsilon &&
        std::abs(offset.z) < tol::kLengthEpsilon) {
        return Error{"Move distance must not be zero."};
    }
    ProfileObject changed = *o;
    changed.transform.translation += offset;
    return replaceObject(doc, std::move(changed));
}

Expected<OpResult> deleteObject(const Document& doc, ObjectId id) {
    const auto index = doc.indexOf(id);
    if (index < 0) return Error{"Object " + idString(id) + " no longer exists."};
    OpResult result{doc, std::nullopt};
    result.document.objects.erase(result.document.objects.begin() + index);
    return result;
}

Expected<OpResult> setDisplayUnit(const Document& doc, LengthUnit unit) {
    OpResult result{doc, std::nullopt};
    result.document.displayUnit = unit;
    return result;
}

} // namespace pf
