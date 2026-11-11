#include "core/geometry/inference.hpp"

#include "core/document/tolerance.hpp"
#include "core/geometry/guide_snap.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace pf {

namespace {

constexpr std::array<Vec3, 3> kAxes{Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 0.0, 1.0}};
// Directions closer than this (|cross| of unit vectors) are the same line.
constexpr double kSameDirection = 1e-9;
// Two inference lines "cross" when they pass this close (meters).
constexpr double kCrossing = 1e-6;
// Axis lines from the start point win over other lines at the same angular
// distance from the pointer by this factor (an axis is the more common intent).
constexpr double kAxisPreference = 0.85;

/// One candidate inference line.
struct Line {
    Vec3 origin{0.0};
    Vec3 dir{1.0, 0.0, 0.0}; ///< unit
    SnapKind kind = SnapKind::Axis;
    int axis = -1;
    std::optional<SnapEdge> edge;
};

/// Angular distance of `p` from the ray, and its depth; nothing behind the eye.
struct Angular {
    double s = 0.0;
    double t = 0.0;
};

std::optional<Angular> angularDistance(const Ray& ray, const Vec3& dir, const Vec3& p) {
    const Vec3 w = p - ray.origin;
    const double t = glm::dot(w, dir);
    if (t <= 0.0) return std::nullopt;
    return Angular{glm::length(w - dir * t) / t, t};
}

bool parallel(const Vec3& a, const Vec3& b) { return glm::length(glm::cross(a, b)) <= kSameDirection; }

Vec3 normalizedOrZero(const Vec3& v) {
    const double len = glm::length(v);
    return len > tol::kLengthEpsilon ? v / len : Vec3{0.0};
}

class Engine {
public:
    Engine(const SnapIndex& index, const InferenceQuery& q) : index_(index), q_(q), dir_(glm::normalize(q.ray.direction)) {}

    std::optional<SnapResult> run() {
        if (!(q_.radius > 0.0)) return std::nullopt;
        buildLines();
        if (q_.lock) return locked(*q_.lock);

        auto hit = index_.nearest(q_.ray, q_.radius, q_.occluder, q_.exclude);
        if (!q_.guides.empty()) {
            // Guide points and guide crossings compete with corners and midpoints.
            if (auto g = nearestGuidePoint(q_.guides, index_, q_.ray, q_.radius, q_.occluder, q_.exclude)) {
                const auto score = hit ? snapScore(q_.ray, *hit) : std::nullopt;
                if (!score || g->score < *score) hit = std::move(g->snap);
            }
        }
        if (hit) return onPlane(std::move(*hit));

        std::optional<SnapResult> second;
        auto edge = index_.nearestEdge(q_.ray, q_.radius, q_.occluder, q_.exclude, &second);
        if (edge && !inPlane(edge->point)) edge.reset(); // an edge off the drawing plane is not a point on it
        if (second && !inPlane(second->point)) second.reset();
        if (edge) {
            if (auto x = intersection(*edge, second)) return x;
            return edge;
        }
        return bestLine();
    }

private:
    [[nodiscard]] bool visible(const Angular& a) const { return !q_.occluder || a.t <= *q_.occluder + q_.radius * a.t; }

    [[nodiscard]] std::optional<Angular> near(const Vec3& p) const {
        const auto a = angularDistance(q_.ray, dir_, p);
        if (!a || a->s > q_.radius || !visible(*a)) return std::nullopt;
        return a;
    }

    [[nodiscard]] bool inPlane(const Vec3& p) const {
        return !q_.plane || std::abs(glm::dot(p - q_.plane->point, q_.plane->normal)) <= tol::kWeldDistance;
    }

    [[nodiscard]] bool directionInPlane(const Vec3& d) const {
        return !q_.plane || std::abs(glm::dot(d, q_.plane->normal)) <= kSameDirection;
    }

    [[nodiscard]] Vec3 toPlane(const Vec3& p) const {
        if (!q_.plane) return p;
        return p - q_.plane->normal * glm::dot(p - q_.plane->point, q_.plane->normal);
    }

