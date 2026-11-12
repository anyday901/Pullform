#include "core/commands/operations.hpp"

#include "core/document/tolerance.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <cmath>
#include <iterator>
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

namespace {

/// The first-vertex angle of a regular shape mirrored by the signs of `f`
/// (x: pi - a, y: -a, both: a + pi), which keeps the boundary counter-clockwise.
double mirroredStart(double start, const Vec3& f) {
    if (f.x < 0.0 && f.y < 0.0) start += std::numbers::pi;
    else if (f.x < 0.0) start = std::numbers::pi - start;
    else if (f.y < 0.0) start = -start;
    return std::remainder(start, 2.0 * std::numbers::pi);
}

/// scaleObject of a rectangle, circle or polygon with a negative factor. A
/// rectangle is symmetric across its width and depth about its base centre,
/// so mirroring there gives the same shape; a circle or polygon turns its
/// first vertex (mirroredStart). Mirroring the height flips the prism to the
/// other side of the base.
Expected<OpResult> mirrorProfile(const Document& doc, const ProfileObject& o, const Vec3& f) {
    auto scaled = scaleObject(doc, o.id, Vec3{std::abs(f.x), std::abs(f.y), f.z == 0.0 ? 1.0 : std::abs(f.z)});
    if (!scaled) return scaled;
    ProfileObject flipped = *scaled->document.find(o.id);
    if (o.profile.kind != ProfileKind::Rectangle && (f.x < 0.0 || f.y < 0.0)) {
        const auto shape = regularShape(flipped.profile.boundary);
        if (!shape) return Error{shape.error()};
        flipped.profile.boundary = regularBoundary(shape->radius, shape->segments, mirroredStart(shape->start, f));
    }
    if (f.z < 0.0 && o.extrusion) flipped.transform.translation -= profileNormalWorld(flipped) * flipped.extrusion->height;
    return replaceObject(scaled->document, std::move(flipped));
}

} // namespace

Expected<OpResult> scaleObject(const Document& doc, ObjectId id, const Vec3& f) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(f[i])) return Error{"Scale factors must be finite numbers."};
        if (f[i] == 0.0 && (i < 2 || o->extrusion || o->isMeshGroup())) {
            return Error{"Scale factors must not be zero (a negative factor mirrors)."};
        }
    }
    if (!o->isMeshGroup() && (f.x < 0.0 || f.y < 0.0 || f.z < 0.0)) return mirrorProfile(doc, *o, f);
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
    // Dimensions on corners that disappeared go with them.
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

