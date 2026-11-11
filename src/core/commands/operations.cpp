#include "core/commands/operations.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <algorithm>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>

namespace pf {

namespace {

Expected<OpResult> replaceObject(const Document& doc, ProfileObject changed) {
    const auto index = doc.indexOf(changed.id);
    if (index < 0) return Error{"Object " + idString(changed.id) + " no longer exists."};
    if (auto e = validateObject(changed, idString(changed.id))) return Error{*e};
    OpResult result{doc, changed.id, {}};
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
    OpResult result{doc, o.id, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

Expected<OpResult> extrudeProfile(const Document& doc, ObjectId id, double height) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (o->isMeshGroup()) return Error{"Push/pull of drawn faces is not supported yet."};
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
    if (o->isMeshGroup()) return Error{"Push/pull of drawn faces is not supported yet."};
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

namespace {

/// Centre of a mesh group's base in its local space (lowest local z): the
/// point scaleObject keeps fixed.
Vec3 meshBaseCenterLocal(const mesh::Mesh& m) {
    Bounds local;
    for (const auto& [vid, v] : m.vertices()) local.extend(v.position);
    return Vec3{local.center().x, local.center().y, local.min.z};
}

} // namespace

Vec3 objectBaseCenter(const ProfileObject& o) {
    if (o.isMeshGroup()) return localToWorld(o, meshBaseCenterLocal(*o.mesh));
    Vec2 sum{0.0};
    for (const auto& p : o.profile.boundary) sum += p;
    const Vec2 c = o.profile.boundary.empty() ? Vec2{0.0} : sum / static_cast<double>(o.profile.boundary.size());
    return profileToWorld(o, c, 0.0);
}

Expected<OpResult> scaleObject(const Document& doc, ObjectId id, const Vec3& f) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(f[i]) || f[i] <= 0.0) {
            return Error{"Scale factors must be positive numbers. Mirroring (negative factors) is not supported."};
        }
    }
    if (o->isMeshGroup()) {
        // Scale the local geometry about the centre of its base (lowest local z).
        const Vec3 c = meshBaseCenterLocal(*o->mesh);
        mesh::Mesh scaled = *o->mesh;
        scaled.translate(-c);
        scaled.scale(f);
        scaled.translate(c);
        ProfileObject changed = *o;
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(scaled));
        return replaceObject(doc, std::move(changed));
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

std::string duplicateName(const std::string& name, ObjectId newId) {
    for (std::string_view prefix : {std::string_view{"Rectangle "}, std::string_view{"Box "}}) {
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) continue;
        const auto rest = std::string_view{name}.substr(prefix.size());
        if (std::ranges::all_of(rest, [](char c) { return c >= '0' && c <= '9'; })) {
            std::string renamed{prefix};
            renamed.append(std::to_string(newId.value));
            return renamed;
        }
    }
    return name.empty() ? std::string{"Copy"} : name + " copy";
}

Expected<OpResult> duplicateObject(const Document& doc, ObjectId id, const Vec3& offset) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (auto e = checkMove(*o, offset)) return Error{*e};
    ProfileObject copy = *o;
    copy.id = ObjectId{doc.nextId};
    copy.name = duplicateName(o->name, copy.id);
    copy.transform.translation += offset;
    if (auto e = validateObject(copy, "copy")) return Error{*e};
    OpResult result{doc, copy.id, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(copy)));
    return result;
}

Expected<OpResult> deleteObject(const Document& doc, ObjectId id) {
    const auto index = doc.indexOf(id);
    if (index < 0) return Error{"Object " + idString(id) + " no longer exists."};
    OpResult result{doc, std::nullopt, {}};
    result.document.objects.erase(result.document.objects.begin() + index);
    // A dimension measures geometry; without its object it has nothing to measure.
    std::erase_if(result.document.dimensions,
                  [id](const Dimension& d) { return anchorUses(d.a, id) || anchorUses(d.b, id); });
    return result;
}