    /// A point feature off the drawing plane is used straight across on the
    /// plane, with a guide from the feature (a corner above the ground snaps
    /// a ground point below it).
    SnapResult onPlane(SnapResult hit) const {
        if (!q_.plane || inPlane(hit.point)) return hit;
        const Vec3 p = toPlane(hit.point);
        int axis = -1;
        for (int i = 0; i < 3; ++i) {
            if (parallel(q_.plane->normal, kAxes[static_cast<std::size_t>(i)])) axis = i;
        }
        hit.guides.emplace_back(hit.featurePoint, p, axis, GuideKind::Point);
        hit.point = p;
        return hit;
    }

    void addLine(const Line& line) {
        if (!directionInPlane(line.dir)) return;
        // One line per direction through the same origin: the first (an axis) wins.
        const bool dup = std::ranges::any_of(lines_, [&line](const Line& l) {
            return parallel(l.dir, line.dir) && glm::length(l.origin - line.origin) <= tol::kWeldDistance;
        });
        if (!dup) lines_.push_back(line);
    }

    void buildLines() {
        if (q_.from) {
            const Vec3 o = *q_.from;
            for (int i = 0; i < 3; ++i) addLine(Line{o, kAxes[static_cast<std::size_t>(i)], SnapKind::Axis, i, std::nullopt});
            // References: the remembered edges, then every edge through the start point.
            std::vector<SnapEdge> refs(q_.referenceEdges.begin(), q_.referenceEdges.end());
            for (auto& e : index_.edgesAt(o)) {
                if (std::ranges::find(refs, e) == refs.end()) refs.push_back(e);
            }
            for (const auto& e : refs) addEdgeLines(o, e);
        }
        // Construction guide lines (only those in the drawing plane, if any).
        for (const auto& g : q_.guides) {
            if (g.kind == Guide::Kind::Line && inPlane(g.point)) addLine(Line{g.point, g.direction, SnapKind::OnGuide, -1, std::nullopt});
        }
        for (const auto& r : q_.references) {
            if (q_.from && glm::length(r - *q_.from) <= tol::kWeldDistance) continue;
            const Vec3 o = toPlane(r);
            for (int i = 0; i < 3; ++i) addLine(Line{o, kAxes[static_cast<std::size_t>(i)], SnapKind::Inference, i, std::nullopt});
        }
    }

    void addEdgeLines(const Vec3& o, const SnapEdge& e) {
        const Vec3 u = e.direction();
        if (u == Vec3{0.0}) return;
        addLine(Line{o, u, SnapKind::Parallel, -1, e});
        // Perpendicular: in the drawing plane when there is one. Otherwise the
        // line from the start point to its foot on the edge, then the
        // horizontal and the upright perpendiculars.
        std::vector<Vec3> perps;
        if (q_.plane) {
            perps.push_back(normalizedOrZero(glm::cross(q_.plane->normal, u)));
        } else {
            const Vec3 foot = e.a + u * glm::dot(o - e.a, u);
            perps.push_back(normalizedOrZero(foot - o));
            const Vec3 h = normalizedOrZero(glm::cross(kAxes[2], u));
            perps.push_back(h);
            perps.push_back(normalizedOrZero(glm::cross(u, h)));
        }
        for (const auto& p : perps) {
            if (p != Vec3{0.0}) addLine(Line{o, p, SnapKind::Perpendicular, -1, e});
        }
    }

    /// The guides that show a line inference reaching `p`.
    static void lineGuides(const Line& l, const Vec3& p, std::vector<SnapGuide>& guides) {
        switch (l.kind) {
        case SnapKind::Inference: guides.emplace_back(l.origin, p, l.axis, GuideKind::Point); break;
        case SnapKind::OnGuide: break; // the guide itself is drawn already
        case SnapKind::Parallel:
        case SnapKind::Perpendicular:
            guides.emplace_back(l.origin, p, -1, GuideKind::Parallel);
            if (l.edge) guides.emplace_back(l.edge->a, l.edge->b, -1, GuideKind::Reference);
            break;
        default: guides.emplace_back(l.origin, p, l.axis, GuideKind::Axis); break;
        }
    }

