#include "core/geometry/solid_boolean.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pf::mesh {

namespace {

constexpr double kWeld = tol::kWeldDistance;

// ---- Small geometry helpers ----------------------------------------------------------

/// In-plane axes with u x v = n, so a loop counter-clockwise about n has
/// positive area in (u, v).
struct Axes {
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
};

Axes axesFor(const Vec3& n) {
    Vec3 ax{0.0, 0.0, 1.0};
    if (std::abs(n.x) < 0.6) {
        ax = Vec3{1.0, 0.0, 0.0};
    } else if (std::abs(n.y) < 0.6) {
        ax = Vec3{0.0, 1.0, 0.0};
    }
    const Vec3 u = glm::normalize(glm::cross(ax, n));
    return {u, glm::cross(n, u)};
}

double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

double segmentDistance2d(const Vec2& p, const Vec2& a, const Vec2& b, double* param = nullptr) {
    const Vec2 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    const double t = len2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    if (param) *param = len2 > 0.0 ? glm::dot(p - a, ab) / len2 : 0.0;
    return glm::length(p - (a + ab * t));
}

double segmentDistance3d(const Vec3& p, const Vec3& a, const Vec3& b, double* param = nullptr) {
    const Vec3 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    const double raw = len2 > 0.0 ? glm::dot(p - a, ab) / len2 : 0.0;
    if (param) *param = raw;
    return glm::length(p - (a + ab * std::clamp(raw, 0.0, 1.0)));
}

double area2d(const std::vector<Vec2>& loop) {
    double a = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) a += cross2(loop[i], loop[(i + 1) % loop.size()]);
    return 0.5 * a;
}

/// Whether segment a-b crosses the ray from p in +x (half-open in y).
bool crossesRay(const Vec2& a, const Vec2& b, const Vec2& p) {
    if ((a.y > p.y) == (b.y > p.y)) return false;
    return p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
}

enum class Where { Outside, Boundary, Inside };

/// Where p is relative to a polygon with holes (even-odd over all loops);
/// within `eps` of any loop edge is the boundary.
Where locate(const std::vector<std::vector<Vec2>>& loops, const Vec2& p, double eps) {
    using enum Where;
    bool inside = false;
    for (const auto& loop : loops) {
        for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
            if (segmentDistance2d(p, loop[i], loop[j]) <= eps) return Boundary;
            if (crossesRay(loop[i], loop[j], p)) inside = !inside;
        }
    }
    return inside ? Inside : Outside;
}

/// A point well inside a polygon with holes: the middle of the widest span of
/// a horizontal line through the widest gap between vertex heights.
std::optional<Vec2> interiorPoint(const std::vector<std::vector<Vec2>>& loops) {
    std::vector<double> ys;
    for (const auto& loop : loops) {
        for (const auto& p : loop) ys.push_back(p.y);
    }
    std::ranges::sort(ys);
    double bestGap = 0.0;
    double y = 0.0;
    for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
        if (ys[i + 1] - ys[i] > bestGap) {
            bestGap = ys[i + 1] - ys[i];
            y = 0.5 * (ys[i] + ys[i + 1]);
        }
    }
    if (!(bestGap > 0.0)) return std::nullopt;
    std::vector<double> xs;
    for (const auto& loop : loops) {
        for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
            const Vec2& a = loop[i];
            const Vec2& b = loop[j];
            if ((a.y > y) != (b.y > y)) xs.push_back((b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x);
        }
    }
    std::ranges::sort(xs);
    double bestWidth = 0.0;
    std::optional<Vec2> best;
    for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
        if (xs[i + 1] - xs[i] > bestWidth) {
            bestWidth = xs[i + 1] - xs[i];
            best = Vec2{0.5 * (xs[i] + xs[i + 1]), y};
        }
    }
    return best;
}

/// Parameter runs between consecutive sorted `ts` whose middles pass `inside`,
/// joined where they touch; a run shorter than kWeld (in units of `scale`) is dropped.
template <class Inside>
std::vector<std::pair<double, double>> insideRuns(const std::vector<double>& ts, double scale, Inside inside) {
    std::vector<std::pair<double, double>> runs;
    std::optional<double> start;
    double end = 0.0;
    const auto flush = [&runs, &start, &end, scale] {
        if (start.has_value() && (end - *start) * scale > kWeld) runs.emplace_back(*start, end);
        start.reset();
    };
    for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
        if ((ts[k + 1] - ts[k]) * scale <= kWeld) continue;
        if (inside(0.5 * (ts[k] + ts[k + 1]))) {
            if (!start.has_value()) start = ts[k];
            end = ts[k + 1];
        } else {
            flush();
        }
    }
    flush();
    return runs;
}

// ---- Welded point pool -----------------------------------------------------------------

/// Every point of both solids and every cut point, welded within kWeld, so a
/// point shared by two faces has one index (and one exact position).
class PointPool {
public:
    std::uint32_t add(const Vec3& p) {
        const Key k = keyOf(p);
        for (std::int64_t n = 0; n < 27; ++n) {
            if (const auto i = findIn(Key{k.x + n % 3 - 1, k.y + n / 3 % 3 - 1, k.z + n / 9 - 1}, p); i.has_value()) return *i;
        }
        const auto index = static_cast<std::uint32_t>(points_.size());
        points_.push_back(p);
        grid_[k].push_back(index);
        return index;
    }

    [[nodiscard]] const Vec3& point(std::uint32_t i) const { return points_[i]; }

private:
    static constexpr double kCell = 2.0 * kWeld;
    struct Key {
        std::int64_t x;
        std::int64_t y;
        std::int64_t z;
        friend bool operator==(const Key&, const Key&) = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            const auto h = static_cast<std::uint64_t>(k.x) * 73856093ULL ^ static_cast<std::uint64_t>(k.y) * 19349663ULL ^
                           static_cast<std::uint64_t>(k.z) * 83492791ULL;
            return static_cast<std::size_t>(h);
        }
    };
    static Key keyOf(const Vec3& p) {
        return {static_cast<std::int64_t>(std::floor(p.x / kCell)), static_cast<std::int64_t>(std::floor(p.y / kCell)),
                static_cast<std::int64_t>(std::floor(p.z / kCell))};
    }
    [[nodiscard]] std::optional<std::uint32_t> findIn(const Key& cell, const Vec3& p) const {
        const auto it = grid_.find(cell);
        if (it == grid_.end()) return std::nullopt;
        for (const std::uint32_t i : it->second) {
            if (glm::length(points_[i] - p) <= kWeld) return i;
        }
        return std::nullopt;
    }

    std::vector<Vec3> points_;
    std::unordered_map<Key, std::vector<std::uint32_t>, KeyHash> grid_;
};

// ---- Faces of the two solids -------------------------------------------------------------

