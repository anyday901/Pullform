#include "core/geometry/measure.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

Vec3 profileNormal(const ProfileObject& o) { return glm::normalize(glm::cross(o.profile.plane.xAxis, o.profile.plane.yAxis)); }

// Same mapping as buildFaces, so a corner is bit-identical to the point the
// mesh, picking and snapping see.
Vec3 toWorld(const ProfileObject& o, const Vec2& p, double offset) {
    const auto& plane = o.profile.plane;
    return o.transform.translation + plane.origin + plane.xAxis * p.x + plane.yAxis * p.y + profileNormal(o) * offset;
}

} // namespace

int vertexCount(const ProfileObject& o) {
    const auto n = static_cast<int>(o.profile.boundary.size());
    return o.extrusion ? 2 * n : n;
}

std::optional<Vec3> objectVertex(const ProfileObject& o, int index) {
    const auto n = static_cast<int>(o.profile.boundary.size());
    if (index < 0 || index >= vertexCount(o)) return std::nullopt;
    if (index < n) return toWorld(o, o.profile.boundary[static_cast<std::size_t>(index)], 0.0);
    return toWorld(o, o.profile.boundary[static_cast<std::size_t>(index - n)], o.extrusion->height);
}

std::vector<std::pair<int, int>> objectEdges(const ProfileObject& o) {
    const auto n = static_cast<int>(o.profile.boundary.size());
    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < n; ++i) edges.emplace_back(i, (i + 1) % n);
    if (o.extrusion) {
        for (int i = 0; i < n; ++i) edges.emplace_back(n + i, n + (i + 1) % n);
        for (int i = 0; i < n; ++i) edges.emplace_back(i, n + i);
    }
    return edges;
}

std::optional<Vec3> resolveAnchor(const Document& doc, const DimensionAnchor& anchor) {
    if (!anchor.object) return anchor.point;
    const auto* o = doc.find(*anchor.object);
    if (!o || anchor.vertices.empty() || anchor.vertices.size() > 2) return std::nullopt;
    const auto first = objectVertex(*o, anchor.vertices[0]);
    if (!first) return std::nullopt;
    if (anchor.vertices.size() == 1) return first;
    const auto second = objectVertex(*o, anchor.vertices[1]);
    if (!second) return std::nullopt;
    return (*first + *second) * 0.5;
}

DimensionAnchor anchorAt(const Document& doc, const Vec3& point, std::optional<ObjectId> preferred, double tolerance) {
    const auto attachTo = [&](const ProfileObject& o) {
        double best = tolerance;
        std::optional<DimensionAnchor> found;
        const auto consider = [&](const Vec3& p, std::vector<int> vertices) {
            const double d = glm::length(p - point);
            if (d <= best) {
                best = d;
                found = DimensionAnchor{Vec3{0.0}, o.id, std::move(vertices)};
            }
        };
        for (int i = 0; i < vertexCount(o); ++i) consider(*objectVertex(o, i), {i});
        for (const auto& [i, j] : objectEdges(o)) consider((*objectVertex(o, i) + *objectVertex(o, j)) * 0.5, {i, j});
        return found;
    };
    if (preferred) {
        if (const auto* o = doc.find(*preferred)) {
            if (auto a = attachTo(*o)) return *a;
        }
    }
    for (const auto& ptr : doc.objects) {
        if (!ptr || (preferred && ptr->id == *preferred)) continue;
        if (auto a = attachTo(*ptr)) return *a;
    }
    return DimensionAnchor{point, std::nullopt, {}};
}

bool anchorUses(const DimensionAnchor& anchor, ObjectId id) { return anchor.object && *anchor.object == id; }

Measurement measureBetween(const Vec3& a, const Vec3& b) {
    const Vec3 d = b - a;
    return {d, glm::length(d)};
}

Vec3 perpendicularTo(const Vec3& a, const Vec3& b, const Vec3& v) {
    const Vec3 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    if (len2 <= 0.0) return v;
    return v - ab * (glm::dot(v, ab) / len2);
}

std::optional<Vec3> offsetFromRay(const Vec3& a, const Vec3& b, const Ray& ray) {
    const Vec3 ab = b - a;
    const double len = glm::length(ab);
    if (len <= 0.0) return std::nullopt;
    const Vec3 u = ab / len;
    // Plane through a-b whose normal is the view direction with its a-b part
    // removed: the plane containing the segment that faces the viewer most.
    Vec3 n = ray.direction - u * glm::dot(ray.direction, u);
    if (glm::length(n) < 1e-9) return std::nullopt; // looking straight along the segment
    n = glm::normalize(n);
    const double denom = glm::dot(ray.direction, n);
    if (std::abs(denom) < 1e-12) return std::nullopt;
    const double t = glm::dot(a - ray.origin, n) / denom;
    if (t <= 0.0) return std::nullopt;
    const Vec3 hit = ray.origin + ray.direction * t;
    return perpendicularTo(a, b, hit - a);
}

Vec3 defaultOffsetDirection(const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const Vec3 horizontal{ab.x, ab.y, 0.0};
    if (glm::length(horizontal) < 1e-12) return {1.0, 0.0, 0.0};
    // Rotate the horizontal direction 90 degrees clockwise (seen from above):
    // for a segment drawn along +X the dimension sits on the -Y side, in front.
    const Vec3 h = glm::normalize(horizontal);
    return {h.y, -h.x, 0.0};
}

DimensionLayout layoutDimension(const Vec3& a, const Vec3& b, const Vec3& offset) {
    DimensionLayout l;
    l.a = a;
    l.b = b;
    l.lineA = a + offset;
    l.lineB = b + offset;
    l.label = (l.lineA + l.lineB) * 0.5;
    l.length = glm::length(b - a);
    return l;
}

std::optional<DimensionLayout> layoutDimension(const Document& doc, const Dimension& d) {
    const auto a = resolveAnchor(doc, d.a);
    const auto b = resolveAnchor(doc, d.b);
    if (!a || !b) return std::nullopt;
    return layoutDimension(*a, *b, d.offset);
}

} // namespace pf