    static SnapConstraint constraintOf(const Line& l) {
        return SnapConstraint{SnapConstraint::Type::Line, l.origin, l.dir, l.kind, l.axis, l.edge};
    }

    SnapResult lineResult(const Line& l, const Vec3& p) const {
        SnapResult r{p, l.kind, std::nullopt, -1, p, {}};
        lineGuides(l, p, r.guides);
        r.edge = l.edge;
        r.constraint = constraintOf(l);
        return r;
    }

    /// Edge crossing another edge or an inference line near the pointer.
    std::optional<SnapResult> intersection(const SnapResult& edge, const std::optional<SnapResult>& second) const {
        const SnapEdge& e = *edge.edge;
        const Vec3 u = e.direction();
        const double len = glm::length(e.b - e.a);
        std::optional<SnapResult> best;
        double bestS = q_.radius;
        const auto consider = [&](const Vec3& o, const Vec3& d, auto&& make) {
            const auto params = closestLineParameters(e.a, u, o, d);
            if (!params) return;
            const Vec3 p = e.a + u * params->first;
            if (glm::length(p - (o + d * params->second)) > kCrossing) return;
            if (params->first < -tol::kWeldDistance || params->first > len + tol::kWeldDistance) return;
            const auto a = near(p);
            if (!a || a->s > bestS) return;
            auto r = make(p);
            if (!r) return;
            bestS = a->s;
            best = std::move(r);
        };
        if (second && second->edge) {
            const SnapEdge& f = *second->edge;
            const double flen = glm::length(f.b - f.a);
            consider(f.a, f.direction(), [&](const Vec3& p) -> std::optional<SnapResult> {
                const double v = glm::dot(p - f.a, f.direction());
                if (v < -tol::kWeldDistance || v > flen + tol::kWeldDistance) return std::nullopt;
                SnapResult r{p, SnapKind::Intersection, edge.objectId, -1, p, {}};
                r.edge = e;
                return r;
            });
        }
        for (const auto& l : lines_) {
            if (parallel(l.dir, u)) continue;
            consider(l.origin, l.dir, [&](const Vec3& p) -> std::optional<SnapResult> {
                SnapResult r{p, SnapKind::Intersection, edge.objectId, -1, p, {}};
                lineGuides(l, p, r.guides);
                r.edge = e;
                return r;
            });
        }
        return best;
    }

    /// The line nearest the pointer, or the crossing of two lines near it.
    std::optional<SnapResult> bestLine() const {
        struct Hit {
            std::size_t line = 0;
            Vec3 point{0.0};
            double score = 0.0;
        };
        std::vector<Hit> hits;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const Line& l = lines_[i];
            const auto t = closestParameterOnAxis(l.origin, l.dir, q_.ray);
            if (!t) continue;
            const Vec3 p = l.origin + l.dir * *t;
            const auto a = near(p);
            if (!a) continue;
            hits.push_back(Hit{i, p, a->s * (l.kind == SnapKind::Axis ? kAxisPreference : 1.0)});
        }
        if (hits.empty()) return std::nullopt;
        std::ranges::sort(hits, [](const Hit& a, const Hit& b) { return a.score < b.score; });