struct Segment {
    Segment(const Vec3& from, const Vec3& to) : a(from), b(to) {}
    Vec3 a;
    Vec3 b;
};

struct SourceFace {
    int solid = 0; ///< 0 = a, 1 = b
    Vec3 normal{0.0};
    Vec3 origin{0.0};
    Axes axes;
    std::vector<std::vector<Vec3>> loops;   ///< outer loop counter-clockwise about normal, then holes
    std::vector<std::vector<Vec2>> loops2d; ///< the same in (u, v) about origin
    Vec3 lo{0.0};
    Vec3 hi{0.0};
    std::vector<Segment> cuts;           ///< where the other solid's faces cross this one
    std::vector<std::size_t> coplanar;   ///< faces of the other solid in the same plane

    [[nodiscard]] Vec2 to2d(const Vec3& p) const {
        const Vec3 d = p - origin;
        return {glm::dot(d, axes.u), glm::dot(d, axes.v)};
    }
    [[nodiscard]] Vec3 to3d(const Vec2& p) const { return origin + axes.u * p.x + axes.v * p.y; }
    [[nodiscard]] double distance(const Vec3& p) const { return glm::dot(p - origin, normal); }
    [[nodiscard]] Where where(const Vec3& p) const { return locate(loops2d, to2d(p), kWeld); }
};

std::vector<SourceFace> sourceFaces(const Mesh& m, int solid) {
    std::vector<SourceFace> out;
    for (const auto& [id, f] : m.faces()) {
        SourceFace s;
        s.solid = solid;
        s.normal = f.normal;
        s.axes = axesFor(f.normal);
        for (const auto& loop : f.loops) s.loops.push_back(m.loopPositions(loop));
        if (s.loops.empty() || s.loops.front().empty()) continue; // not in a validated solid
        s.origin = s.loops.front().front();
        s.lo = s.origin;
        s.hi = s.origin;
        for (const auto& loop : s.loops) {
            std::vector<Vec2> flat;
            for (const auto& p : loop) {
                flat.push_back(s.to2d(p));
                s.lo = glm::min(s.lo, p);
                s.hi = glm::max(s.hi, p);
            }
            s.loops2d.push_back(std::move(flat));
        }
        out.push_back(std::move(s));
    }
    return out;
}

bool boxesOverlap(const Vec3& alo, const Vec3& ahi, const Vec3& blo, const Vec3& bhi) {
    return alo.x <= bhi.x + kWeld && blo.x <= ahi.x + kWeld && alo.y <= bhi.y + kWeld && blo.y <= ahi.y + kWeld &&
           alo.z <= bhi.z + kWeld && blo.z <= ahi.z + kWeld;
}

/// Parameters along a2-b2 (0 at a2, 1 at b2) where it meets the boundary of
/// `face`: its ends, the face's corners on it, and crossings with its edges.
std::vector<double> boundaryParams(const SourceFace& face, const Vec2& a2, const Vec2& b2) {
    const Vec2 d = b2 - a2;
    const double len = glm::length(d);
    std::vector ts{0.0, 1.0};
    for (const auto& loop : face.loops2d) {
        for (std::size_t k = 0; k < loop.size(); ++k) {
            const Vec2& p = loop[k];
            const Vec2& q = loop[(k + 1) % loop.size()];
            if (double t = 0.0; segmentDistance2d(p, a2, b2, &t) <= kWeld) ts.push_back(t);
            const Vec2 e = q - p;
            const double den = cross2(d, e);
            if (std::abs(den) <= 1e-12 * len * glm::length(e)) continue;
            const double s = cross2(p - a2, e) / den;
            if (const double r = cross2(p - a2, d) / den; s > 0.0 && s < 1.0 && r >= 0.0 && r <= 1.0) ts.push_back(s);
        }
    }
    std::ranges::sort(ts);
    for (double& t : ts) t = std::clamp(t, 0.0, 1.0);
    return ts;
}

/// Adds to `onto` the parts of `from`'s edges that lie on it (`from` is in its plane).
void imprintCoplanar(SourceFace& onto, const SourceFace& from) {
    for (const auto& loop : from.loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vec3& a = loop[i];
            const Vec3& b = loop[(i + 1) % loop.size()];
            const Vec2 a2 = onto.to2d(a);
            const Vec2 b2 = onto.to2d(b);
            const double len = glm::length(b2 - a2);
            if (len <= kWeld) continue;
            const auto onFace = [&onto, a2, b2](double t) { return locate(onto.loops2d, a2 + (b2 - a2) * t, kWeld) != Where::Outside; };
            for (const auto& [t0, t1] : insideRuns(boundaryParams(onto, a2, b2), len, onFace)) {
                onto.cuts.emplace_back(a + (b - a) * t0, a + (b - a) * t1);
            }
        }
    }
}

/// Parameters along the line O + t*d where face's boundary meets plane's plane.
void lineTouches(const SourceFace& face, const SourceFace& plane, const Vec3& o, const Vec3& d, std::vector<double>& ts) {
    for (const auto& loop : face.loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vec3& p = loop[i];
            const Vec3& q = loop[(i + 1) % loop.size()];
            const double dp = plane.distance(p);
            const double dq = plane.distance(q);
            if (std::abs(dp) <= kWeld) {
                ts.push_back(glm::dot(p - o, d));
            } else if ((dp > kWeld && dq < -kWeld) || (dp < -kWeld && dq > kWeld)) {
                const Vec3 x = p + (q - p) * (dp / (dp - dq));
                ts.push_back(glm::dot(x - o, d));
            }
        }
    }
}

/// Signed distances of a face's points to another face's plane straddle zero.
bool straddles(const SourceFace& face, const SourceFace& plane) {
    double lo = 0.0;
    double hi = 0.0;
    bool first = true;
    for (const auto& loop : face.loops) {
        for (const auto& p : loop) {
            const double dist = plane.distance(p);
            lo = first ? dist : std::min(lo, dist);
            hi = first ? dist : std::max(hi, dist);
            first = false;
        }
    }
    return lo <= kWeld && hi >= -kWeld;
}

/// Every point of `face` lies in the plane of `plane`.
bool inPlane(const SourceFace& face, const SourceFace& plane) {
    if (std::abs(plane.distance(face.origin)) > tol::kPlanarity) return false;
    return std::ranges::all_of(face.loops, [&plane](const std::vector<Vec3>& loop) {
        return std::ranges::all_of(loop, [&plane](const Vec3& p) { return std::abs(plane.distance(p)) <= tol::kPlanarity; });
    });
}