Expected<OpResult> addDimension(const Document& doc, const DimensionAnchor& a, const DimensionAnchor& b, const Vec3& offset) {
    const auto pa = resolveAnchor(doc, a);
    const auto pb = resolveAnchor(doc, b);
    if (!pa || !pb) return Error{"The measured object no longer exists."};
    if (measureBetween(*pa, *pb).length < tol::kMinDimension) {
        return Error{"The two points are the same. Pick two different points to dimension."};
    }
    Dimension d{DimensionId{doc.nextId}, a, b, offset};
    if (auto e = validateDimension(d, doc, "dimension")) return Error{*e};
    OpResult result{doc, std::nullopt, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.dimensions.push_back(std::move(d));
    return result;
}

Expected<OpResult> deleteDimension(const Document& doc, DimensionId id) {
    OpResult result{doc, std::nullopt, {}};
    if (const auto removed = std::erase_if(result.document.dimensions, [id](const Dimension& d) { return d.id == id; });
        removed == 0) {
        return Error{"Dimension " + idString(id) + " no longer exists."};
    }
    return result;
}

namespace {

/// The object as a mesh group: a profile group becomes one (same ids as
/// meshFromProfile, so corner anchors stay valid); a mesh group is unchanged.
ProfileObject asMeshGroup(const ProfileObject& o) {
    if (o.isMeshGroup()) return o;
    ProfileObject changed = o;
    changed.mesh = std::make_shared<const mesh::Mesh>(mesh::meshFromProfile(o));
    changed.profile.boundary.clear();
    changed.extrusion.reset();
    if (changed.name.rfind("Rectangle ", 0) == 0 || changed.name.rfind("Box ", 0) == 0) {
        changed.name = "Group " + std::to_string(o.id.value);
    }
    return changed;
}

Vec3 worldToLocal(const ProfileObject& o, const Vec3& world) {
    return glm::transpose(rotationMatrix(o.transform)) * (world - o.transform.translation);
}

std::optional<std::string> checkIds(const Document& doc, const std::vector<ObjectId>& ids) {
    if (ids.empty()) return "Nothing is selected.";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (!doc.find(ids[i])) return "Object " + idString(ids[i]) + " no longer exists.";
        if (std::find(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(i), ids[i]) != ids.begin() + static_cast<std::ptrdiff_t>(i)) {
            return "Object " + idString(ids[i]) + " is listed twice.";
        }
    }
    return std::nullopt;
}

/// Applies `op` to each object in turn on the evolving document.
template <class Op>
Expected<OpResult> forEachObject(const Document& doc, const std::vector<ObjectId>& ids, Op op) {
    if (auto e = checkIds(doc, ids)) return Error{*e};
    OpResult result{doc, std::nullopt, {}};
    for (const ObjectId id : ids) {
        auto step = op(result.document, id);
        if (!step) return Error{step.error()};
        result.document = std::move(step).value().document;
        result.objectIds.push_back(id);
    }
    result.objectId = ids.back();
    return result;
}

} // namespace

Expected<OpResult> drawEdge(const Document& doc, std::optional<ObjectId> target, const Vec3& a, const Vec3& b) {
    for (const Vec3& p : {a, b}) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::abs(p.x) > tol::kMaxCoordinate ||
            std::abs(p.y) > tol::kMaxCoordinate || std::abs(p.z) > tol::kMaxCoordinate) {
            return Error{"Line ends must be finite and within the supported range of +/-10 km."};
        }
    }
    if (glm::length(b - a) <= tol::kWeldDistance) return Error{"The line has no length. Pick two different points."};
    if (!target) {
        // Loose lines stick to the drawn geometry they touch.
        for (const auto& ptr : doc.objects) {
            if (!ptr->isMeshGroup()) continue;
            const auto& m = *ptr->mesh;
            if (m.findVertex(worldToLocal(*ptr, a)) || m.findVertex(worldToLocal(*ptr, b))) {
                target = ptr->id;
                break;
            }
        }
    }
    if (target) {
        const auto* o = doc.find(*target);
        if (!o) return Error{"Object " + idString(*target) + " no longer exists."};
        ProfileObject changed = asMeshGroup(*o);
        mesh::Mesh m = *changed.mesh;
        if (!m.addEdge(worldToLocal(changed, a), worldToLocal(changed, b))) {
            return Error{"The line has no length. Pick two different points."};
        }
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
        return replaceObject(doc, std::move(changed));
    }
    ProfileObject o;
    o.id = ObjectId{doc.nextId};
    o.name = "Group " + std::to_string(doc.nextId);
    o.transform.translation = a;
    mesh::Mesh m;
    m.addEdge(Vec3{0.0}, b - a);
    o.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    if (auto e = validateObject(o, "line")) return Error{*e};
    OpResult result{doc, o.id, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

Expected<OpResult> eraseEdge(const Document& doc, ObjectId id, mesh::EdgeId edge) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    ProfileObject changed = asMeshGroup(*o);
    mesh::Mesh m = *changed.mesh;
    if (!m.eraseEdge(edge)) return Error{"That edge no longer exists."};
    if (m.empty()) return deleteObject(doc, id); // nothing left: the group goes, with its dimensions
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    // Dimensions on corners that disappeared go with them.
    OpResult partial{doc, id, {}};
    const ProfileObject& probe = changed;
    std::erase_if(partial.document.dimensions, [&](const Dimension& d) {
        for (const auto* anchor : {&d.a, &d.b}) {
            if (anchor->object != id) continue;
            for (int v : anchor->vertices) {
                if (!objectVertex(probe, v)) return true;
            }
        }
        return false;
    });
    return replaceObject(partial.document, std::move(changed));
}

