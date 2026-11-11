#include "core/geometry/guide_snap.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

// Same preference as SnapIndex::nearest: an endpoint (or a guide point) wins
// over a nearby midpoint unless the midpoint is clearly nearer the pointer.
constexpr double kPointWeight = 0.6;
// A crossing ranks between an endpoint and a midpoint.
constexpr double kCrossingWeight = 0.8;
// Two lines cross when their closest points are this close (meters).
constexpr double kCrossingDistance = 1e-6;

struct Best {
    std::optional<ScoredSnap> found;
    void offer(const Ray& ray, double radius, std::optional<double> occluder, SnapKind kind, int feature, const Vec3& p,
               double weight) {
        const Vec3 dir = glm::normalize(ray.direction);
        const double t = glm::dot(p - ray.origin, dir);
        if (t <= 0.0) return;
        const double s = glm::length(p - ray.origin - dir * t) / t;
        if (s > radius) return;
        if (occluder && t > *occluder + radius * t) return; // behind the face under the pointer
        const double score = s * weight;
        if (found && score >= found->score) return;
        found = ScoredSnap{SnapResult{p, kind, std::nullopt, feature, p, {}}, score};
    }
};

} // namespace

std::optional<double> angularDistance(const Ray& ray, const Vec3& p) {
    const Vec3 dir = glm::normalize(ray.direction);
    const double t = glm::dot(p - ray.origin, dir);
    if (t <= 0.0) return std::nullopt;
    return glm::length(p - ray.origin - dir * t) / t;
}

std::optional<double> snapScore(const Ray& ray, const SnapResult& snap) {
    const auto s = angularDistance(ray, snap.featurePoint);
    if (!s) return std::nullopt;
    if (snap.kind == SnapKind::Endpoint || snap.kind == SnapKind::GuidePoint) return *s * kPointWeight;
    if (snap.kind == SnapKind::Intersection) return *s * kCrossingWeight;
    return *s;
}

std::optional<ScoredSnap> nearestGuidePoint(std::span<const Guide> guides, const SnapIndex& edges, const Ray& ray,
                                            double radius, std::optional<double> occluder) {
    if (!(radius > 0.0)) return std::nullopt;
    Best best;
    for (std::size_t i = 0; i < guides.size(); ++i) {
        const Guide& g = guides[i];
        if (g.kind == Guide::Kind::Point) {
            best.offer(ray, radius, occluder, SnapKind::GuidePoint, static_cast<int>(i), g.point, kPointWeight);
            continue;
        }
        // With another guide line (each pair once).
        for (std::size_t j = i + 1; j < guides.size(); ++j) {
            const Guide& h = guides[j];
            if (h.kind != Guide::Kind::Line) continue;
            const auto st = closestLineParameters(g.point, g.direction, h.point, h.direction);
            if (!st) continue;
            const Vec3 a = g.point + g.direction * st->first;
            const Vec3 b = h.point + h.direction * st->second;
            if (glm::length(a - b) > kCrossingDistance) continue;
            best.offer(ray, radius, occluder, SnapKind::Intersection, -1, a, kCrossingWeight);
        }
        // With an object edge: the crossing must lie on the segment.
        for (const auto& [id, entry] : edges.entries()) {
            const auto& f = entry.features;
            // The line misses the object's bounding sphere: no edge can cross it.
            const Vec3 w = f.center - g.point;
            if (glm::length(w - g.direction * glm::dot(w, g.direction)) > f.radius + kCrossingDistance) continue;
            for (const auto& [ia, ib] : f.edges) {
                const Vec3& ea = f.vertices[static_cast<std::size_t>(ia)];
                const Vec3& eb = f.vertices[static_cast<std::size_t>(ib)];
                const double len = glm::length(eb - ea);
                if (len <= 0.0) continue;
                const Vec3 u = (eb - ea) / len;
                const auto st = closestLineParameters(g.point, g.direction, ea, u);
                if (!st || st->second < -kCrossingDistance || st->second > len + kCrossingDistance) continue;
                const Vec3 a = g.point + g.direction * st->first;
                const Vec3 b = ea + u * st->second;
                if (glm::length(a - b) > kCrossingDistance) continue;
                best.offer(ray, radius, occluder, SnapKind::Intersection, -1, b, kCrossingWeight);
            }
        }
    }
    return best.found;
}

std::optional<SnapResult> nearestOnGuide(std::span<const Guide> guides, const Ray& ray, double radius,
                                         std::optional<double> occluder) {
    if (!(radius > 0.0)) return std::nullopt;
    Best best;
    for (std::size_t i = 0; i < guides.size(); ++i) {
        const Guide& g = guides[i];
        if (g.kind != Guide::Kind::Line) continue;
        const auto t = closestParameterOnAxis(g.point, g.direction, ray);
        if (!t) continue;
        best.offer(ray, radius, occluder, SnapKind::OnGuide, static_cast<int>(i), g.point + g.direction * *t, 1.0);
    }
    if (!best.found) return std::nullopt;
    return best.found->snap;
}

} // namespace pf