void intersectFaces(std::vector<SourceFace>& faces, std::size_t fi, std::size_t gi) {
    SourceFace& f = faces[fi];
    SourceFace& g = faces[gi];
    const Vec3 dir = glm::cross(f.normal, g.normal);
    const double len = glm::length(dir);
    if (len < 1e-9) {
        if (!inPlane(f, g)) return;
        f.coplanar.push_back(gi);
        g.coplanar.push_back(fi);
        imprintCoplanar(f, g);
        imprintCoplanar(g, f);
        return;
    }
    if (!straddles(f, g) || !straddles(g, f)) return;
    const Vec3 d = dir / len;
    // A point on both planes, near f's origin: f.normal . x = 0 and g.normal . x = h
    // (relative to f.origin). Written with the cross product, which stays exact for
    // nearly parallel planes where 1 - dot^2 would cancel to zero.
    const double h = glm::dot(g.origin - f.origin, g.normal);
    const Vec3 o = f.origin + glm::cross(dir, f.normal) * (h / (len * len));

    std::vector<double> ts;
    lineTouches(f, g, o, d, ts);
    lineTouches(g, f, o, d, ts);
    if (ts.size() < 2) return;
    std::ranges::sort(ts);
    const auto onBoth = [&f, &g, o, d](double t) {
        const Vec3 p = o + d * t;
        return f.where(p) != Where::Outside && g.where(p) != Where::Outside;
    };
    for (const auto& [t0, t1] : insideRuns(ts, 1.0, onBoth)) {
        f.cuts.emplace_back(o + d * t0, o + d * t1);
        g.cuts.emplace_back(o + d * t0, o + d * t1);
    }
}

// ---- Splitting a face into regions -------------------------------------------------------

/// One piece of a source face: an outer loop and holes, as pool indices.
struct Region {
    std::size_t face = 0;
    std::vector<std::vector<std::uint32_t>> loops;
    Vec3 point{0.0}; ///< a point well inside the region, for classification
};

using PointPair = std::pair<std::uint32_t, std::uint32_t>;
using Graph = std::map<std::uint32_t, std::vector<std::uint32_t>>;

/// Pool points in the (u, v) plane of one face, computed once each.
class FlatPoints {
public:
    FlatPoints(const SourceFace& face, const PointPool& pool) : face_(&face), pool_(&pool) {}
    Vec2 operator()(std::uint32_t i) {
        const auto [it, added] = cache_.try_emplace(i);
        if (added) it->second = face_->to2d(pool_->point(i));
        return it->second;
    }

private:
    const SourceFace* face_;
    const PointPool* pool_;
    std::map<std::uint32_t, Vec2> cache_;
};

/// Splits a closed walk that visits a vertex more than once into simple loops.
void splitPinched(const std::vector<std::uint32_t>& walk, std::vector<std::vector<std::uint32_t>>& out) {
    std::vector<std::uint32_t> stack;
    std::unordered_map<std::uint32_t, std::size_t> at;
    for (const std::uint32_t v : walk) {
        if (const auto it = at.find(v); it != at.end()) {
            const std::size_t from = it->second;
            std::vector<std::uint32_t> sub(stack.begin() + static_cast<std::ptrdiff_t>(from), stack.end());
            for (std::size_t k = from + 1; k < stack.size(); ++k) at.erase(stack[k]);
            stack.resize(from + 1);
            if (sub.size() >= 3) out.push_back(std::move(sub));
            continue;
        }
        at[v] = stack.size();
        stack.push_back(v);
    }
    if (stack.size() >= 3) out.push_back(std::move(stack));
}

/// The face's boundary edges and cuts as pool segments.
std::vector<PointPair> faceSegments(const SourceFace& f, PointPool& pool) {
    std::vector<PointPair> segs;
    const auto addSeg = [&segs, &pool](const Vec3& a, const Vec3& b) {
        const std::uint32_t ia = pool.add(a);
        const std::uint32_t ib = pool.add(b);
        if (ia != ib) segs.emplace_back(ia, ib);
    };
    for (const auto& loop : f.loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) addSeg(loop[i], loop[(i + 1) % loop.size()]);
    }
    for (const auto& s : f.cuts) addSeg(s.a, s.b);
    return segs;
}

bool boxesApart2d(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    return std::max(a.x, b.x) < std::min(c.x, d.x) - kWeld || std::max(c.x, d.x) < std::min(a.x, b.x) - kWeld ||
           std::max(a.y, b.y) < std::min(c.y, d.y) - kWeld || std::max(c.y, d.y) < std::min(a.y, b.y) - kWeld;
}

/// Where a-b crosses c-d away from all four ends, as a parameter along a-b.
/// Parallel segments give none: their overlaps are found as points on segments.
std::optional<double> crossingParam(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    if (boxesApart2d(a, b, c, d)) return std::nullopt;
    const Vec2 d1 = b - a;
    const Vec2 d2 = d - c;
    const double l1 = glm::length(d1);
    const double l2 = glm::length(d2);
    const double den = cross2(d1, d2);
    if (std::abs(den) <= 1e-12 * l1 * l2) return std::nullopt;
    const double s = cross2(c - a, d2) / den;
    if (s * l1 <= kWeld || (1.0 - s) * l1 <= kWeld) return std::nullopt;
    if (const double r = cross2(c - a, d1) / den; r * l2 <= kWeld || (1.0 - r) * l2 <= kWeld) return std::nullopt;
    return s;
}

/// The ends of every segment and the crossings between segments.
std::set<std::uint32_t> arrangementPoints(const std::vector<PointPair>& segs, FlatPoints& flat, PointPool& pool) {
    std::set<std::uint32_t> points;
    for (const auto& [a, b] : segs) {
        points.insert(a);
        points.insert(b);
    }
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const auto [ia, ib] = segs[i];
        for (std::size_t j = i + 1; j < segs.size(); ++j) {
            const auto [ic, id] = segs[j];
            if (ic == ia || ic == ib || id == ia || id == ib) continue;
            if (const auto s = crossingParam(flat(ia), flat(ib), flat(ic), flat(id)); s.has_value()) {
                const Vec3 pa = pool.point(ia);
                const Vec3 pb = pool.point(ib);
                points.insert(pool.add(pa + (pb - pa) * *s));
            }
        }
    }
    return points;
}

/// Every segment split at the points on it.
std::set<PointPair> splitSegments(const std::vector<PointPair>& segs, const std::set<std::uint32_t>& points, FlatPoints& flat) {
    std::set<PointPair> pieces;
    for (const auto& [ia, ib] : segs) {
        const Vec2 a = flat(ia);
        const Vec2 b = flat(ib);
        std::vector<std::pair<double, std::uint32_t>> on{{0.0, ia}, {1.0, ib}};
        for (const std::uint32_t k : points) {
            if (k == ia || k == ib) continue;
            if (double t = 0.0; segmentDistance2d(flat(k), a, b, &t) <= kWeld && t > 0.0 && t < 1.0) on.emplace_back(t, k);
        }
        std::ranges::sort(on);
        for (std::size_t k = 0; k + 1 < on.size(); ++k) {
            const std::uint32_t u = on[k].second;
            const std::uint32_t v = on[k + 1].second;
            if (u != v) pieces.insert(std::minmax(u, v));
        }
    }
    return pieces;
}