Expected<Vec3> selectionBaseCenter(const Document& doc, const std::vector<ObjectId>& ids) {
    if (auto e = checkIds(doc, ids)) return Error{*e};
    Bounds b;
    for (const ObjectId id : ids) b.extend(objectBounds(*doc.find(id)));
    return Vec3{(b.min.x + b.max.x) * 0.5, (b.min.y + b.max.y) * 0.5, b.min.z};
}

Expected<OpResult> moveObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& offset) {
    return forEachObject(doc, ids, [&](const Document& d, ObjectId id) { return moveObject(d, id, offset); });
}

Expected<OpResult> rotateObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& axis,
                                 double radians) {
    return forEachObject(doc, ids, [&](const Document& d, ObjectId id) { return rotateObject(d, id, pivot, axis, radians); });
}

Expected<OpResult> scaleObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& factors, const Vec3& pivot) {
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        return Error{"The scale centre is not valid."};
    }
    return forEachObject(doc, ids, [&](const Document& d, ObjectId id) -> Expected<OpResult> {
        const ProfileObject& before = *d.find(id);
        const Vec3 centre = objectBaseCenter(before);
        auto scaled = scaleObject(d, id, factors); // keeps `centre` fixed
        if (!scaled) return scaled;
        const Vec3 shift = (centre - pivot) * (factors - Vec3{1.0});
        if (std::abs(shift.x) < tol::kLengthEpsilon && std::abs(shift.y) < tol::kLengthEpsilon &&
            std::abs(shift.z) < tol::kLengthEpsilon) {
            return scaled;
        }
        const ProfileObject& after = *scaled->document.find(id);
        if (auto e = checkMove(after, shift)) return Error{*e};
        ProfileObject moved = after;
        moved.transform.translation += shift;
        return replaceObject(scaled->document, std::move(moved));
    });
}

Expected<OpResult> duplicateObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& offset) {
    if (auto e = checkIds(doc, ids)) return Error{*e};
    OpResult result{doc, std::nullopt, {}};
    for (const ObjectId id : ids) {
        auto step = duplicateObject(result.document, id, offset);
        if (!step) return Error{step.error()};
        result.objectIds.push_back(*step->objectId);
        result.document = std::move(step).value().document;
    }
    result.objectId = result.objectIds.back();
    return result;
}

Expected<OpResult> deleteObjects(const Document& doc, const std::vector<ObjectId>& ids) {
    auto r = forEachObject(doc, ids, [](const Document& d, ObjectId id) { return deleteObject(d, id); });
    if (!r) return r;
    OpResult result = std::move(r).value();
    result.objectId.reset();
    return result;
}

Expected<OpResult> setDisplayUnit(const Document& doc, LengthUnit unit) {
    OpResult result{doc, std::nullopt, {}};
    result.document.displayUnit = unit;
    return result;
}

} // namespace pf