        // Two lines crossing near the pointer: their crossing (a line from the
        // start point meeting a From Point guide, for example).
        std::optional<SnapResult> crossing;
        double crossingS = q_.radius;
        for (std::size_t i = 0; i < hits.size(); ++i) {
            for (std::size_t j = i + 1; j < hits.size(); ++j) {
                const Line& a = lines_[hits[i].line];
                const Line& b = lines_[hits[j].line];
                if (parallel(a.dir, b.dir)) continue;
                const auto params = closestLineParameters(a.origin, a.dir, b.origin, b.dir);
                if (!params) continue;
                const Vec3 p = a.origin + a.dir * params->first;
                if (glm::length(p - (b.origin + b.dir * params->second)) > kCrossing) continue;
                const auto ang = near(p);
                if (!ang || ang->s > crossingS) continue;
                crossingS = ang->s;
                // The stronger kind names the point; both guides show.
                const Line& lead = a.kind >= b.kind ? a : b;
                const Line& other = &lead == &a ? b : a;
                SnapResult r = lineResult(lead, p);
                lineGuides(other, p, r.guides);
                crossing = std::move(r);
            }
        }
        if (crossing) return crossing;
        const Hit& h = hits.front();
        return lineResult(lines_[h.line], h.point);
    }

    std::optional<SnapResult> locked(const SnapConstraint& c) const {
        const auto raw = pointOnConstraint(c, q_.ray);
        if (!raw) return std::nullopt;
        const auto project = [&c](const Vec3& p) {
            if (c.type == SnapConstraint::Type::Line) return c.origin + c.direction * glm::dot(p - c.origin, c.direction);
            return p - c.direction * glm::dot(p - c.origin, c.direction);
        };
        SnapResult r{*raw, c.kind, std::nullopt, -1, *raw, {}};
        r.edge = c.edge;
        // A point feature under the pointer moves the point to its projection.
        if (auto hit = index_.nearest(q_.ray, q_.radius, q_.occluder, q_.exclude)) {
            r.point = project(hit->featurePoint);
            r.featurePoint = r.point;
            if (glm::length(r.point - hit->featurePoint) > tol::kLengthEpsilon) {
                r.guides.emplace_back(hit->featurePoint, r.point, -1, GuideKind::Point);
            }
        } else if (c.type == SnapConstraint::Type::Line) {
            // An edge crossing the locked line near the pointer.
            if (auto e = index_.nearestEdge(q_.ray, q_.radius, q_.occluder, q_.exclude); e && e->edge) {
                const Vec3 u = e->edge->direction();
                if (const auto params = closestLineParameters(c.origin, c.direction, e->edge->a, u)) {
                    const Vec3 p = c.origin + c.direction * params->first;
                    const double v = params->second;
                    const bool onSegment = v >= -tol::kWeldDistance && v <= glm::length(e->edge->b - e->edge->a) + tol::kWeldDistance;
                    if (onSegment && glm::length(p - (e->edge->a + u * v)) <= kCrossing && near(p)) {
                        r.point = p;
                        r.featurePoint = p;
                        r.kind = SnapKind::Intersection;
                    }
                }
            }
        }
        if (c.type == SnapConstraint::Type::Line) {
            Line l{c.origin, c.direction, c.kind, c.axis, c.edge};
            if (c.kind == SnapKind::OnEdge) {
                if (c.edge) r.guides.emplace_back(c.edge->a, c.edge->b, -1, GuideKind::Reference);
            } else {
                lineGuides(l, r.point, r.guides);
            }
        }
        r.constraint = c;
        r.locked = true;
        return r;
    }

    const SnapIndex& index_;
    const InferenceQuery& q_;
    Vec3 dir_;
    std::vector<Line> lines_;
};

} // namespace

std::optional<std::pair<double, double>> closestLineParameters(const Vec3& o1, const Vec3& d1, const Vec3& o2, const Vec3& d2) {
    const double b = glm::dot(d1, d2);
    const double denom = 1.0 - b * b;
    if (denom <= 1e-12) return std::nullopt;
    const Vec3 w = o1 - o2;
    const double d = glm::dot(d1, w);
    const double e = glm::dot(d2, w);
    return std::pair{(b * e - d) / denom, (e - b * d) / denom};
}

std::optional<Vec3> pointOnConstraint(const SnapConstraint& c, const Ray& ray) {
    if (c.type == SnapConstraint::Type::Plane) return intersectPlane(ray, c.origin, c.direction);
    const auto t = closestParameterOnAxis(c.origin, c.direction, ray);
    if (!t) return std::nullopt;
    return c.origin + c.direction * *t;
}

std::optional<SnapResult> inferPoint(const SnapIndex& index, const InferenceQuery& query) { return Engine(index, query).run(); }

} // namespace pf