/// Removes edges with a free end, which cannot bound a region.
void pruneSpurs(Graph& adj) {
    for (bool pruned = true; pruned;) {
        pruned = false;
        for (auto it = adj.begin(); it != adj.end();) {
            if (it->second.size() >= 2) {
                ++it;
                continue;
            }
            for (const std::uint32_t n : it->second) std::erase(adj[n], it->first);
            it = adj.erase(it);
            pruned = true;
        }
    }
}

/// The planar graph of the pieces without spurs, neighbours sorted by angle.
Graph planarGraph(const std::set<PointPair>& pieces, FlatPoints& flat) {
    Graph adj;
    for (const auto& [u, v] : pieces) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
    pruneSpurs(adj);
    for (auto& [v, ns] : adj) {
        const Vec2 pv = flat(v);
        std::ranges::sort(ns, [&flat, pv](std::uint32_t x, std::uint32_t y) {
            const Vec2 dx = flat(x) - pv;
            const Vec2 dy = flat(y) - pv;
            return std::atan2(dx.y, dx.x) < std::atan2(dy.y, dy.x);
        });
    }
    return adj;
}

/// The cycle that leaves `start` towards `first` and keeps its region on the
/// left; empty when the walk does not come back.
std::vector<std::uint32_t> walkCycle(const Graph& adj, std::set<PointPair>& used, std::uint32_t start, std::uint32_t first) {
    std::vector<std::uint32_t> walk;
    std::uint32_t u = start;
    std::uint32_t v = first;
    while (!used.contains({u, v})) {
        used.insert({u, v});
        walk.push_back(u);
        const auto at = adj.find(v);
        if (at == adj.end()) return {};
        const auto& around = at->second;
        const auto pos = std::ranges::find(around, u);
        if (pos == around.end()) return {};
        const auto index = static_cast<std::size_t>(pos - around.begin());
        const std::uint32_t w = around[(index + around.size() - 1) % around.size()];
        u = v;
        v = w;
    }
    if (u != start || v != first) return {};
    return walk;
}

/// Every cycle of the graph with its region on the left, as simple loops.
std::vector<std::vector<std::uint32_t>> traceCycles(const Graph& adj) {
    std::set<PointPair> used;
    std::vector<std::vector<std::uint32_t>> cycles;
    for (const auto& [start, ns] : adj) {
        for (const std::uint32_t first : ns) {
            if (!used.contains({start, first})) splitPinched(walkCycle(adj, used, start, first), cycles);
        }
    }
    return cycles;
}

struct Cycle {
    std::vector<std::uint32_t> ids;
    std::vector<Vec2> pts;
    double area = 0.0;
};

/// Whether `hole` lies inside `outer` (judged by its first point off outer's boundary).
bool cycleInside(const Cycle& hole, const Cycle& outer) {
    for (const auto& p : hole.pts) {
        if (const Where w = locate({outer.pts}, p, kWeld); w != Where::Boundary) return w == Where::Inside;
    }
    return false;
}

/// The smallest outer cycle around `hole`, if any.
std::optional<std::size_t> regionAround(const Cycle& hole, const std::vector<Cycle>& outers) {
    std::optional<std::size_t> best;
    for (std::size_t o = 0; o < outers.size(); ++o) {
        if (-hole.area >= outers[o].area) continue;
        if (best.has_value() && outers[o].area >= outers[*best].area) continue;
        if (cycleInside(hole, outers[o])) best = o;
    }
    return best;
}

/// Counter-clockwise cycles bound regions; clockwise ones are holes in the
/// smallest region around them (or the outside of everything). Regions outside
/// the original face are dropped.
std::vector<Region> regionsOf(const SourceFace& f, std::size_t faceIndex, std::vector<std::vector<std::uint32_t>> cycles, FlatPoints& flat) {
    std::vector<Cycle> outers;
    std::vector<Cycle> holes;
    for (auto& ids : cycles) {
        Cycle c;
        for (const std::uint32_t i : ids) c.pts.push_back(flat(i));
        c.area = area2d(c.pts);
        c.ids = std::move(ids);
        if (std::abs(c.area) <= tol::kMinArea * 1e-3) continue;
        (c.area > 0.0 ? outers : holes).push_back(std::move(c));
    }
    std::vector<std::vector<std::size_t>> holesOf(outers.size());
    for (std::size_t h = 0; h < holes.size(); ++h) {
        if (const auto o = regionAround(holes[h], outers); o.has_value()) holesOf[*o].push_back(h);
    }
    std::vector<Region> regions;
    for (std::size_t o = 0; o < outers.size(); ++o) {
        std::vector<std::vector<Vec2>> loops2d{outers[o].pts};
        Region r;
        r.face = faceIndex;
        r.loops.push_back(outers[o].ids);
        for (const std::size_t h : holesOf[o]) {
            loops2d.push_back(holes[h].pts);
            r.loops.push_back(holes[h].ids);
        }
        const auto inner = interiorPoint(loops2d);
        if (!inner.has_value() || locate(f.loops2d, *inner, kWeld) != Where::Inside) continue;
        r.point = f.to3d(*inner);
        regions.push_back(std::move(r));
    }
    return regions;
}

std::vector<Region> splitFace(const SourceFace& f, std::size_t faceIndex, PointPool& pool) {
    FlatPoints flat(f, pool);
    const std::vector<PointPair> segs = faceSegments(f, pool);
    const std::set<std::uint32_t> points = arrangementPoints(segs, flat, pool);
    const Graph adj = planarGraph(splitSegments(segs, points, flat), flat);
    return regionsOf(f, faceIndex, traceCycles(adj), flat);
}

// ---- Classification ---------------------------------------------------------------------

using Triangle = std::array<Vec3, 3>;
using Triangles = std::vector<Triangle>;

Triangle corners(const Mesh& m, const std::array<VertexId, 3>& t) {
    return {m.vertex(t[0])->position, m.vertex(t[1])->position, m.vertex(t[2])->position};
}

Triangles trianglesOf(const Mesh& m) {
    Triangles out;
    for (const auto& [id, f] : m.faces()) {
        for (const auto& t : triangulateFace(m, FaceId{id})) out.push_back(corners(m, t));
    }
    return out;
}

