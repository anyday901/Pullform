#include "core/geometry/element_pick.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pf {

namespace {

struct Candidate {
    ElementKind kind = ElementKind::Face;
    std::uint32_t index = 0;
    std::size_t target = 0;
    Vec3 point{0.0};
    double distance = 0.0; ///< along the ray
    double pixels = 0.0;   ///< from the pointer, on screen
};

/// Whether the ray passes within `margin` of the box (slab test on the inflated box).
bool rayNearBox(const Ray& ray, const Bounds& b, double margin, double limit) {
    if (!b.valid) return false;
    double t0 = 0.0, t1 = limit;
    for (int axis = 0; axis < 3; ++axis) {
        const double lo = b.min[axis] - margin, hi = b.max[axis] + margin;
        const double o = ray.origin[axis], d = ray.direction[axis];
        if (std::abs(d) < 1e-15) {
            if (o < lo || o > hi) return false;
            continue;
        }
        double a = (lo - o) / d, c = (hi - o) / d;
        if (a > c) std::swap(a, c);
        t0 = std::max(t0, a);
        t1 = std::min(t1, c);
        if (t0 > t1) return false;
    }
    return true;
}

/// Pick tolerance in world units around a box: the widest pixel radius at the
/// box's far side.
double pickMargin(const Ray& ray, const Bounds& b, double pixels, double pixelAngle) {
    const double far = glm::length(b.center() - ray.origin) + 0.5 * glm::length(b.size());
    return pixels * pixelAngle * far;
}

/// Nearest triangle hit along `ray` closer than `limit`, optionally skipping
/// faces of one target that touch the element being tested.
template <typename Skip>
std::optional<std::pair<std::size_t, double>> castFaces(const Ray& ray, std::span<const PickTarget> targets, double limit,
                                                        Skip&& skip, std::uint32_t* faceOut) {
    std::optional<std::pair<std::size_t, double>> best;
    for (std::size_t ti = 0; ti < targets.size(); ++ti) {
        if (!targets[ti].topology) continue;
        const auto& topo = *targets[ti].topology;
        if (!rayNearBox(ray, topo.bounds, 1e-9 + 1e-9 * glm::length(topo.bounds.size()), limit)) continue;
        for (const auto& tri : topo.triangles) {
            if (skip(ti, tri.face)) continue;
            const auto& face = topo.faces[tri.face];
            const auto t = intersectTriangle(ray, topo.vertices[tri.v[0]].position, topo.vertices[tri.v[1]].position,
                                             topo.vertices[tri.v[2]].position, /*cullBack=*/!face.twoSided);
            if (t && *t < limit && (!best || *t < best->second)) {
                best = std::make_pair(ti, *t);
                if (faceOut) *faceOut = tri.face;
            }
        }
    }
    return best;
}

/// Whether face index `face` contains the element at `index`.
bool touches(const ElementTopology& topo, ElementKind kind, std::uint32_t index, std::uint32_t face) {
    const auto& f = topo.faces[face];
    switch (kind) {
    case ElementKind::Vertex: return std::find(f.loop.begin(), f.loop.end(), index) != f.loop.end();
    case ElementKind::Edge: return std::find(f.edges.begin(), f.edges.end(), index) != f.edges.end();
    case ElementKind::Face: return face == index;
    }
    return false;
}

} // namespace

std::optional<ElementHit> pickElement(const Ray& ray, std::span<const PickTarget> targets, const ElementPickOptions& options) {
    const double pixelAngle = options.pixelAngle > 0.0 ? options.pixelAngle : 1e-3;
    std::vector<Candidate> candidates;

    for (std::size_t ti = 0; ti < targets.size(); ++ti) {
        if (!targets[ti].topology) continue;
        const auto& topo = *targets[ti].topology;
        const double reach = std::max(options.vertexPixels, options.edgePixels);
        if (!rayNearBox(ray, topo.bounds, pickMargin(ray, topo.bounds, reach, pixelAngle),
                        std::numeric_limits<double>::infinity())) {
            continue;
        }
        for (std::uint32_t v = 0; v < topo.vertices.size(); ++v) {
            const Vec3 p = topo.vertices[v].position;
            const double t = glm::dot(p - ray.origin, ray.direction);
            if (t <= 0.0) continue;
            const double px = glm::length(p - (ray.origin + ray.direction * t)) / (t * pixelAngle);
            if (px <= options.vertexPixels) candidates.push_back({ElementKind::Vertex, v, ti, p, t, px});
        }
        for (std::uint32_t e = 0; e < topo.edges.size(); ++e) {
            const Vec3 a = topo.vertices[topo.edges[e].a].position;
            const Vec3 b = topo.vertices[topo.edges[e].b].position;
            const Vec3 ab = b - a;
            double s = 0.0;
            if (const auto u = closestParameterOnAxis(a, ab, ray)) {
                s = std::clamp(*u, 0.0, 1.0);
            } else {
                // Parallel to the ray: either end is as close as any point.
                s = glm::dot(a - ray.origin, ray.direction) <= glm::dot(b - ray.origin, ray.direction) ? 0.0 : 1.0;
            }
            const Vec3 q = a + ab * s;
            const double t = glm::dot(q - ray.origin, ray.direction);
            if (t <= 0.0) continue;
            const double px = glm::length(q - (ray.origin + ray.direction * t)) / (t * pixelAngle);
            if (px <= options.edgePixels) candidates.push_back({ElementKind::Edge, e, ti, q, t, px});
        }
    }

    // Vertices before edges; nearer to the pointer first, then nearer to the eye.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) {
        if (x.kind != y.kind) return x.kind < y.kind;
        if (std::abs(x.pixels - y.pixels) > 0.5) return x.pixels < y.pixels;
        return x.distance < y.distance;
    });
    for (const auto& c : candidates) {
        // Visible when no face, other than the ones it lies on, is between the eye and it.
        const Vec3 toPoint = c.point - ray.origin;
        const double length = glm::length(toPoint);
        if (length <= 0.0) continue;
        const Ray sight{ray.origin, toPoint / length};
        const double limit = length * (1.0 - 1e-7) - 1e-9;
        const auto blocker = castFaces(
            sight, targets, limit,
            [&](std::size_t ti, std::uint32_t face) { return ti == c.target && touches(*targets[ti].topology, c.kind, c.index, face); },
            nullptr);
        if (!blocker) {
            const auto& topo = *targets[c.target].topology;
            return ElementHit{{targets[c.target].object, c.kind, topo.idAt(c.kind, c.index)}, c.point, c.distance};
        }
    }

    std::uint32_t face = 0;
    const auto hit = castFaces(ray, targets, std::numeric_limits<double>::infinity(),
                               [](std::size_t, std::uint32_t) { return false; }, &face);
    if (!hit) return std::nullopt;
    const auto& topo = *targets[hit->first].topology;
    return ElementHit{{targets[hit->first].object, ElementKind::Face, topo.faces[face].id}, ray.origin + ray.direction * hit->second, hit->second};
}

} // namespace pf
