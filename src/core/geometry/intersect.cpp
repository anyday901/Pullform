#include "core/geometry/intersect.hpp"

#include "core/document/tolerance.hpp"
#include "core/geometry/mesh.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pf::mesh {

namespace {

/// Faces closer to parallel than this (sine of the angle between normals) are
/// treated as parallel: their meeting line would be badly conditioned.
constexpr double kParallelSine = 1e-9;

enum class Where { Outside, Boundary, Inside };

struct Projected {
    Vec3 origin{0.0};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    std::vector<std::vector<Vec2>> loops;

    [[nodiscard]] Vec2 to2d(const Vec3& p) const { return {glm::dot(p - origin, u), glm::dot(p - origin, v)}; }
};

Projected project(const PlanarFace& f) {
    Projected p;
    p.origin = f.loops.front().front();
    const Vec3 helper = std::abs(f.normal.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
    p.u = glm::normalize(glm::cross(helper, f.normal));
    p.v = glm::cross(f.normal, p.u);
    for (const auto& loop : f.loops) {
        auto& out = p.loops.emplace_back();
        for (const Vec3& q : loop) out.push_back(p.to2d(q));
    }
    return p;
}

double segmentDistance(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    const double t = len2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return glm::length(p - (a + ab * t));
}

/// Where a point (in the face's plane) is relative to the face: within the
/// weld distance of a loop it is on the boundary; otherwise even-odd over all
/// loops, so holes are outside.
Where classify(const Projected& f, const Vec3& point) {
    const Vec2 q = f.to2d(point);
    bool inside = false;
    for (const auto& loop : f.loops) {
        for (std::size_t i = 0, n = loop.size(); i < n; ++i) {
            const Vec2& a = loop[i];
            const Vec2& b = loop[(i + 1) % n];
            if (segmentDistance(q, a, b) <= tol::kWeldDistance) return Where::Boundary;
            if ((a.y > q.y) != (b.y > q.y)) {
                const double x = a.x + (q.y - a.y) * (b.x - a.x) / (b.y - a.y);
                if (q.x < x) inside = !inside;
            }
        }
    }
    return inside ? Where::Inside : Where::Outside;
}

/// Parameters along the line (point p0, unit direction d) where it meets the
/// face's loops: crossings of loop edges and loop vertices on the line.
void breakpoints(const PlanarFace& f, const Vec3& p0, const Vec3& d, std::vector<double>& out) {
    Vec3 m = glm::cross(f.normal, d); // in the face plane, across the line
    const double ml = glm::length(m);
    if (ml <= 0.0) return;
    m /= ml;
    for (const auto& loop : f.loops) {
        for (std::size_t i = 0, n = loop.size(); i < n; ++i) {
            const Vec3& a = loop[i];
            const Vec3& b = loop[(i + 1) % n];
            const double sa = glm::dot(a - p0, m);
            const double sb = glm::dot(b - p0, m);
            const double ta = glm::dot(a - p0, d);
            const double tb = glm::dot(b - p0, d);
            if (std::abs(sa) <= tol::kWeldDistance) out.push_back(ta);
            if ((sa > tol::kWeldDistance && sb < -tol::kWeldDistance) || (sa < -tol::kWeldDistance && sb > tol::kWeldDistance)) {
                out.push_back(ta + (tb - ta) * sa / (sa - sb));
            }
        }
    }
}

struct Box {
    Vec3 min{0.0};
    Vec3 max{0.0};
};

Box boundsOf(const PlanarFace& f) {
    Box b{f.loops.front().front(), f.loops.front().front()};
    for (const auto& loop : f.loops) {
        for (const Vec3& p : loop) {
            b.min = glm::min(b.min, p);
            b.max = glm::max(b.max, p);
        }
    }
    return b;
}

bool overlaps(const Box& a, const Box& b) {
    for (int i = 0; i < 3; ++i) {
        if (a.max[i] + tol::kWeldDistance < b.min[i] || b.max[i] + tol::kWeldDistance < a.min[i]) return false;
    }
    return true;
}

} // namespace

std::vector<Segment> faceIntersection(const PlanarFace& a, const PlanarFace& b) {
    std::vector<Segment> segments;
    if (a.loops.empty() || b.loops.empty() || a.loops.front().size() < 3 || b.loops.front().size() < 3) return segments;
    if (!overlaps(boundsOf(a), boundsOf(b))) return segments;

    const Vec3 n1 = a.normal;
    const Vec3 n2 = b.normal;
    const Vec3 dir = glm::cross(n1, n2);
    const double sine2 = glm::dot(dir, dir);
    if (sine2 <= kParallelSine * kParallelSine) return segments; // parallel or coplanar

    // A point on both planes (n . x = h). Dividing by |n1 x n2|^2 rather than
    // 1 - (n1 . n2)^2 avoids cancellation when the faces are nearly parallel.
    const double h1 = glm::dot(n1, a.loops.front().front());
    const double h2 = glm::dot(n2, b.loops.front().front());
    const Vec3 p0 = (glm::cross(dir, n1) * h2 + glm::cross(n2, dir) * h1) / sine2;
    const Vec3 d = dir / std::sqrt(sine2);

    std::vector<double> ts;
    breakpoints(a, p0, d, ts);
    breakpoints(b, p0, d, ts);
    if (ts.size() < 2) return segments;
    std::ranges::sort(ts);
    std::vector<double> stops;
    for (const double t : ts) {
        if (stops.empty() || t - stops.back() > tol::kWeldDistance) stops.push_back(t);
    }

    const Projected pa = project(a);
    const Projected pb = project(b);
    std::optional<double> open; // start of the covered run being built
    double end = 0.0;
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        const Vec3 mid = p0 + d * ((stops[i] + stops[i + 1]) * 0.5);
        const Where wa = classify(pa, mid);
        const Where wb = classify(pb, mid);
        const bool covered =
            wa != Where::Outside && wb != Where::Outside && !(wa == Where::Boundary && wb == Where::Boundary);
        if (covered) {
            if (!open) open = stops[i];
            end = stops[i + 1];
        } else if (open) {
            segments.emplace_back(p0 + d * *open, p0 + d * end);
            open.reset();
        }
    }
    if (open) segments.emplace_back(p0 + d * *open, p0 + d * end);
    std::erase_if(segments, [](const Segment& s) { return glm::length(s.second - s.first) <= tol::kWeldDistance; });
    return segments;
}

PlanarFace planarFace(const Mesh& mesh, FaceId face) {
    PlanarFace out;
    const Face* f = mesh.face(face);
    if (!f) return out;
    for (const auto& loop : f->loops) out.loops.push_back(mesh.loopPositions(loop));
    out.normal = f->normal;
    return out;
}

} // namespace pf::mesh