/// Generalised winding number of a closed outward surface about p: 1 inside, 0 outside.
double winding(const Triangles& tris, const Vec3& p) {
    double sum = 0.0;
    for (const auto& t : tris) {
        const Vec3 a = t[0] - p;
        const Vec3 b = t[1] - p;
        const Vec3 c = t[2] - p;
        const double la = glm::length(a);
        const double lb = glm::length(b);
        const double lc = glm::length(c);
        const double num = glm::dot(a, glm::cross(b, c));
        const double den = la * lb * lc + glm::dot(a, b) * lc + glm::dot(a, c) * lb + glm::dot(b, c) * la;
        sum += 2.0 * std::atan2(num, den);
    }
    return sum / (4.0 * std::numbers::pi);
}

enum class Side { Inside, Outside, Same, Opposite };

Side classify(const Region& r, const std::vector<SourceFace>& faces, const Triangles& other) {
    const SourceFace& f = faces[r.face];
    for (const std::size_t gi : f.coplanar) {
        const SourceFace& g = faces[gi];
        if (g.where(r.point) != Where::Outside) return glm::dot(f.normal, g.normal) > 0.0 ? Side::Same : Side::Opposite;
    }
    return winding(other, r.point) > 0.5 ? Side::Inside : Side::Outside;
}

bool keep(BooleanOp op, int solid, Side side, bool& flip) {
    using enum BooleanOp;
    using enum Side;
    flip = false;
    switch (op) {
    case Union: return side == Outside || (solid == 0 && side == Same);
    case Intersect: return side == Inside || (solid == 0 && side == Same);
    case Subtract:
        if (solid == 1) {
            flip = true;
            return side == Inside;
        }
        return side == Outside || side == Opposite;
    }
    return false;
}

// ---- Building and cleaning the result ------------------------------------------------------

struct SourceEdge {
    Vec3 a;
    Vec3 b;
    bool soft = false;
    bool smooth = false;
    bool flatSplit = false; ///< between two coplanar faces in its own solid: the user drew it, keep it
};

bool coplanarFaces(const Face& f1, const Face& f2, const Vec3& p1, const Vec3& p2) {
    return 1.0 - glm::dot(f1.normal, f2.normal) <= tol::kCoplanarNormal && std::abs(glm::dot(p2 - p1, f1.normal)) <= tol::kPlanarity;
}

bool coplanarFaces(const Mesh& m, const Face& f1, const Face& f2) {
    return coplanarFaces(f1, f2, m.loopPositions(f1.loops[0]).front(), m.loopPositions(f2.loops[0]).front());
}

/// Faces using each edge, without Mesh::facesOfEdge's scan per edge.
std::map<std::uint32_t, std::vector<std::uint32_t>> edgeFaces(const Mesh& m) {
    std::map<std::uint32_t, std::vector<std::uint32_t>> out;
    for (const auto& [fid, f] : m.faces()) {
        for (const auto& loop : f.loops) {
            for (const auto& use : loop) out[use.edge.value].push_back(fid);
        }
    }
    return out;
}

std::vector<SourceEdge> sourceEdges(const Mesh& m) {
    std::vector<SourceEdge> out;
    const auto uses = edgeFaces(m);
    for (const auto& [id, e] : m.edges()) {
        SourceEdge s{m.vertex(e.v0)->position, m.vertex(e.v1)->position, e.soft, e.smooth, false};
        if (const auto it = uses.find(id); it != uses.end() && it->second.size() == 2) {
            s.flatSplit = coplanarFaces(m, *m.face(FaceId{it->second[0]}), *m.face(FaceId{it->second[1]}));
        }
        out.push_back(s);
    }
    return out;
}

/// An output edge a-b lies along source edge s.
bool alongSource(const SourceEdge& s, const Vec3& a, const Vec3& b) {
    return segmentDistance3d(a, s.a, s.b) <= kWeld && segmentDistance3d(b, s.a, s.b) <= kWeld;
}

/// Builds a mesh from loops of point numbers: one vertex per number and one
/// edge per pair of numbers, created in order of first use.
class MeshBuilder {
public:
    VertexId vertex(std::uint32_t k, const Vec3& p) {
        if (const auto it = vertexOf_.find(k); it != vertexOf_.end()) return it->second;
        const VertexId v = mesh_.appendVertex(p);
        vertexOf_.try_emplace(k, v);
        return v;
    }

    /// The use of edge a-b running from a to b (both added with vertex() first);
    /// `created` is set when the edge is new.
    EdgeUse use(std::uint32_t a, std::uint32_t b, bool* created = nullptr) {
        const VertexId va = vertexOf_.at(a);
        const VertexId vb = vertexOf_.at(b);
        const PointPair key = std::minmax(a, b);
        auto it = edgeOf_.find(key);
        if (created) *created = it == edgeOf_.end();
        if (it == edgeOf_.end()) it = edgeOf_.try_emplace(key, mesh_.appendEdge(va, vb)).first;
        return EdgeUse{it->second, mesh_.edge(it->second)->v0 != va};
    }

    Mesh& mesh() { return mesh_; }

private:
    Mesh mesh_;
    std::map<std::uint32_t, VertexId> vertexOf_;
    std::map<PointPair, EdgeId> edgeOf_;
};

/// Points of `used` (sorted by x) strictly inside segment ia-ib, in order along it.
std::vector<std::uint32_t> pointsOnSegment(std::uint32_t ia, std::uint32_t ib, const std::vector<std::uint32_t>& used, const PointPool& pool) {
    const Vec3& a = pool.point(ia);
    const Vec3& b = pool.point(ib);
    const double lo = std::min(a.x, b.x) - kWeld;
    const double hi = std::max(a.x, b.x) + kWeld;
    std::vector<std::pair<double, std::uint32_t>> on;
    for (auto it = std::ranges::lower_bound(used, lo, {}, [&pool](std::uint32_t k) { return pool.point(k).x; });
         it != used.end() && pool.point(*it).x <= hi; ++it) {
        if (*it == ia || *it == ib) continue;
        if (double t = 0.0; segmentDistance3d(pool.point(*it), a, b, &t) <= kWeld && t > 0.0 && t < 1.0) on.emplace_back(t, *it);
    }
    std::ranges::sort(on);
    std::vector<std::uint32_t> out;
    for (const auto& [t, k] : on) out.push_back(k);
    return out;
}

/// A region loop with every used point that lies on one of its edges.
std::vector<std::uint32_t> loopWithJunctions(const std::vector<std::uint32_t>& source, const std::vector<std::uint32_t>& used,
                                             const PointPool& pool) {
    std::vector<std::uint32_t> loop;
    for (std::size_t i = 0; i < source.size(); ++i) {
        loop.push_back(source[i]);
        for (const std::uint32_t k : pointsOnSegment(source[i], source[(i + 1) % source.size()], used, pool)) loop.push_back(k);
    }
    return loop;
}

