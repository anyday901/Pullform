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
    double t0 = 0.0;
    double t1 = limit;
    for (int axis = 0; axis < 3; ++axis) {
        const double lo = b.min[axis] - margin;
        const double hi = b.max[axis] + margin;
        const double o = ray.origin[axis];
        const double d = ray.direction[axis];
        if (std::abs(d) < 1e-15) {
            if (o < lo || o > hi) return false;
            continue;
        }
        double a = (lo - o) / d;
        double c = (hi - o) / d;
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
            // Equal distances (a rectangle drawn on a wall) go to the later target, drawn on top.
            const bool nearer = best && t.has_value() &&
                                (*t < best->second - 1e-9 * std::max(1.0, best->second) ||
                                 (ti > best->first && std::abs(*t - best->second) <= 1e-9 * std::max(1.0, best->second)));
            if (t.has_value() && *t < limit && (!best || nearer)) {
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
    using enum ElementKind;
    switch (kind) {
    case Vertex: return std::ranges::find(f.loop, index) != f.loop.end();
    case Edge: return std::ranges::find(f.edges, index) != f.edges.end();
    case Face: return face == index;
    }
    return false;
}

/// The point of edge a-b nearest the ray.
Vec3 closestOnEdge(const Vec3& a, const Vec3& b, const Ray& ray) {
    const Vec3 ab = b - a;
    if (const auto u = closestParameterOnAxis(a, ab, ray)) return a + ab * std::clamp(*u, 0.0, 1.0);
    // Parallel to the ray: either end is as close as any point.
    return glm::dot(a - ray.origin, ray.direction) <= glm::dot(b - ray.origin, ray.direction) ? a : b;
}

/// Distance along the ray and screen distance (pixels) from it to `p`; empty behind the eye.
std::optional<std::pair<double, double>> rayDistance(const Ray& ray, const Vec3& p, double pixelAngle) {
    const double t = glm::dot(p - ray.origin, ray.direction);
    if (t <= 0.0) return std::nullopt;
    return std::make_pair(t, glm::length(p - (ray.origin + ray.direction * t)) / (t * pixelAngle));
}

/// Vertices and edges of target `ti` within the pick radius of the ray.
void collectCandidates(const Ray& ray, const PickTarget& target, std::size_t ti, const ElementPickOptions& options,
                       double pixelAngle, std::vector<Candidate>& out) {
    const auto& topo = *target.topology;
    for (std::uint32_t v = 0; v < topo.vertices.size(); ++v) {
        if (topo.vertices[v].hidden) continue;
        const Vec3 p = topo.vertices[v].position;
        if (const auto d = rayDistance(ray, p, pixelAngle); d && d->second <= options.vertexPixels) {
            out.push_back(Candidate{ElementKind::Vertex, v, ti, p, d->first, d->second});
        }
    }
    for (std::uint32_t e = 0; e < topo.edges.size(); ++e) {
        if (topo.edges[e].soft) continue;
        const Vec3 q = closestOnEdge(topo.vertices[topo.edges[e].a].position, topo.vertices[topo.edges[e].b].position, ray);
        if (const auto d = rayDistance(ray, q, pixelAngle); d && d->second <= options.edgePixels) {
            out.push_back(Candidate{ElementKind::Edge, e, ti, q, d->first, d->second});
        }
    }
}

/// Whether nothing but faces it lies on is between the eye and candidate `c`.
bool visible(const Ray& ray, std::span<const PickTarget> targets, const Candidate& c) {
    const Vec3 toPoint = c.point - ray.origin;
    const double length = glm::length(toPoint);
    if (length <= 0.0) return false;
    const Ray sight{ray.origin, toPoint / length};
    const double limit = length * (1.0 - 1e-7) - 1e-9;
    const auto ownFace = [&targets, &c](std::size_t ti, std::uint32_t face) {
        return ti == c.target && touches(*targets[ti].topology, c.kind, c.index, face);
    };
    return !castFaces(sight, targets, limit, ownFace, nullptr).has_value();
}

} // namespace

std::optional<ElementHit> pickElement(const Ray& ray, std::span<const PickTarget> targets, const ElementPickOptions& options) {
    const double pixelAngle = options.pixelAngle > 0.0 ? options.pixelAngle : 1e-3;
    std::vector<Candidate> candidates;
    for (std::size_t ti = 0; ti < targets.size(); ++ti) {
        if (!targets[ti].topology) continue;
        const auto& bounds = targets[ti].topology->bounds;
        if (const double reach = std::max(options.vertexPixels, options.edgePixels);
            rayNearBox(ray, bounds, pickMargin(ray, bounds, reach, pixelAngle), std::numeric_limits<double>::infinity())) {
            collectCandidates(ray, targets[ti], ti, options, pixelAngle, candidates);
        }
    }

    // Vertices before edges; nearer to the pointer first (in half-pixel steps,
    // so near-ties go to the one nearer the eye), then nearer to the eye. The
    // steps keep the ordering strict and weak, as std::sort requires.
    const auto step = [](double pixels) { return std::floor(pixels * 2.0); };
    std::ranges::sort(candidates, [&step](const Candidate& x, const Candidate& y) {
        if (x.kind != y.kind) return x.kind < y.kind;
        if (step(x.pixels) != step(y.pixels)) return step(x.pixels) < step(y.pixels);
        return x.distance < y.distance;
    });
    for (const auto& c : candidates) {
        if (visible(ray, targets, c)) {
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
