#include "core/commands/operations.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/follow_me.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/intersect.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/offset.hpp"
#include "core/geometry/solid.hpp"

#include <algorithm>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <numbers>
#include <string_view>

namespace pf {

Expected<OpResult> addGuide(const Document& doc, const Guide& guide) {
    Guide g = guide;
    g.id = GuideId{doc.nextId};
    if (g.kind == Guide::Kind::Line) {
        const double len = glm::length(g.direction);
        if (!std::isfinite(len) || len < 1e-9) return Error{"A guide line needs a direction."};
        g.direction /= len;
    } else {
        g.direction = Vec3{1.0, 0.0, 0.0};
    }
    if (validateGuide(g, "guide")) return Error{"The guide would be outside the supported range of +/-10 km."};
    OpResult result{doc, std::nullopt, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.guides.push_back(g);
    return result;
}

Expected<OpResult> deleteGuides(const Document& doc) {
    if (doc.guides.empty()) return Error{"There are no guides to delete."};
    OpResult result{doc, std::nullopt, {}};
    result.document.guides.clear();
    return result;
}

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
    o.transform.rotation = placement.rotation;
    if (auto e = validateObject(o, "rectangle")) return Error{*e};
    OpResult result{doc, o.id, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

Expected<OpResult> createCircle(const Document& doc, const CirclePlacement& placement, ProfileKind kind) {
    if (kind != ProfileKind::Circle && kind != ProfileKind::Polygon) return Error{"Only circles and polygons are drawn from a center."};
    auto checked = circleFromRadius(placement.center, placement.radius, placement.start, placement.segments, placement.rotation);
    if (!checked) return Error{checked.error()};
    ProfileObject o;
    o.id = ObjectId{doc.nextId};
    o.name = (kind == ProfileKind::Circle ? "Circle " : "Polygon ") + std::to_string(doc.nextId);
    o.profile.kind = kind;
    o.profile.boundary = regularBoundary(checked->radius, checked->segments, checked->start);
    o.transform.translation = checked->center;
    o.transform.rotation = checked->rotation;
    if (auto e = validateObject(o, kind == ProfileKind::Circle ? "circle" : "polygon")) return Error{*e};
    OpResult result{doc, o.id, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

namespace {

/// Default names that follow the object's id, planar name first, extruded second.
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kDefaultNames{{
    {"Rectangle ", "Box "},
    {"Circle ", "Cylinder "},
    {"Polygon ", "Prism "},
}};

} // namespace
std::optional<std::string> checkProfileHeight(double height) {
    if (std::isfinite(height) && height < 0.0) return checkHeight(-height);
    return checkHeight(height);
}

int flippedCorner(ProfileKind kind, int count, int index) {
    if (count <= 0 || index < 0 || index >= count) return index;
    return kind == ProfileKind::Rectangle ? count - 1 - index : (count - index) % count;
}

ProfileObject flippedProfile(const ProfileObject& object) {
    ProfileObject out = object;
    if (object.isMeshGroup()) return out;
    out.transform.rotation = glm::normalize(object.transform.rotation * Quat{0.0, 1.0, 0.0, 0.0});
    if (object.profile.kind != ProfileKind::Rectangle) {
        // A circle or polygon turns half a turn about its local X axis through
        // its center: local (x, y) lands where (x, -y) was. Mirroring the start
        // angle keeps every corner where it was (corner i is where corner
        // (n - i) mod n was).
        const auto shape = regularShape(object.profile.boundary);
        if (!shape) return object;
        out.profile.boundary = regularBoundary(shape->radius, shape->segments, -shape->start);
        return out;
    }
    // Turn half a turn about the local X axis (local (x, y) -> (x, -y), normal
    // reversed), then shift by the depth so the footprint stays where it is:
    // local (x, y) lands where (x, d - y) was.
    if (object.profile.boundary.size() != 4) return object;
    const double depth = object.profile.boundary[3].y;
    out.transform.translation = localToWorld(object, Vec3{0.0, depth, 0.0});
    return out;
}

Expected<OpResult> extrudeProfile(const Document& doc, ObjectId id, double height) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (o->isMeshGroup()) return Error{"Push/pull of drawn faces is not supported yet."};
    if (o->extrusion) return Error{"This object is already extruded. Select its cap to change the height."};
    if (auto e = checkProfileHeight(height)) return Error{*e};
    ProfileObject changed = height < 0.0 ? flippedProfile(*o) : *o;
    changed.extrusion = Extrusion{std::abs(height)};
    for (const auto& [planar, extruded] : kDefaultNames) {
        if (changed.name.starts_with(planar)) {
            changed.name = std::string{extruded} + changed.name.substr(planar.size());
            break;
        }
    }
    auto replaced = replaceObject(doc, std::move(changed));
    if (!replaced || height >= 0.0) return replaced;
    OpResult result = std::move(replaced).value();
    // The flip renumbers the corners (flippedCorner), so dimensions attached
    // to them keep measuring the same points.
    const int count = static_cast<int>(o->profile.boundary.size());
    for (auto& d : result.document.dimensions) {
        for (DimensionAnchor* a : {&d.a, &d.b}) {
            if (a->object != id) continue;
            for (int& v : a->vertices) v = flippedCorner(o->profile.kind, count, v);
        }
    }
    return result;
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
    // A circle or polygon is centred on its origin exactly; averaging its corners would round.
    if (o.profile.kind != ProfileKind::Rectangle) return profileToWorld(o, Vec2{0.0}, 0.0);
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
    if (o->profile.kind != ProfileKind::Rectangle) {
        // A circle or polygon is centred on its local origin, which is also the
        // centre of its base, so scaling the radius keeps that centre in place.
        if (std::abs(f.x - f.y) > 1e-9 * std::max(f.x, f.y)) {
            return Error{"Circles and polygons scale by the same factor in width and depth. Type one factor, or equal width and depth factors."};
        }
        const auto shape = regularShape(o->profile.boundary);
        if (!shape) return Error{shape.error()};
        const double r = shape->radius * f.x;
        if (auto e = checkCircle(r, shape->segments)) return Error{"Scaled radius: " + *e};
        ProfileObject changed = *o;
        changed.profile.boundary = regularBoundary(r, shape->segments, shape->start);
        if (o->extrusion) {
            const double h = o->extrusion->height * f.z;
            if (auto e = checkLength(h, "Scaled height")) return Error{*e};
            changed.extrusion->height = h;
        }
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
    for (std::string_view prefix : {std::string_view{"Rectangle "}, std::string_view{"Box "}, std::string_view{"Circle "},
                                    std::string_view{"Cylinder "}, std::string_view{"Polygon "}, std::string_view{"Prism "}}) {
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
    changed.profile.kind = ProfileKind::Rectangle; // mesh groups have no profile; the curves stay in the mesh
    changed.extrusion.reset();
    for (const auto& [planar, extruded] : kDefaultNames) {
        if (changed.name.starts_with(planar) || changed.name.starts_with(extruded)) {
            changed.name = "Group " + std::to_string(o.id.value);
            break;
        }
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

Expected<OpResult> drawPolyline(const Document& doc, std::optional<ObjectId> target, const std::vector<Vec3>& points,
                                bool closed) {
    std::vector<Vec3> chain;
    for (const Vec3& p : points) {
        if (chain.empty() || glm::length(p - chain.back()) > tol::kWeldDistance) chain.push_back(p);
    }
    if (closed && chain.size() > 2 && glm::length(chain.back() - chain.front()) > tol::kWeldDistance) {
        chain.push_back(chain.front());
    }
    if (chain.size() < 2) return Error{"The stroke has no length. Drag further before releasing."};
    OpResult result{doc, target, {}};
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        auto next = drawEdge(result.document, result.objectId, chain[i], chain[i + 1]);
        if (!next) return Error{next.error()};
        result = std::move(next).value();
    }
    return result;
}

Expected<OpResult> eraseEdge(const Document& doc, ObjectId id, mesh::EdgeId edge) {
    return eraseEdges(doc, id, {edge});
}

namespace {

/// Replaces the object, first dropping dimensions on its corners that no longer exist.
Expected<OpResult> replaceDroppingDimensions(const Document& doc, ProfileObject changed) {
    const ObjectId id = changed.id;
    OpResult partial{doc, id, {}};
    const ProfileObject& probe = changed;
    std::erase_if(partial.document.dimensions, [id, &probe](const Dimension& d) {
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

} // namespace

Expected<OpResult> eraseEdges(const Document& doc, ObjectId id, const std::vector<mesh::EdgeId>& edges) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    ProfileObject changed = asMeshGroup(*o);
    mesh::Mesh m = *changed.mesh;
    bool any = false;
    for (const mesh::EdgeId edge : edges) {
        // An edge may already be gone (listed twice); the rest still erase.
        if (m.eraseEdge(edge)) any = true;
    }
    if (!any) return Error{edges.size() == 1 ? "That edge no longer exists." : "Those edges no longer exist."};
    if (m.empty()) return deleteObject(doc, id); // nothing left: the group goes, with its dimensions
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    return replaceDroppingDimensions(doc, std::move(changed));
}

namespace {

/// The edges of `refs` grouped by object, objects in first-seen order.
std::vector<std::pair<ObjectId, std::vector<mesh::EdgeId>>> edgesByObject(const std::vector<ElementRef>& refs) {
    std::vector<std::pair<ObjectId, std::vector<mesh::EdgeId>>> groups;
    for (const auto& r : refs) {
        if (r.kind != ElementKind::Edge) continue;
        auto it = std::ranges::find_if(groups, [&r](const auto& g) { return g.first == r.object; });
        if (it == groups.end()) {
            groups.push_back({r.object, {}});
            it = std::prev(groups.end());
        }
        const mesh::EdgeId e{r.id};
        if (std::ranges::find(it->second, e) == it->second.end()) it->second.push_back(e);
    }
    return groups;
}

} // namespace

Expected<OpResult> eraseEdges(const Document& doc, const std::vector<ElementRef>& edges) {
    const auto groups = edgesByObject(edges);
    if (groups.empty()) return Error{"Pick an edge to erase."};
    OpResult result{doc, std::nullopt, {}};
    for (const auto& [id, ids] : groups) {
        auto step = eraseEdges(result.document, id, ids);
        if (!step) return Error{step.error()};
        result.document = std::move(step).value().document;
        result.objectIds.push_back(id);
    }
    result.objectId = result.objectIds.back();
    return result;
}

Expected<OpResult> setEdgesSoft(const Document& doc, const std::vector<ElementRef>& edges, bool soft, bool smooth) {
    const auto groups = edgesByObject(edges);
    if (groups.empty()) return Error{"Pick an edge first."};
    OpResult result{doc, std::nullopt, {}};
    for (const auto& [id, ids] : groups) {
        const auto* o = result.document.find(id);
        if (!o) return Error{"Object " + idString(id) + " no longer exists."};
        ProfileObject changed = asMeshGroup(*o);
        mesh::Mesh m = *changed.mesh;
        bool any = false;
        for (const mesh::EdgeId e : ids) {
            const auto* edge = m.edge(e);
            if (!edge) continue;
            mesh::Edge flagged = *edge;
            flagged.soft = soft;
            flagged.smooth = smooth;
            m.insertEdge(e, flagged);
            any = true;
        }
        if (!any) return Error{ids.size() == 1 ? "That edge no longer exists." : "Those edges no longer exist."};
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
        auto step = replaceObject(result.document, std::move(changed));
        if (!step) return Error{step.error()};
        result.document = std::move(step).value().document;
        result.objectIds.push_back(id);
    }
    result.objectId = result.objectIds.back();
    return result;
}

Expected<OpResult> pushPullFace(const Document& doc, ObjectId id, mesh::FaceId face, double distance, mesh::FaceId* pushed,
                                bool newFace) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (!std::isfinite(distance) || std::abs(distance) < tol::kMinDimension) return Error{"Distance must be at least 0.1 mm."};
    // A rectangle or box becomes a mesh group (same face ids as meshFromProfile).
    ProfileObject changed = asMeshGroup(*o);
    mesh::Mesh m = *changed.mesh;
    if (!m.face(face)) return Error{"That face no longer exists."};
    std::string why;
    const auto result = m.pushPull(face, distance, newFace, &why);
    if (!result) return Error{why.empty() ? std::string("This face cannot be pushed or pulled.") : why};
    if (pushed) *pushed = result->face;
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    // Dimensions on corners that disappeared (an opening cut through) go with them.
    Document partial = doc;
    const ProfileObject& probe = changed;
    std::erase_if(partial.dimensions, [id, &probe](const Dimension& d) {
        for (const auto* anchor : {&d.a, &d.b}) {
            if (anchor->object != id) continue;
            for (int v : anchor->vertices) {
                if (!objectVertex(probe, v)) return true;
            }
        }
        return false;
    });
    return replaceObject(partial, std::move(changed));
}

namespace {

/// Draws a planar shape (a rectangle, circle or polygon object, not in the
/// document) into group `target` as edges, so it sticks to the faces there.
Expected<OpResult> insertShape(const Document& doc, ObjectId target, const ProfileObject& shape, mesh::FaceId* face) {
    const auto* o = doc.find(target);
    if (!o) return Error{"Object " + idString(target) + " no longer exists."};
    ProfileObject changed = asMeshGroup(*o);
    mesh::Mesh m = *changed.mesh;
    const mesh::Mesh outline = mesh::meshFromProfile(shape);
    std::vector<Vec3> pts;
    for (const auto& [id, v] : outline.vertices()) pts.push_back(worldToLocal(changed, localToWorld(shape, v.position)));
    // Only a shape that lies on one face sticks to it; one hanging over the
    // edge stays a shape of its own.
    const auto onFace = [&m, &pts](const auto& entry) {
        return std::ranges::all_of(pts, [&m, &entry](const Vec3& p) { return mesh::faceContains(m, mesh::FaceId{entry.first}, p); });
    };
    if (std::ranges::none_of(m.faces(), onFace)) return Error{""}; // the callers add the shape on its own
    if (const auto* c = outline.curve(mesh::CurveId{1})) {
        // A circle or polygon keeps its true shape beside the segments.
        const Mat3 toLocal = glm::transpose(rotationMatrix(changed.transform)) * rotationMatrix(shape.transform);
        mesh::Curve curve = *c;
        curve.center = worldToLocal(changed, localToWorld(shape, c->center));
        curve.normal = glm::normalize(toLocal * c->normal);
        curve.xAxis = glm::normalize(toLocal * c->xAxis);
        curve.edges.clear();
        std::vector<Vec3> closed = pts;
        closed.push_back(pts.front());
        if (!m.addCurve(curve, closed)) return Error{"The shape is too small to draw."};
    } else {
        for (std::size_t i = 0; i < pts.size(); ++i) m.addEdge(pts[i], pts[(i + 1) % pts.size()]);
    }
    if (face) {
        // The face the outline closed: exactly its corners.
        *face = mesh::FaceId{0};
        for (const auto& [fid, f] : m.faces()) {
            const auto loop = m.loopPositions(f.loops[0]);
            const bool same = loop.size() == pts.size() && std::ranges::all_of(pts, [&loop](const Vec3& p) {
                return std::ranges::any_of(loop, [&p](const Vec3& q) { return glm::length(p - q) <= tol::kWeldDistance; });
            });
            if (same) {
                *face = mesh::FaceId{fid};
                break;
            }
        }
    }
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    return replaceObject(doc, std::move(changed));
}

} // namespace

Expected<OpResult> drawRectangleOn(const Document& doc, ObjectId target, const RectanglePlacement& placement, mesh::FaceId* face) {
    auto made = createRectangle(doc, placement);
    if (!made) return Error{made.error()};
    if (face) *face = mesh::FaceId{0};
    auto inserted = insertShape(doc, target, *made->document.find(*made->objectId), face);
    if (!inserted && inserted.error().empty()) return made; // not on one face: a rectangle of its own
    return inserted;
}

Expected<OpResult> drawCircleOn(const Document& doc, ObjectId target, const CirclePlacement& placement, ProfileKind kind,
                                mesh::FaceId* face) {
    auto made = createCircle(doc, placement, kind);
    if (!made) return Error{made.error()};
    if (face) *face = mesh::FaceId{0};
    auto inserted = insertShape(doc, target, *made->document.find(*made->objectId), face);
    if (!inserted && inserted.error().empty()) return made; // not on one face: a shape of its own
    return inserted;
}

namespace {

/// The one group the path's edges and faces belong to (the profile face itself does not count).
Expected<ObjectId> pathOwnerOf(const std::vector<ElementRef>& path, const ElementRef& profile) {
    std::optional<ObjectId> owner;
    for (const ElementRef& ref : path) {
        if (ref.kind == ElementKind::Vertex || ref == profile) continue;
        if (owner && *owner != ref.object) return Error{"The path must be edges of one group."};
        owner = ref.object;
    }
    if (!owner) return Error{"Pick the path first: edges, or a face to go around its edge."};
    return *owner;
}

/// The path's edges: edges as given, faces as their outer edges.
Expected<std::vector<mesh::EdgeId>> pathEdges(const mesh::Mesh& m, const std::vector<ElementRef>& path, ObjectId owner,
                                              const ElementRef& profile) {
    using enum ElementKind;
    std::vector<mesh::EdgeId> edges;
    for (const ElementRef& ref : path) {
        if (ref.object != owner || ref == profile || ref.kind == Vertex) continue;
        if (ref.kind == Edge) {
            edges.emplace_back(mesh::EdgeId{ref.id});
            continue;
        }
        const auto* f = m.face(mesh::FaceId{ref.id});
        if (!f) return Error{"A path face no longer exists."};
        for (const auto& use : f->loops[0]) edges.push_back(use.edge);
    }
    return edges;
}

/// Sharing an edge is fine (a profile drawn on the corner of the face it goes
/// round); only the profile's own edges are not a path.
bool onlyProfileEdges(const mesh::Mesh& m, mesh::FaceId profile, const std::vector<mesh::EdgeId>& edges) {
    const auto own = m.edgesOfFace(profile);
    return std::ranges::all_of(edges, [&own](mesh::EdgeId e) { return std::ranges::find(own, e) != own.end(); });
}

} // namespace

Expected<OpResult> followMe(const Document& doc, const ElementRef& profile, const std::vector<ElementRef>& path,
                            std::vector<mesh::FaceId>* created) {
    if (profile.kind != ElementKind::Face) return Error{"Pick a face as the profile to sweep."};
    const auto* owner = doc.find(profile.object);
    if (!owner) return Error{"Object " + idString(profile.object) + " no longer exists."};
    const auto pathId = pathOwnerOf(path, profile);
    if (!pathId) return Error{pathId.error()};
    const auto* pathOwner = doc.find(*pathId);
    if (!pathOwner) return Error{"Object " + idString(*pathId) + " no longer exists."};
    const mesh::MeshPtr pathMesh = mesh::objectMesh(*pathOwner);
    const auto edges = pathEdges(*pathMesh, path, *pathId, profile);
    if (!edges) return Error{edges.error()};
    const mesh::FaceId face{profile.id};
    const mesh::MeshPtr ownMesh = mesh::objectMesh(*owner);
    if (!ownMesh->face(face)) return Error{"The profile face no longer exists."};
    if (*pathId == profile.object && onlyProfileEdges(*ownMesh, face, *edges)) {
        return Error{"The path cannot use the profile's own edges. Pick edges away from the profile face."};
    }
    auto chain = mesh::chainPath(*pathMesh, *edges);
    if (!chain) return Error{chain.error()};
    ProfileObject changed = asMeshGroup(*owner);
    mesh::SweepPath sweep = std::move(chain).value();
    for (Vec3& p : sweep.points) p = worldToLocal(changed, localToWorld(*pathOwner, p));
    mesh::Mesh m = *changed.mesh;
    auto swept = mesh::followMe(m, face, sweep);
    if (!swept) return Error{swept.error()};
    if (created) *created = swept->createdFaces;
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    // A closed sweep can remove profile corners (a lathe profile's axis ends); their dimensions go too.
    return replaceDroppingDimensions(doc, std::move(changed));
}

std::vector<ElementRef> followMePath(const Document& doc, const ElementRef& element) {
    using enum ElementKind;
    const auto* o = doc.find(element.object);
    if (!o) return {};
    const mesh::MeshPtr m = mesh::objectMesh(*o);
    if (element.kind == Face) {
        if (!m->face(mesh::FaceId{element.id})) return {};
        return {element};
    }
    if (element.kind != Edge) return {};
    std::vector<ElementRef> out;
    for (const mesh::EdgeId e : mesh::pathThrough(*m, mesh::EdgeId{element.id})) {
        out.emplace_back(ElementRef{element.object, Edge, e.value});
    }
    return out;
}

Expected<Vec3> selectionBaseCenter(const Document& doc, const std::vector<ObjectId>& ids) {
    if (auto e = checkIds(doc, ids)) return Error{*e};
    Bounds b;
    for (const ObjectId id : ids) b.extend(objectBounds(*doc.find(id)));
    return Vec3{(b.min.x + b.max.x) * 0.5, (b.min.y + b.max.y) * 0.5, b.min.z};
}

Expected<OpResult> moveObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& offset) {
    return forEachObject(doc, ids, [&offset](const Document& d, ObjectId id) { return moveObject(d, id, offset); });
}

Expected<OpResult> rotateObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& axis,
                                 double radians) {
    return forEachObject(doc, ids, [&pivot, &axis, radians](const Document& d, ObjectId id) {
        return rotateObject(d, id, pivot, axis, radians);
    });
}

Expected<OpResult> scaleObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& factors, const Vec3& pivot) {
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        return Error{"The scale centre is not valid."};
    }
    return forEachObject(doc, ids, [&factors, &pivot](const Document& d, ObjectId id) -> Expected<OpResult> {
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

namespace pf {

namespace {

/// Draws the arc and, for a pie, its radii into `m`, in object-local space.
bool drawArcInto(mesh::Mesh& m, const ProfileObject& o, const ArcGeometry& arc, int segments, bool pie) {
    const Mat3 toLocal = glm::transpose(rotationMatrix(o.transform));
    mesh::Curve curve;
    curve.kind = mesh::CurveKind::Arc;
    curve.center = worldToLocal(o, arc.center);
    curve.normal = toLocal * arc.normal;
    curve.xAxis = toLocal * arc.xAxis;
    curve.radius = arc.radius;
    curve.start = arc.start;
    curve.sweep = arc.sweep;
    std::vector<Vec3> points;
    for (const Vec3& p : arcPoints(arc, segments)) points.push_back(worldToLocal(o, p));
    if (!m.addCurve(curve, points)) return false;
    if (pie) {
        m.addEdge(points.back(), curve.center);
        m.addEdge(curve.center, points.front());
    }
    return true;
}

} // namespace

Expected<OpResult> drawArc(const Document& doc, std::optional<ObjectId> target, const ArcGeometry& arc, int segments, bool pie) {
    if (auto e = checkArcSegments(segments)) return Error{*e};
    const std::vector<Vec3> ends = {arcStartPoint(arc), arcEndPoint(arc), arc.center};
    for (const Vec3& p : ends) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || std::abs(p.x) > tol::kMaxCoordinate ||
            std::abs(p.y) > tol::kMaxCoordinate || std::abs(p.z) > tol::kMaxCoordinate) {
            return Error{"The arc must stay within the supported range of +/-10 km."};
        }
    }
    if (!target) {
        // Like a line, an arc sticks to the drawn geometry its ends touch.
        for (const auto& ptr : doc.objects) {
            if (!ptr->isMeshGroup()) continue;
            const auto& m = *ptr->mesh;
            const bool touches = std::ranges::any_of(ends.begin(), ends.begin() + (pie ? 3 : 2),
                                                     [&](const Vec3& p) { return m.findVertex(worldToLocal(*ptr, p)).has_value(); });
            if (touches) {
                target = ptr->id;
                break;
            }
        }
    }
    if (target) {
        const auto* o = doc.find(*target);
        if (!o) return Error{std::format("Object {} no longer exists.", idString(*target))};
        ProfileObject changed = asMeshGroup(*o);
        mesh::Mesh m = *changed.mesh;
        if (!drawArcInto(m, changed, arc, segments, pie)) return Error{"The arc is too small to draw."};
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
        return replaceObject(doc, std::move(changed));
    }
    ProfileObject o;
    o.id = ObjectId{doc.nextId};
    o.name = "Group " + std::to_string(doc.nextId);
    o.transform.translation = arcStartPoint(arc);
    mesh::Mesh m;
    if (!drawArcInto(m, o, arc, segments, pie)) return Error{"The arc is too small to draw."};
    o.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    if (auto e = validateObject(o, "arc")) return Error{*e};
    OpResult result{doc, o.id, {}};
    result.document.nextId = doc.nextId + 1;
    result.document.objects.push_back(std::make_shared<const ProfileObject>(std::move(o)));
    return result;
}

Expected<OpResult> offsetFace(const Document& doc, ObjectId id, mesh::FaceId face, double distance, mesh::FaceId* ring) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    ProfileObject changed = asMeshGroup(*o); // a rectangle or box keeps its face ids
    mesh::Mesh m = *changed.mesh;
    if (!m.face(face)) return Error{"That face no longer exists."};
    auto result = mesh::offsetFace(m, face, distance);
    if (!result) return Error{result.error()};
    if (ring && result->ring) *ring = *result->ring;
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    return replaceObject(doc, std::move(changed));
}

} // namespace pf

namespace pf {

namespace {

/// A face of an object with its loops and normal in world space.
struct WorldFaceItem {
    ElementRef ref;
    mesh::PlanarFace world;
};

mesh::PlanarFace worldFace(const ProfileObject& o, const mesh::Mesh& m, mesh::FaceId f) {
    mesh::PlanarFace p = mesh::planarFace(m, f);
    const Mat3 r = rotationMatrix(o.transform);
    for (auto& loop : p.loops) {
        std::ranges::transform(loop, loop.begin(), [&](const Vec3& q) { return o.transform.translation + r * q; });
    }
    p.normal = r * p.normal;
    return p;
}

void appendFacesOf(const ProfileObject& o, std::vector<WorldFaceItem>& out) {
    const auto m = mesh::objectMesh(o);
    for (const auto& [fid, f] : m->faces()) {
        out.emplace_back(WorldFaceItem{ElementRef{o.id, ElementKind::Face, fid}, worldFace(o, *m, mesh::FaceId{fid})});
    }
}

/// The chosen faces (faces only, each once), or why one cannot be used.
Expected<std::vector<WorldFaceItem>> chosenFaces(const Document& doc, const std::vector<ElementRef>& faces) {
    std::vector<WorldFaceItem> chosen;
    for (const ElementRef& r : faces) {
        if (r.kind != ElementKind::Face || std::ranges::any_of(chosen, [&r](const WorldFaceItem& c) { return c.ref == r; })) {
            continue;
        }
        const auto* o = doc.find(r.object);
        if (!o) return Error{"Object " + idString(r.object) + " no longer exists."};
        const auto m = mesh::objectMesh(*o);
        if (!m->face(mesh::FaceId{r.id})) return Error{"A selected face no longer exists."};
        chosen.emplace_back(WorldFaceItem{r, worldFace(*o, *m, mesh::FaceId{r.id})});
    }
    if (chosen.empty()) return Error{"Select the faces or objects to intersect first."};
    return chosen;
}

/// What the chosen faces are cut against.
std::vector<WorldFaceItem> intersectPool(const Document& doc, const std::vector<WorldFaceItem>& chosen, IntersectMode mode) {
    using enum IntersectMode;
    if (mode == Selection) return chosen;
    std::vector<WorldFaceItem> pool;
    if (mode == Model) {
        for (const auto& ptr : doc.objects) appendFacesOf(*ptr, pool);
        return pool;
    }
    std::vector<ObjectId> owners;
    for (const WorldFaceItem& c : chosen) {
        if (std::ranges::find(owners, c.ref.object) == owners.end()) owners.push_back(c.ref.object);
    }
    for (const ObjectId id : owners) appendFacesOf(*doc.find(id), pool);
    return pool;
}

/// The crossing segments (world space) per object, from each unordered face pair once.
std::map<ObjectId, std::vector<mesh::Segment>> findCuts(const std::vector<WorldFaceItem>& chosen,
                                                        const std::vector<WorldFaceItem>& pool, IntersectMode mode) {
    std::map<ObjectId, std::vector<mesh::Segment>> cuts;
    std::set<std::pair<ElementRef, ElementRef>> done;
    const auto skip = [&done, mode](const ElementRef& c, const ElementRef& p) {
        if (p == c) return true;
        if (mode == IntersectMode::Context && p.object != c.object) return true; // each face's own object only
        return !done.emplace(std::minmax(c, p)).second;
    };
    for (const WorldFaceItem& c : chosen) {
        for (const WorldFaceItem& p : pool) {
            if (skip(c.ref, p.ref)) continue;
            const auto segments = mesh::faceIntersection(c.world, p.world);
            auto& own = cuts[c.ref.object];
            own.insert(own.end(), segments.begin(), segments.end());
            if (p.ref.object == c.ref.object) continue;
            auto& other = cuts[p.ref.object];
            other.insert(other.end(), segments.begin(), segments.end());
        }
    }
    return cuts;
}

std::string nothingCrossed(IntersectMode mode) {
    switch (mode) {
    case IntersectMode::Model: return "The selection does not cross any other faces.";
    case IntersectMode::Selection: return "The selected faces do not cross each other.";
    case IntersectMode::Context: return "The selection does not cross other faces of its own object.";
    }
    return "Nothing to intersect.";
}

} // namespace

Expected<OpResult> intersectFaces(const Document& doc, const std::vector<ElementRef>& faces, IntersectMode mode) {
    auto chosen = chosenFaces(doc, faces);
    if (!chosen) return Error{chosen.error()};
    const auto cuts = findCuts(*chosen, intersectPool(doc, *chosen, mode), mode);

    OpResult result{doc, std::nullopt, {}};
    for (const auto& ptr : doc.objects) {
        const auto it = cuts.find(ptr->id);
        if (it == cuts.end()) continue;
        ProfileObject changed = asMeshGroup(*ptr);
        mesh::Mesh m = *changed.mesh;
        for (const auto& [a, b] : it->second) m.addEdge(worldToLocal(changed, a), worldToLocal(changed, b));
        if (m == *changed.mesh) continue; // every cut was an edge it already had
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
        auto step = replaceObject(result.document, std::move(changed));
        if (!step) return Error{step.error()};
        result.document = std::move(step).value().document;
        result.objectIds.push_back(ptr->id);
    }
    if (result.objectIds.empty()) return Error{nothingCrossed(mode)};
    result.objectId = result.objectIds.back();
    return result;
}

} // namespace pf