/// `loop` (pool indices) as a loop of `out`.
Loop builtLoop(MeshBuilder& out, const std::vector<std::uint32_t>& loop, const PointPool& pool) {
    Loop uses;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const std::uint32_t a = loop[i];
        const std::uint32_t b = loop[(i + 1) % loop.size()];
        out.vertex(a, pool.point(a));
        out.vertex(b, pool.point(b));
        uses.push_back(out.use(a, b));
    }
    return uses;
}

/// The kept regions as a mesh. Loops get every kept point that lies on one of
/// their edges (T-junctions where a neighbour was cut), so shared edges match.
Expected<Mesh> assemble(const std::vector<std::pair<Region, bool>>& kept, const std::vector<SourceFace>& faces, const PointPool& pool) {
    std::set<std::uint32_t> usedSet;
    for (const auto& [r, flip] : kept) {
        for (const auto& loop : r.loops) usedSet.insert(loop.begin(), loop.end());
    }
    std::vector<std::uint32_t> used(usedSet.begin(), usedSet.end());
    std::ranges::sort(used, [&pool](std::uint32_t x, std::uint32_t y) { return pool.point(x).x < pool.point(y).x; });

    MeshBuilder out;
    for (const auto& [r, flip] : kept) {
        Face face;
        face.normal = flip ? -faces[r.face].normal : faces[r.face].normal;
        std::set<std::uint32_t> visited;
        for (const auto& source : r.loops) {
            std::vector<std::uint32_t> loop = loopWithJunctions(source, used, pool);
            if (flip) std::ranges::reverse(loop);
            if (!std::ranges::all_of(loop, [&visited](std::uint32_t k) { return visited.insert(k).second; })) {
                return Error{"a face of the result would touch itself at a corner"};
            }
            face.loops.push_back(builtLoop(out, loop, pool));
        }
        out.mesh().insertFace(FaceId{out.mesh().nextFaceId()}, face);
    }
    return std::move(out.mesh());
}

/// Gives each edge of `m` the soft and smooth flags of the source edges it lies
/// along. Returns the edges along a split the user drew, which must stay.
std::set<std::uint32_t> applySourceFlags(Mesh& m, const std::vector<SourceEdge>& sources) {
    std::set<std::uint32_t> keepEdges;
    for (const auto& [id, e] : m.edges()) {
        Edge changed = e;
        bool touched = false;
        const Vec3 a = m.vertex(e.v0)->position;
        const Vec3 b = m.vertex(e.v1)->position;
        for (const auto& s : sources) {
            if (!alongSource(s, a, b)) continue;
            changed.soft = changed.soft || s.soft;
            changed.smooth = changed.smooth || s.smooth;
            if (s.flatSplit) keepEdges.insert(id);
            touched = true;
        }
        if (touched && changed != e) m.insertEdge(EdgeId{id}, changed);
    }
    return keepEdges;
}

/// Index of the loop of `f` that uses edge `eid` (0 when none does).
std::size_t loopWith(const Face& f, std::uint32_t eid) {
    for (std::size_t l = 0; l < f.loops.size(); ++l) {
        if (std::ranges::any_of(f.loops[l], [eid](const EdgeUse& u) { return u.edge.value == eid; })) return l;
    }
    return 0;
}

std::set<std::uint32_t> loopEdges(const Loop& loop) {
    std::set<std::uint32_t> out;
    for (const auto& u : loop) out.insert(u.edge.value);
    return out;
}

/// Whether erasing edge `eid` between faces `fs` may merge them: they are
/// coplanar, and eraseEdge absorbing an inner face into the face it is a hole
/// of is only right when the inner face fills the whole hole.
bool mergeable(const Mesh& m, std::uint32_t eid, const std::vector<std::uint32_t>& fs) {
    const Face& f1 = *m.face(FaceId{fs[0]});
    const Face& f2 = *m.face(FaceId{fs[1]});
    if (!coplanarFaces(m, f1, f2)) return false;
    const std::size_t l1 = loopWith(f1, eid);
    const std::size_t l2 = loopWith(f2, eid);
    if (l1 != 0 && l2 != 0) return false;
    if (l1 == 0 && l2 == 0) return true;
    const Loop& hole = l1 == 0 ? f2.loops[l2] : f1.loops[l1];
    const Loop& inner = l1 == 0 ? f1.loops[0] : f2.loops[0];
    return loopEdges(hole) == loopEdges(inner);
}

/// Erases `eid` when that merges two faces; otherwise leaves `m` as it was.
bool eraseMerging(Mesh& m, std::uint32_t eid) {
    Mesh before = m;
    if (const auto result = m.eraseEdge(EdgeId{eid}); result.has_value() && result->mergedFace.has_value()) return true;
    m = std::move(before);
    return false;
}

/// Merges coplanar neighbours one edge at a time; a merge eraseEdge cannot do
/// (the joined loop would not be simple) is undone and the edge kept.
void mergeCoplanar(Mesh& m, const std::set<std::uint32_t>& keepEdges) {
    std::set<std::uint32_t> tried;
    for (bool merged = true; merged;) {
        merged = false;
        for (const auto& [eid, fs] : edgeFaces(m)) {
            if (fs.size() != 2 || fs[0] == fs[1] || keepEdges.contains(eid) || tried.contains(eid)) continue;
            if (!mergeable(m, eid, fs)) continue;
            if (eraseMerging(m, eid)) {
                merged = true;
                break;
            }
            tried.insert(eid);
        }
    }
}

/// A merge drops the back-and-forth spurs of other edges the two faces
/// shared, which leaves those edges unused: remove them.
void removeLooseEdges(Mesh& m) {
    const auto uses = edgeFaces(m);
    if (std::ranges::all_of(m.edges(), [&uses](const auto& entry) { return uses.contains(entry.first); })) return;
    Mesh kept;
    std::set<std::uint32_t> live;
    for (const auto& [id, e] : m.edges()) {
        if (!uses.contains(id)) continue;
        kept.insertEdge(EdgeId{id}, e);
        live.insert(e.v0.value);
        live.insert(e.v1.value);
    }
    for (const std::uint32_t v : live) kept.insertVertex(VertexId{v}, m.vertex(VertexId{v})->position);
    for (const auto& [id, f] : m.faces()) kept.insertFace(FaceId{id}, f);
    kept.reserveIds(m.nextVertexId(), m.nextEdgeId(), m.nextFaceId(), m.nextCurveId());
    m = std::move(kept);
}