Expected<OpResult> pushPullFace(const Document& doc, ObjectId id, mesh::FaceId face, double distance, mesh::FaceId* pushed) {
    const auto* o = doc.find(id);
    if (!o) return Error{"Object " + idString(id) + " no longer exists."};
    if (!o->isMeshGroup()) return Error{"Only drawn faces are pushed this way; use the height for a rectangle or box."};
    if (!std::isfinite(distance) || std::abs(distance) < tol::kMinDimension) return Error{"Distance must be at least 0.1 mm."};
    ProfileObject changed = *o;
    mesh::Mesh m = *changed.mesh;
    if (!m.face(face)) return Error{"That face no longer exists."};
    const auto result = m.pushPull(face, distance);
    if (!result) {
        return Error{"Push/pull works on a face standing on its own, or on the end of a prism that keeps a positive "
                     "length. Other faces are not supported yet."};
    }
    if (pushed) *pushed = result->face;
    changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
    return replaceObject(doc, std::move(changed));
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

namespace {

/// The factors along `frame` as factors along the object's own axes, when each
/// own axis lies along a frame axis (either way); nothing when a non-uniform
/// scale would shear the object.
std::optional<Vec3> ownFactors(const ProfileObject& o, const Vec3& factors, const Mat3& frame) {
    if (factors.x == factors.y && factors.y == factors.z) return factors;
    const Mat3 a = glm::transpose(frame) * objectAxes(o); // own axes in frame coordinates
    Vec3 own = factors;
    for (int i = 0; i < 3; ++i) {
        int along = -1;
        for (int j = 0; j < 3; ++j) {
            if (std::abs(std::abs(a[i][j]) - 1.0) < 1e-9) along = j;
        }
        if (along < 0) return std::nullopt;
        own[i] = factors[along];
    }
    return own;
}

/// One object of scaleObjects.
Expected<OpResult> scaleOneObject(const Document& d, ObjectId id, const Vec3& factors, const Vec3& pivot, const Mat3& frame) {
    const ProfileObject& before = *d.find(id);
    const auto own = ownFactors(before, factors, frame);
    if (!own) {
        // A sheared object cannot stay a profile: map its geometry directly (it becomes a mesh group).
        const mesh::MeshPtr m = mesh::objectMesh(before);
        std::vector<ElementRef> all;
        for (const auto& [vid, v] : m->vertices()) all.push_back(ElementRef{id, ElementKind::Vertex, vid});
        return transformElements(d, all, AffineMap::scale(pivot, factors, frame));
    }
    const Vec3 centre = objectBaseCenter(before);
    auto scaled = scaleObject(d, id, *own); // keeps `centre` fixed
    if (!scaled) return scaled;
    // The base centre goes where the scale about `pivot` (along `frame`) takes it.
    const Vec3 shift = frame * ((glm::transpose(frame) * (centre - pivot)) * (factors - Vec3{1.0}));
    if (glm::length(shift) < tol::kLengthEpsilon) return scaled;
    const ProfileObject& after = *scaled->document.find(id);
    if (auto e = checkMove(after, shift)) return Error{*e};
    ProfileObject moved = after;
    moved.transform.translation += shift;
    return replaceObject(scaled->document, std::move(moved));
}

} // namespace

Expected<OpResult> scaleObjects(const Document& doc, const std::vector<ObjectId>& ids, const Vec3& factors, const Vec3& pivot,
                                const Mat3& frame) {
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        return Error{"The scale centre is not valid."};
    }
    return forEachObject(doc, ids, [&factors, &pivot, &frame](const Document& d, ObjectId id) {
        return scaleOneObject(d, id, factors, pivot, frame);
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

// ---- Moving, rotating and scaling vertices, edges and faces ------------------------

AffineMap AffineMap::shift(const Vec3& offset) { return AffineMap{Mat3(1.0), offset}; }

AffineMap AffineMap::rotation(const Vec3& pivot, const Vec3& axis, double radians) {
    const Mat3 r = rotationMatrix(Transform{Vec3{0.0}, axisAngle(glm::normalize(axis), radians)});
    return AffineMap{r, pivot - r * pivot};
}

AffineMap AffineMap::scale(const Vec3& pivot, const Vec3& factors, const Mat3& frame) {
    const Mat3 f(Vec3{factors.x, 0.0, 0.0}, Vec3{0.0, factors.y, 0.0}, Vec3{0.0, 0.0, factors.z});
    const Mat3 m = frame * f * glm::transpose(frame);
    return AffineMap{m, pivot - m * pivot};
}

Mat3 objectAxes(const ProfileObject& object) {
    const Mat3 r = rotationMatrix(object.transform);
    if (object.isMeshGroup()) return r;
    const auto& plane = object.profile.plane;
    return r * Mat3(glm::normalize(plane.xAxis), glm::normalize(plane.yAxis), glm::normalize(glm::cross(plane.xAxis, plane.yAxis)));
}

namespace {

/// The mesh vertices an object's elements use, each once, in id order.
std::vector<mesh::VertexId> elementVertices(const mesh::Mesh& m, const std::vector<ElementRef>& elements, ObjectId object) {
    using enum ElementKind;
    std::vector<std::uint32_t> ids;
    const auto addFace = [&m, &ids](const mesh::Face& face) {
        for (const auto& loop : face.loops) {
            for (const auto v : m.loopVertices(loop)) ids.push_back(v.value);
        }
    };
    for (const ElementRef& e : elements) {
        if (e.object != object) continue;
        if (e.kind == Vertex && m.vertex(mesh::VertexId{e.id})) ids.push_back(e.id);
        if (const auto* edge = e.kind == Edge ? m.edge(mesh::EdgeId{e.id}) : nullptr) {
            ids.push_back(edge->v0.value);
            ids.push_back(edge->v1.value);
        }
        if (const auto* face = e.kind == Face ? m.face(mesh::FaceId{e.id}) : nullptr) addFace(*face);
    }
    std::ranges::sort(ids);
    const auto [first, last] = std::ranges::unique(ids);
    ids.erase(first, last);
    std::vector<mesh::VertexId> out;
    out.reserve(ids.size());
    for (const auto v : ids) out.emplace_back(v);
    return out;
}

/// The owners of `elements`, in order of first appearance.
std::vector<ObjectId> elementOwners(const std::vector<ElementRef>& elements) {
    std::vector<ObjectId> owners;
    for (const ElementRef& e : elements) {
        if (std::ranges::find(owners, e.object) == owners.end()) owners.push_back(e.object);
    }
    return owners;
}

/// Signed volume enclosed by a mesh whose every edge borders exactly two
/// faces (a closed shell); nothing for an open mesh, where it means nothing.
std::optional<double> closedVolume(const mesh::Mesh& m) {
    for (const auto& [eid, e] : m.edges()) {
        if (m.facesOfEdge(mesh::EdgeId{eid}).size() != 2) return std::nullopt;
    }
    if (m.faces().empty()) return std::nullopt;
    const mesh::Triangulation t = mesh::triangulate(m);
    double volume = 0.0;
    for (std::size_t i = 0; i + 2 < t.indices.size(); i += 3) {
        const Vec3& a = t.positions[t.indices[i]];
        const Vec3& b = t.positions[t.indices[i + 1]];
        const Vec3& c = t.positions[t.indices[i + 2]];
        volume += glm::dot(a, glm::cross(b, c)) / 6.0;
    }
    return volume;
}

/// A user-facing reason for a mesh that fails validation after a transform.
std::string transformProblem(const std::string& detail) {
    if (detail.find("zero length") != std::string::npos) return "That would shrink an edge to nothing.";
    if (detail.find("counter-clockwise") != std::string::npos || detail.find("clockwise") != std::string::npos) {
        return "That would turn a face inside out.";
    }
    if (detail.find("10 km") != std::string::npos) return "That would place geometry outside the supported range of +/-10 km.";
    if (detail.find("too small") != std::string::npos) return "That would shrink a face to nothing.";
    return "That would make invalid geometry (" + detail + ").";
}

} // namespace

std::vector<Vec3> elementPoints(const Document& doc, const std::vector<ElementRef>& elements) {
    std::vector<Vec3> points;
    for (const ObjectId id : elementOwners(elements)) {
        const auto* o = doc.find(id);
        if (!o) continue;
        const mesh::MeshPtr m = mesh::objectMesh(*o);
        for (const auto v : elementVertices(*m, elements, id)) points.push_back(localToWorld(*o, m->vertex(v)->position));
    }
    return points;
}

Expected<OpResult> transformElements(const Document& doc, const std::vector<ElementRef>& elements, const AffineMap& map) {
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            if (!std::isfinite(map.linear[c][r])) return Error{"The transform is not valid."};
        }
        if (!std::isfinite(map.translation[c])) return Error{"The transform is not valid."};
    }
    if (std::abs(glm::determinant(map.linear)) < 1e-12) return Error{"Scale factors must not be zero (a negative factor mirrors)."};
    const std::vector<ObjectId> owners = elementOwners(elements);
    if (owners.empty()) return Error{"Select vertices, edges or faces first."};
    OpResult result{doc, std::nullopt, {}};
    for (const ObjectId id : owners) {
        const auto* o = result.document.find(id);
        if (!o) return Error{"Object " + idString(id) + " no longer exists."};
        ProfileObject changed = asMeshGroup(*o);
        mesh::Mesh m = *changed.mesh;
        const auto vertices = elementVertices(m, elements, id);
        if (vertices.empty()) return Error{"The selected geometry no longer exists."};
        // World p -> M p + t becomes local q -> R^T M R q + R^T (M x + t - x),
        // with the object placed at x + R q.
        const Mat3 r = rotationMatrix(changed.transform);
        const Mat3 rt = glm::transpose(r);
        const Vec3 x = changed.transform.translation;
        const Mat3 linear = rt * map.linear * r;
        const Vec3 translation = rt * (map.linear * x + map.translation - x);
        const auto volumeBefore = closedVolume(m);
        if (!m.transformVertices(vertices, linear, translation)) {
            return Error{"That would make a face cross itself, or bend a face with holes (which cannot be folded yet)."};
        }
        if (auto e = mesh::validateMesh(m, "mesh")) return Error{transformProblem(*e)};
        // Faces stay valid one by one when a solid is pushed through itself;
        // its volume changing sign gives it away.
        if (const auto after = closedVolume(m); volumeBefore && after && (*volumeBefore > 0.0) != (*after > 0.0)) {
            return Error{"That would turn the solid inside out."};
        }
        changed.mesh = std::make_shared<const mesh::Mesh>(std::move(m));
        auto step = replaceObject(result.document, std::move(changed));
        if (!step) return Error{step.error()};
        result.document = std::move(step).value().document;
        result.objectIds.push_back(id);
    }
    result.objectId = result.objectIds.back();
    return result;
}

Expected<OpResult> moveElements(const Document& doc, const std::vector<ElementRef>& elements, const Vec3& offset) {
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) {
        return Error{"Move distance must be a finite number."};
    }
    if (glm::length(offset) < tol::kLengthEpsilon) return Error{"Move distance must not be zero."};
    return transformElements(doc, elements, AffineMap::shift(offset));
}

Expected<OpResult> rotateElements(const Document& doc, const std::vector<ElementRef>& elements, const Vec3& pivot,
                                  const Vec3& axis, double radians) {
    if (!std::isfinite(radians)) return Error{"Angle must be a finite number."};
    const double len = glm::length(axis);
    if (!std::isfinite(len) || len < 1e-12) return Error{"The rotation axis is not valid."};
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        return Error{"The rotation centre is not valid."};
    }
    return transformElements(doc, elements, AffineMap::rotation(pivot, axis / len, std::remainder(radians, 2.0 * std::numbers::pi)));
}

Expected<OpResult> scaleElements(const Document& doc, const std::vector<ElementRef>& elements, const Vec3& factors,
                                 const Vec3& pivot, const Mat3& frame) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(factors[i])) return Error{"Scale factors must be finite numbers."};
        if (factors[i] == 0.0) return Error{"Scale factors must not be zero (a negative factor mirrors)."};
    }
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
        return Error{"The scale centre is not valid."};
    }
    return transformElements(doc, elements, AffineMap::scale(pivot, factors, frame));
}

} // namespace pf