/// Vertices in the middle of a straight run of two edges with the same flags:
/// not a corner of anything (the cut or merge that made them is gone).
std::set<std::uint32_t> straightRunVertices(const Mesh& m) {
    std::map<std::uint32_t, std::vector<std::uint32_t>> edgesAt;
    for (const auto& [id, e] : m.edges()) {
        edgesAt[e.v0.value].push_back(id);
        edgesAt[e.v1.value].push_back(id);
    }
    std::set<std::uint32_t> drop;
    for (const auto& [vid, es] : edgesAt) {
        if (es.size() != 2) continue;
        const Edge& e1 = *m.edge(EdgeId{es[0]});
        const Edge& e2 = *m.edge(EdgeId{es[1]});
        if (e1.soft != e2.soft || e1.smooth != e2.smooth) continue;
        const Vec3 p = m.vertex(VertexId{vid})->position;
        const Vec3 a = m.vertex(e1.v0.value == vid ? e1.v1 : e1.v0)->position;
        if (const Vec3 b = m.vertex(e2.v0.value == vid ? e2.v1 : e2.v0)->position; segmentDistance3d(p, a, b) <= kWeld * 1e-3) {
            drop.insert(vid);
        }
    }
    return drop;
}

/// The vertices of `loop` not in `drop`, each with the edge that leaves it along the loop.
std::vector<std::pair<std::uint32_t, const Edge*>> keptCorners(const Mesh& m, const Loop& loop, const std::set<std::uint32_t>& drop) {
    std::vector<std::pair<std::uint32_t, const Edge*>> vs;
    for (const auto& use : loop) {
        if (const VertexId v = m.useStart(use); !drop.contains(v.value)) vs.emplace_back(v.value, m.edge(use.edge));
    }
    return vs;
}

/// The kept corners as a loop of `out`. A joined edge takes the flags of the
/// edges it replaces (they match), here those of the edge leaving its start.
Loop joinedLoop(MeshBuilder& out, const Mesh& m, const std::vector<std::pair<std::uint32_t, const Edge*>>& vs) {
    Loop uses;
    for (std::size_t i = 0; i < vs.size(); ++i) {
        const std::uint32_t a = vs[i].first;
        const std::uint32_t b = vs[(i + 1) % vs.size()].first;
        out.vertex(a, m.vertex(VertexId{a})->position);
        out.vertex(b, m.vertex(VertexId{b})->position);
        bool created = false;
        const EdgeUse use = out.use(a, b, &created);
        if (created) {
            Edge flagged = *out.mesh().edge(use.edge);
            flagged.soft = vs[i].second->soft;
            flagged.smooth = vs[i].second->smooth;
            out.mesh().insertEdge(use.edge, flagged);
        }
        uses.push_back(use);
    }
    return uses;
}

/// `m` with the `drop` vertices removed and the edges on either side of each
/// joined; `m` unchanged when a loop would be left with fewer than three corners.
Mesh withoutVertices(const Mesh& m, const std::set<std::uint32_t>& drop) {
    MeshBuilder out;
    for (const auto& [fid, f] : m.faces()) {
        Face face;
        face.normal = f.normal;
        for (const auto& loop : f.loops) {
            const auto vs = keptCorners(m, loop, drop);
            if (vs.size() < 3) return m;
            face.loops.push_back(joinedLoop(out, m, vs));
        }
        out.mesh().insertFace(FaceId{out.mesh().nextFaceId()}, face);
    }
    return std::move(out.mesh());
}

/// Gives `m` the edge flags of the source edges, merges coplanar faces across
/// edges that are not a split the user drew, and joins straight runs of two
/// edges whose shared vertex no longer is a corner.
Mesh cleanUp(Mesh m, const std::vector<SourceEdge>& sources) {
    const std::set<std::uint32_t> keepEdges = applySourceFlags(m, sources);
    mergeCoplanar(m, keepEdges);
    removeLooseEdges(m);
    const std::set<std::uint32_t> drop = straightRunVertices(m);
    if (drop.empty()) return m;
    return withoutVertices(m, drop);
}

/// The faces of both solids, each with the cuts where the other solid meets it.
std::vector<SourceFace> cutFaces(const Mesh& a, const Mesh& b) {
    std::vector<SourceFace> faces = sourceFaces(a, 0);
    const std::size_t countA = faces.size();
    for (auto& f : sourceFaces(b, 1)) faces.push_back(std::move(f));
    for (std::size_t i = 0; i < countA; ++i) {
        for (std::size_t j = countA; j < faces.size(); ++j) {
            if (boxesOverlap(faces[i].lo, faces[i].hi, faces[j].lo, faces[j].hi)) intersectFaces(faces, i, j);
        }
    }
    return faces;
}

/// The regions of the cut faces that `op` keeps, each with whether it is turned over.
std::vector<std::pair<Region, bool>> keptRegions(const std::vector<SourceFace>& faces, const Mesh& a, const Mesh& b, BooleanOp op,
                                                 PointPool& pool) {
    // Every original corner first, so cut points weld onto corners and not the other way round.
    for (const auto& f : faces) {
        for (const auto& loop : f.loops) {
            for (const auto& p : loop) pool.add(p);
        }
    }
    const Triangles trisA = trianglesOf(a);
    const Triangles trisB = trianglesOf(b);
    std::vector<std::pair<Region, bool>> kept;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const int solid = faces[i].solid;
        for (auto& r : splitFace(faces[i], i, pool)) {
            bool flip = false;
            if (keep(op, solid, classify(r, faces, solid == 0 ? trisB : trisA), flip)) kept.emplace_back(std::move(r), flip);
        }
    }
    return kept;
}

/// Face normals as files derive them (from the outer loop), so a saved and
/// reloaded result is the same mesh.
void refitNormals(Mesh& m) {
    const auto faceList = m.faces(); // a copy: the loop replaces faces
    for (const auto& [id, f] : faceList) {
        const Vec3 n = loopNormal(m.loopPositions(f.loops[0]));
        if (glm::length(n) == 0.0 || glm::dot(n, f.normal) <= 0.0) continue;
        Face refit = f;
        refit.normal = n;
        m.insertFace(FaceId{id}, refit);
    }
}

using Shells = std::map<std::uint32_t, std::vector<std::uint32_t>>;

/// Faces grouped into shells: faces connected through shared edges.
Shells shellsOf(const Mesh& m) {
    std::map<std::uint32_t, std::uint32_t> parent;
    const auto find = [&parent](std::uint32_t x) {
        while (parent.at(x) != x) {
            const std::uint32_t up = parent.at(parent.at(x));
            parent.at(x) = up;
            x = up;
        }
        return x;
    };
    for (const auto& [fid, f] : m.faces()) parent[fid] = fid;
    for (const auto& [eid, fs] : edgeFaces(m)) {
        for (std::size_t k = 1; k < fs.size(); ++k) parent[find(fs[k])] = find(fs[0]);
    }
    Shells shells;
    for (const auto& [fid, f] : m.faces()) shells[find(fid)].push_back(fid);
    return shells;
}

/// Faces of every shell that lies inside another shell.
std::set<std::uint32_t> innerShellFaces(const Mesh& m, const Shells& shells) {
    std::map<std::uint32_t, Triangles> tris;
    for (const auto& [root, fs] : shells) {
        for (const std::uint32_t fid : fs) {
            for (const auto& t : triangulateFace(m, FaceId{fid})) tris[root].push_back(corners(m, t));
        }
    }
    std::set<std::uint32_t> inner;
    for (const auto& [root, fs] : shells) {
        const auto& t = tris.at(root).front();
        const Vec3 probe = (t[0] + t[1] + t[2]) / 3.0;
        const bool enclosed = std::ranges::any_of(tris, [root, &probe](const auto& entry) {
            return entry.first != root && std::abs(winding(entry.second, probe)) > 0.5;
        });
        if (enclosed) inner.insert(fs.begin(), fs.end());
    }
    return inner;
}

/// `m` without the `drop` faces and the edges and vertices only they used.
Mesh withoutFaces(const Mesh& m, const std::set<std::uint32_t>& drop) {
    Mesh out;
    std::set<std::uint32_t> keepEdges;
    std::set<std::uint32_t> keepVertices;
    for (const auto& [fid, f] : m.faces()) {
        if (drop.contains(fid)) continue;
        for (const auto& loop : f.loops) {
            for (const auto& use : loop) {
                keepEdges.insert(use.edge.value);
                const Edge& e = *m.edge(use.edge);
                keepVertices.insert(e.v0.value);
                keepVertices.insert(e.v1.value);
            }
        }
        out.insertFace(FaceId{fid}, f);
    }
    for (const std::uint32_t v : keepVertices) out.insertVertex(VertexId{v}, m.vertex(VertexId{v})->position);
    for (const std::uint32_t e : keepEdges) {
        Edge edge = *m.edge(EdgeId{e});
        edge.curve.reset(); // curves are not carried over
        out.insertEdge(EdgeId{e}, edge);
    }
    out.reserveIds(m.nextVertexId(), m.nextEdgeId(), m.nextFaceId(), m.nextCurveId());
    return out;
}

} // namespace

// ---- Public functions ----------------------------------------------------------------------

std::optional<std::string> solidProblem(const Mesh& m) {
    if (m.faces().empty()) return std::string{"it has no faces"};
    std::map<std::uint32_t, std::pair<int, int>> uses; // forward, reversed
    for (const auto& [fid, f] : m.faces()) {
        for (const auto& loop : f.loops) {
            for (const auto& use : loop) {
                auto& [forward, reversed] = uses[use.edge.value];
                (use.reversed ? reversed : forward) += 1;
            }
        }
    }
    for (const auto& [id, e] : m.edges()) {
        const auto it = uses.find(id);
        if (it == uses.end()) return std::string{"it has loose edges"};
        const auto [forward, reversed] = it->second;
        if (forward + reversed == 1) return std::string{"its surface is not closed"};
        if (forward + reversed > 2) return std::string{"an edge is shared by more than two faces"};
        if (forward != 1 || reversed != 1) return std::string{"its faces are not oriented the same way"};
    }
    double volume = 0.0;
    for (const auto& t : trianglesOf(m)) volume += glm::dot(t[0], glm::cross(t[1], t[2])) / 6.0;
    if (!(volume > 0.0)) return std::string{"its faces point inward"};
    return std::nullopt;
}

MeshData solidMeshData(const Mesh& m) {
    MeshData data;
    data.closed = true;
    for (const auto& [id, f] : m.faces()) {
        const auto first = static_cast<std::uint32_t>(data.indices.size());
        for (const auto& t : triangulateFace(m, FaceId{id})) {
            for (const VertexId v : t) {
                data.indices.push_back(static_cast<std::uint32_t>(data.positions.size()));
                data.positions.push_back(m.vertex(v)->position);
                data.normals.push_back(f.normal);
            }
            data.triangleFaces.push_back(pf::FaceId::meshFace(id));
        }
        data.faces.push_back({pf::FaceId::meshFace(id), first, static_cast<std::uint32_t>(data.indices.size()) - first});
    }
    for (const auto& [id, e] : m.edges()) {
        data.edges.push_back(m.vertex(e.v0)->position);
        data.edges.push_back(m.vertex(e.v1)->position);
    }
    return data;
}

Mesh transformedMesh(const Mesh& m, const Mat3& r, const Vec3& t) {
    Mesh out;
    for (const auto& [id, v] : m.vertices()) out.insertVertex(VertexId{id}, r * v.position + t);
    for (const auto& [id, e] : m.edges()) out.insertEdge(EdgeId{id}, e);
    for (const auto& [id, f] : m.faces()) {
        Face moved = f;
        moved.normal = glm::normalize(r * f.normal);
        out.insertFace(FaceId{id}, moved);
    }
    for (const auto& [id, c] : m.curves()) {
        Curve moved = c;
        moved.center = r * c.center + t;
        moved.normal = r * c.normal;
        moved.xAxis = r * c.xAxis;
        out.insertCurve(CurveId{id}, moved);
    }
    out.reserveIds(m.nextVertexId(), m.nextEdgeId(), m.nextFaceId(), m.nextCurveId());
    return out;
}

Expected<Mesh> booleanSolids(const Mesh& a, const Mesh& b, BooleanOp op) {
    if (auto p = solidProblem(a)) return Error{"The first object is not a solid: " + *p + "."};
    if (auto p = solidProblem(b)) return Error{"The second object is not a solid: " + *p + "."};

    const std::vector<SourceFace> faces = cutFaces(a, b);
    PointPool pool;
    const auto kept = keptRegions(faces, a, b, op, pool);
    if (kept.empty()) return Mesh{};

    auto built = assemble(kept, faces, pool);
    if (!built) return Error{"The result could not be built as a solid: " + built.error() + "."};

    std::vector<SourceEdge> edges = sourceEdges(a);
    const std::vector<SourceEdge> edgesB = sourceEdges(b);
    edges.insert(edges.end(), edgesB.begin(), edgesB.end());
    Mesh result = cleanUp(std::move(built).value(), edges);
    refitNormals(result);

    if (auto e = validateMesh(result, "result")) return Error{"The result could not be built as a valid mesh (" + *e + ")."};
    if (auto p = solidProblem(result)) return Error{"The result would not be a solid: " + *p + "."};
    return result;
}

Mesh outerShells(const Mesh& m) {
    const Shells shells = shellsOf(m);
    if (shells.size() < 2) return m;
    const std::set<std::uint32_t> drop = innerShellFaces(m, shells);
    if (drop.empty()) return m;
    return withoutFaces(m, drop);
}

} // namespace pf::mesh
