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
    const Vec3 ax = std::abs(n.x) < 0.6 ? Vec3{1, 0, 0} : (std::abs(n.y) < 0.6 ? Vec3{0, 1, 0} : Vec3{0, 0, 1});
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

enum class Where { Outside, Boundary, Inside };

/// Where p is relative to a polygon with holes (even-odd over all loops);
/// within `eps` of any loop edge is the boundary.
Where locate(const std::vector<std::vector<Vec2>>& loops, const Vec2& p, double eps) {
    bool inside = false;
    for (const auto& loop : loops) {
        for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
            const Vec2& a = loop[i];
            const Vec2& b = loop[j];
            if (segmentDistance2d(p, a, b) <= eps) return Where::Boundary;
            if ((a.y > p.y) != (b.y > p.y)) {
                const double x = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
                if (p.x < x) inside = !inside;
            }
        }
    }
    return inside ? Where::Inside : Where::Outside;
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

// ---- Welded point pool -----------------------------------------------------------------

/// Every point of both solids and every cut point, welded within kWeld, so a
/// point shared by two faces has one index (and one exact position).
class PointPool {
public:
    std::uint32_t add(const Vec3& p) {
        const Key k = keyOf(p);
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                for (std::int64_t dz = -1; dz <= 1; ++dz) {
                    const auto it = grid_.find(Key{k.x + dx, k.y + dy, k.z + dz});
                    if (it == grid_.end()) continue;
                    for (const std::uint32_t i : it->second) {
                        if (glm::length(points[i] - p) <= kWeld) return i;
                    }
                }
            }
        }
        const auto index = static_cast<std::uint32_t>(points.size());
        points.push_back(p);
        grid_[k].push_back(index);
        return index;
    }

    std::vector<Vec3> points;

private:
    static constexpr double kCell = 2.0 * kWeld;
    struct Key {
        std::int64_t x, y, z;
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
    std::unordered_map<Key, std::vector<std::uint32_t>, KeyHash> grid_;
};

// ---- Faces of the two solids -------------------------------------------------------------

struct Segment {
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
        s.origin = s.loops.front().front();
        s.lo = s.hi = s.origin;
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

/// Adds to `f` the parts of g's edges that lie on f (g is in f's plane).
void imprintCoplanar(SourceFace& f, const SourceFace& g) {
    for (const auto& loop : g.loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vec3& a = loop[i];
            const Vec3& b = loop[(i + 1) % loop.size()];
            const Vec2 a2 = f.to2d(a);
            const Vec2 b2 = f.to2d(b);
            const Vec2 d = b2 - a2;
            const double len = glm::length(d);
            if (len <= kWeld) continue;
            std::vector<double> ts{0.0, 1.0};
            for (const auto& floop : f.loops2d) {
                for (std::size_t k = 0; k < floop.size(); ++k) {
                    const Vec2& p = floop[k];
                    const Vec2& q = floop[(k + 1) % floop.size()];
                    double t = 0.0;
                    if (segmentDistance2d(p, a2, b2, &t) <= kWeld) ts.push_back(t);
                    const Vec2 e = q - p;
                    const double den = cross2(d, e);
                    if (std::abs(den) <= 1e-12 * len * glm::length(e)) continue;
                    const double s = cross2(p - a2, e) / den;
                    const double r = cross2(p - a2, d) / den;
                    if (s > 0.0 && s < 1.0 && r >= 0.0 && r <= 1.0) ts.push_back(s);
                }
            }
            std::ranges::sort(ts);
            std::optional<double> start;
            double end = 0.0;
            const auto flush = [&] {
                if (start.has_value() && (end - *start) * len > kWeld) f.cuts.push_back({a + (b - a) * *start, a + (b - a) * end});
                start.reset();
            };
            for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
                const double t0 = std::clamp(ts[k], 0.0, 1.0);
                const double t1 = std::clamp(ts[k + 1], 0.0, 1.0);
                if ((t1 - t0) * len <= kWeld) continue;
                if (locate(f.loops2d, a2 + d * (0.5 * (t0 + t1)), kWeld) != Where::Outside) {
                    if (!start.has_value()) start = t0;
                    end = t1;
                } else {
                    flush();
                }
            }
            flush();
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

void intersectFaces(std::vector<SourceFace>& faces, std::size_t fi, std::size_t gi) {
    SourceFace& f = faces[fi];
    SourceFace& g = faces[gi];
    const Vec3 dir = glm::cross(f.normal, g.normal);
    const double len = glm::length(dir);
    if (len < 1e-9) {
        bool same = std::abs(g.distance(f.origin)) <= tol::kPlanarity;
        if (same) {
            for (const auto& loop : f.loops) {
                for (const auto& p : loop) same = same && std::abs(g.distance(p)) <= tol::kPlanarity;
            }
        }
        if (!same) return;
        f.coplanar.push_back(gi);
        g.coplanar.push_back(fi);
        imprintCoplanar(f, g);
        imprintCoplanar(g, f);
        return;
    }
    if (!straddles(f, g) || !straddles(g, f)) return;
    const Vec3 d = dir / len;
    // A point on both planes, near f's origin: f.normal . x = 0 and g.normal . x = h (relative to f.origin).
    const double c = glm::dot(f.normal, g.normal);
    const double h = glm::dot(g.origin - f.origin, g.normal);
    const Vec3 o = f.origin + (g.normal - f.normal * c) * (h / (1.0 - c * c));

    std::vector<double> ts;
    lineTouches(f, g, o, d, ts);
    lineTouches(g, f, o, d, ts);
    if (ts.size() < 2) return;
    std::ranges::sort(ts);
    std::optional<double> start;
    double end = 0.0;
    const auto flush = [&] {
        if (start.has_value() && end - *start > kWeld) {
            const Segment s{o + d * *start, o + d * end};
            f.cuts.push_back(s);
            g.cuts.push_back(s);
        }
        start.reset();
    };
    for (std::size_t k = 0; k + 1 < ts.size(); ++k) {
        if (ts[k + 1] - ts[k] <= kWeld) continue;
        const Vec3 mid = o + d * (0.5 * (ts[k] + ts[k + 1]));
        if (f.where(mid) != Where::Outside && g.where(mid) != Where::Outside) {
            if (!start.has_value()) start = ts[k];
            end = ts[k + 1];
        } else {
            flush();
        }
    }
    flush();
}

// ---- Splitting a face into regions -------------------------------------------------------

/// One piece of a source face: an outer loop and holes, as pool indices.
struct Region {
    std::size_t face = 0;
    std::vector<std::vector<std::uint32_t>> loops;
    Vec3 point{0.0}; ///< a point well inside the region, for classification
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

std::vector<Region> splitFace(const SourceFace& f, std::size_t faceIndex, PointPool& pool) {
    std::vector<Region> regions;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> segs;
    const auto addSeg = [&](const Vec3& a, const Vec3& b) {
        const std::uint32_t ia = pool.add(a);
        const std::uint32_t ib = pool.add(b);
        if (ia != ib) segs.emplace_back(ia, ib);
    };
    for (const auto& loop : f.loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) addSeg(loop[i], loop[(i + 1) % loop.size()]);
    }
    for (const auto& s : f.cuts) addSeg(s.a, s.b);

    std::map<std::uint32_t, Vec2> flat;
    const auto flatOf = [&](std::uint32_t i) {
        auto it = flat.find(i);
        if (it == flat.end()) it = flat.emplace(i, f.to2d(pool.points[i])).first;
        return it->second;
    };
    std::set<std::uint32_t> points;
    for (const auto& [a, b] : segs) {
        points.insert(a);
        points.insert(b);
    }

    // Crossings between segments become points of the arrangement.
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const Vec2 a = flatOf(segs[i].first);
        const Vec2 b = flatOf(segs[i].second);
        const Vec2 d1 = b - a;
        const double l1 = glm::length(d1);
        for (std::size_t j = i + 1; j < segs.size(); ++j) {
            const auto [c3, d3] = segs[j];
            if (c3 == segs[i].first || c3 == segs[i].second || d3 == segs[i].first || d3 == segs[i].second) continue;
            const Vec2 c = flatOf(c3);
            const Vec2 d = flatOf(d3);
            if (std::max(a.x, b.x) < std::min(c.x, d.x) - kWeld || std::max(c.x, d.x) < std::min(a.x, b.x) - kWeld ||
                std::max(a.y, b.y) < std::min(c.y, d.y) - kWeld || std::max(c.y, d.y) < std::min(a.y, b.y) - kWeld) {
                continue;
            }
            const Vec2 d2 = d - c;
            const double l2 = glm::length(d2);
            const double den = cross2(d1, d2);
            if (std::abs(den) <= 1e-12 * l1 * l2) continue; // parallel: overlaps are found as points on segments
            const double s = cross2(c - a, d2) / den;
            const double r = cross2(c - a, d1) / den;
            if (s * l1 <= kWeld || (1.0 - s) * l1 <= kWeld || r * l2 <= kWeld || (1.0 - r) * l2 <= kWeld) continue;
            const Vec3 pa = pool.points[segs[i].first];
            const Vec3 pb = pool.points[segs[i].second];
            points.insert(pool.add(pa + (pb - pa) * s));
        }
    }

    // Split every segment at the points on it.
    std::set<std::pair<std::uint32_t, std::uint32_t>> pieces;
    for (const auto& [ia, ib] : segs) {
        const Vec2 a = flatOf(ia);
        const Vec2 b = flatOf(ib);
        std::vector<std::pair<double, std::uint32_t>> on{{0.0, ia}, {1.0, ib}};
        for (const std::uint32_t k : points) {
            if (k == ia || k == ib) continue;
            double t = 0.0;
            if (segmentDistance2d(flatOf(k), a, b, &t) <= kWeld && t > 0.0 && t < 1.0) on.emplace_back(t, k);
        }
        std::ranges::sort(on);
        for (std::size_t k = 0; k + 1 < on.size(); ++k) {
            const std::uint32_t u = on[k].second;
            const std::uint32_t v = on[k + 1].second;
            if (u != v) pieces.insert(std::minmax(u, v));
        }
    }

    // Planar graph; edges with a free end cannot bound a region.
    std::map<std::uint32_t, std::vector<std::uint32_t>> adj;
    for (const auto& [u, v] : pieces) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
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
    for (auto& [v, ns] : adj) {
        const Vec2 pv = flatOf(v);
        std::ranges::sort(ns, [&](std::uint32_t x, std::uint32_t y) {
            const Vec2 dx = flatOf(x) - pv;
            const Vec2 dy = flatOf(y) - pv;
            return std::atan2(dx.y, dx.x) < std::atan2(dy.y, dy.x);
        });
    }

    // Trace every cycle with its region on the left.
    std::set<std::pair<std::uint32_t, std::uint32_t>> used;
    std::vector<std::vector<std::uint32_t>> cycles;
    for (const auto& [start, ns] : adj) {
        for (const std::uint32_t first : ns) {
            if (used.contains({start, first})) continue;
            std::vector<std::uint32_t> walk;
            std::uint32_t u = start;
            std::uint32_t v = first;
            bool ok = true;
            while (!used.contains({u, v})) {
                used.insert({u, v});
                walk.push_back(u);
                const auto& around = adj[v];
                const auto pos = std::ranges::find(around, u);
                if (pos == around.end()) {
                    ok = false;
                    break;
                }
                const auto index = static_cast<std::size_t>(pos - around.begin());
                const std::uint32_t w = around[(index + around.size() - 1) % around.size()];
                u = v;
                v = w;
            }
            if (ok && u == start && v == first) splitPinched(walk, cycles);
        }
    }

    // Counter-clockwise cycles bound regions; clockwise ones are holes in the
    // smallest region around them (or the outside of everything).
    struct Cycle {
        std::vector<std::uint32_t> ids;
        std::vector<Vec2> pts;
        double area = 0.0;
    };
    std::vector<Cycle> outers;
    std::vector<Cycle> holes;
    for (auto& ids : cycles) {
        Cycle c;
        for (const std::uint32_t i : ids) c.pts.push_back(flatOf(i));
        c.area = area2d(c.pts);
        c.ids = std::move(ids);
        if (std::abs(c.area) <= tol::kMinArea * 1e-3) continue;
        (c.area > 0.0 ? outers : holes).push_back(std::move(c));
    }
    std::vector<std::vector<std::size_t>> holesOf(outers.size());
    for (std::size_t h = 0; h < holes.size(); ++h) {
        std::optional<std::size_t> best;
        for (std::size_t o = 0; o < outers.size(); ++o) {
            if (-holes[h].area >= outers[o].area) continue;
            if (best && outers[o].area >= outers[*best].area) continue;
            bool inside = false;
            for (const auto& p : holes[h].pts) {
                const Where w = locate({outers[o].pts}, p, kWeld);
                if (w == Where::Boundary) continue;
                inside = w == Where::Inside;
                break;
            }
            if (inside) best = o;
        }
        if (best) holesOf[*best].push_back(h);
    }
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
        if (!inner || locate(f.loops2d, *inner, kWeld) != Where::Inside) continue; // outside the original face
        r.point = f.to3d(*inner);
        regions.push_back(std::move(r));
    }
    return regions;
}

// ---- Classification ---------------------------------------------------------------------

using Triangles = std::vector<std::array<Vec3, 3>>;

Triangles trianglesOf(const Mesh& m) {
    Triangles out;
    for (const auto& [id, f] : m.faces()) {
        for (const auto& t : triangulateFace(m, FaceId{id})) {
            out.push_back({m.vertex(t[0])->position, m.vertex(t[1])->position, m.vertex(t[2])->position});
        }
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
    flip = false;
    switch (op) {
    case BooleanOp::Union: return side == Side::Outside || (solid == 0 && side == Side::Same);
    case BooleanOp::Intersect: return side == Side::Inside || (solid == 0 && side == Side::Same);
    case BooleanOp::Subtract:
        if (solid == 1) {
            flip = true;
            return side == Side::Inside;
        }
        return side == Side::Outside || side == Side::Opposite;
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
            const Face& f1 = *m.face(FaceId{it->second[0]});
            const Face& f2 = *m.face(FaceId{it->second[1]});
            s.flatSplit = coplanarFaces(f1, f2, m.loopPositions(f1.loops[0]).front(), m.loopPositions(f2.loops[0]).front());
        }
        out.push_back(s);
    }
    return out;
}

/// The source edges an output edge lies along.
template <class Fn>
void forSourceEdges(const std::vector<SourceEdge>& edges, const Vec3& a, const Vec3& b, Fn fn) {
    for (const auto& s : edges) {
        if (segmentDistance3d(a, s.a, s.b) <= kWeld && segmentDistance3d(b, s.a, s.b) <= kWeld) fn(s);
    }
}

/// The kept regions as a mesh. Loops get every kept point that lies on one of
/// their edges (T-junctions where a neighbour was cut), so shared edges match.
Expected<Mesh> assemble(const std::vector<std::pair<Region, bool>>& kept, const std::vector<SourceFace>& faces, const PointPool& pool) {
    std::set<std::uint32_t> usedSet;
    for (const auto& [r, flip] : kept) {
        for (const auto& loop : r.loops) usedSet.insert(loop.begin(), loop.end());
    }
    std::vector<std::uint32_t> used(usedSet.begin(), usedSet.end());
    std::ranges::sort(used, [&](std::uint32_t x, std::uint32_t y) { return pool.points[x].x < pool.points[y].x; });

    Mesh out;
    std::map<std::uint32_t, VertexId> vertexOf;
    std::map<std::pair<std::uint32_t, std::uint32_t>, EdgeId> edgeOf;
    for (const auto& [r, flip] : kept) {
        Face face;
        face.normal = flip ? -faces[r.face].normal : faces[r.face].normal;
        std::set<std::uint32_t> visited;
        for (const auto& source : r.loops) {
            std::vector<std::uint32_t> loop;
            for (std::size_t i = 0; i < source.size(); ++i) {
                const std::uint32_t ia = source[i];
                const std::uint32_t ib = source[(i + 1) % source.size()];
                const Vec3& a = pool.points[ia];
                const Vec3& b = pool.points[ib];
                loop.push_back(ia);
                const double lo = std::min(a.x, b.x) - kWeld;
                const double hi = std::max(a.x, b.x) + kWeld;
                auto it = std::ranges::lower_bound(used, lo, {}, [&](std::uint32_t k) { return pool.points[k].x; });
                std::vector<std::pair<double, std::uint32_t>> on;
                for (; it != used.end() && pool.points[*it].x <= hi; ++it) {
                    if (*it == ia || *it == ib) continue;
                    double t = 0.0;
                    if (segmentDistance3d(pool.points[*it], a, b, &t) <= kWeld && t > 0.0 && t < 1.0) on.emplace_back(t, *it);
                }
                std::ranges::sort(on);
                for (const auto& [t, k] : on) loop.push_back(k);
            }
            if (flip) std::ranges::reverse(loop);
            Loop uses;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                if (!visited.insert(loop[i]).second) return Error{"a face of the result would touch itself at a corner"};
                for (const std::uint32_t k : {loop[i], loop[(i + 1) % loop.size()]}) {
                    if (!vertexOf.contains(k)) vertexOf[k] = out.appendVertex(pool.points[k]);
                }
                const VertexId va = vertexOf[loop[i]];
                const VertexId vb = vertexOf[loop[(i + 1) % loop.size()]];
                const auto key = std::minmax(loop[i], loop[(i + 1) % loop.size()]);
                auto e = edgeOf.find(key);
                if (e == edgeOf.end()) e = edgeOf.emplace(key, out.appendEdge(va, vb)).first;
                uses.push_back(EdgeUse{e->second, out.edge(e->second)->v0 != va});
            }
            face.loops.push_back(std::move(uses));
        }
        out.insertFace(FaceId{out.nextFaceId()}, face);
    }
    return out;
}

/// Gives `m` the edge flags of the source edges, merges coplanar faces across
/// edges that are not a split the user drew, and joins straight runs of two
/// edges whose shared vertex no longer is a corner.
Mesh cleanUp(Mesh m, const std::vector<SourceEdge>& sources) {
    // Flags: an output edge along a soft (smooth) source edge stays soft (smooth).
    std::set<std::uint32_t> keepEdges;
    for (const auto& [id, e] : m.edges()) {
        Edge changed = e;
        bool touched = false;
        forSourceEdges(sources, m.vertex(e.v0)->position, m.vertex(e.v1)->position, [&](const SourceEdge& s) {
            changed.soft = changed.soft || s.soft;
            changed.smooth = changed.smooth || s.smooth;
            if (s.flatSplit) keepEdges.insert(id);
            touched = true;
        });
        if (touched && changed != e) m.insertEdge(EdgeId{id}, changed);
    }

    // Merge coplanar neighbours one edge at a time; a merge eraseEdge cannot do
    // (the joined loop would not be simple) is undone and the edge kept.
    std::set<std::uint32_t> tried;
    for (bool merged = true; merged;) {
        merged = false;
        const auto uses = edgeFaces(m);
        for (const auto& [eid, fs] : uses) {
            if (fs.size() != 2 || fs[0] == fs[1] || keepEdges.contains(eid) || tried.contains(eid)) continue;
            const Face& f1 = *m.face(FaceId{fs[0]});
            const Face& f2 = *m.face(FaceId{fs[1]});
            if (!coplanarFaces(f1, f2, m.loopPositions(f1.loops[0]).front(), m.loopPositions(f2.loops[0]).front())) continue;
            // eraseEdge absorbs an inner face into the face it is a hole of; that is
            // only right when the inner face fills the whole hole.
            const auto loopWith = [eid](const Face& f) {
                for (std::size_t l = 0; l < f.loops.size(); ++l) {
                    if (std::ranges::any_of(f.loops[l], [eid](const EdgeUse& u) { return u.edge.value == eid; })) return l;
                }
                return std::size_t{0};
            };
            const std::size_t l1 = loopWith(f1);
            const std::size_t l2 = loopWith(f2);
            if (l1 != 0 && l2 != 0) continue;
            if ((l1 == 0) != (l2 == 0)) {
                const Loop& hole = l1 == 0 ? f2.loops[l2] : f1.loops[l1];
                const Loop& inner = l1 == 0 ? f1.loops[0] : f2.loops[0];
                std::set<std::uint32_t> he;
                std::set<std::uint32_t> ie;
                for (const auto& u : hole) he.insert(u.edge.value);
                for (const auto& u : inner) ie.insert(u.edge.value);
                if (he != ie) continue;
            }
            const Mesh before = m;
            const auto result = m.eraseEdge(EdgeId{eid});
            if (!result || !result->mergedFace) {
                m = before;
                tried.insert(eid);
                continue;
            }
            merged = true;
            break;
        }
    }

    // A merge drops the back-and-forth spurs of other edges the two faces
    // shared, which leaves those edges unused: remove them.
    {
        const auto uses = edgeFaces(m);
        std::vector<std::uint32_t> loose;
        for (const auto& [id, e] : m.edges()) {
            if (!uses.contains(id)) loose.push_back(id);
        }
        if (!loose.empty()) {
            Mesh kept;
            std::set<std::uint32_t> live;
            for (const auto& [id, e] : m.edges()) {
                if (std::ranges::find(loose, id) != loose.end()) continue;
                kept.insertEdge(EdgeId{id}, e);
                live.insert(e.v0.value);
                live.insert(e.v1.value);
            }
            for (const std::uint32_t v : live) kept.insertVertex(VertexId{v}, m.vertex(VertexId{v})->position);
            for (const auto& [id, f] : m.faces()) kept.insertFace(FaceId{id}, f);
            kept.reserveIds(m.nextVertexId(), m.nextEdgeId(), m.nextFaceId(), m.nextCurveId());
            m = std::move(kept);
        }
    }

    // A vertex in the middle of a straight run of two edges is not a corner
    // of anything (the cut or merge that made it is gone): join the edges.
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
        const Vec3 b = m.vertex(e2.v0.value == vid ? e2.v1 : e2.v0)->position;
        if (segmentDistance3d(p, a, b) > kWeld * 1e-3) continue;
        drop.insert(vid);
    }
    if (drop.empty()) return m;

    Mesh out;
    std::map<std::uint32_t, VertexId> vertexOf;
    std::map<std::pair<std::uint32_t, std::uint32_t>, EdgeId> edgeOf;
    for (const auto& [fid, f] : m.faces()) {
        Face face;
        face.normal = f.normal;
        for (const auto& loop : f.loops) {
            // Kept vertices, each with the flags of the edge that leaves it along the loop
            // (a joined edge takes the flags of the edges it replaces; they match).
            std::vector<std::pair<std::uint32_t, const Edge*>> vs;
            for (const auto& use : loop) {
                const VertexId v = m.useStart(use);
                if (!drop.contains(v.value)) vs.emplace_back(v.value, m.edge(use.edge));
            }
            if (vs.size() < 3) return m;
            Loop uses;
            for (std::size_t i = 0; i < vs.size(); ++i) {
                const std::uint32_t a = vs[i].first;
                const std::uint32_t b = vs[(i + 1) % vs.size()].first;
                for (const std::uint32_t k : {a, b}) {
                    if (!vertexOf.contains(k)) vertexOf[k] = out.appendVertex(m.vertex(VertexId{k})->position);
                }
                const auto key = std::minmax(a, b);
                auto e = edgeOf.find(key);
                if (e == edgeOf.end()) {
                    e = edgeOf.emplace(key, out.appendEdge(vertexOf.at(a), vertexOf.at(b))).first;
                    Edge flagged = *out.edge(e->second);
                    flagged.soft = vs[i].second->soft;
                    flagged.smooth = vs[i].second->smooth;
                    out.insertEdge(e->second, flagged);
                }
                uses.push_back(EdgeUse{e->second, out.edge(e->second)->v0 != vertexOf[a]});
            }
            face.loops.push_back(std::move(uses));
        }
        out.insertFace(FaceId{out.nextFaceId()}, face);
    }
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
                auto& count = uses[use.edge.value];
                (use.reversed ? count.second : count.first) += 1;
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

    std::vector<SourceFace> faces = sourceFaces(a, 0);
    const std::size_t countA = faces.size();
    for (auto& f : sourceFaces(b, 1)) faces.push_back(std::move(f));

    for (std::size_t i = 0; i < countA; ++i) {
        for (std::size_t j = countA; j < faces.size(); ++j) {
            if (boxesOverlap(faces[i].lo, faces[i].hi, faces[j].lo, faces[j].hi)) intersectFaces(faces, i, j);
        }
    }

    PointPool pool;
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
        for (auto& r : splitFace(faces[i], i, pool)) {
            const int solid = faces[i].solid;
            bool flip = false;
            if (keep(op, solid, classify(r, faces, solid == 0 ? trisB : trisA), flip)) kept.emplace_back(std::move(r), flip);
        }
    }
    if (kept.empty()) return Mesh{};

    auto built = assemble(kept, faces, pool);
    if (!built) return Error{"The result could not be built as a solid: " + built.error() + "."};

    std::vector<SourceEdge> edges = sourceEdges(a);
    for (auto& e : sourceEdges(b)) edges.push_back(e);
    Mesh result = cleanUp(std::move(built).value(), edges);
    // Face normals as files derive them (from the outer loop), so a saved and
    // reloaded result is the same mesh.
    const auto faceList = result.faces(); // a copy: the loop replaces faces
    for (const auto& [id, f] : faceList) {
        const Vec3 n = loopNormal(result.loopPositions(f.loops[0]));
        if (glm::length(n) == 0.0 || glm::dot(n, f.normal) <= 0.0) continue;
        Face refit = f;
        refit.normal = n;
        result.insertFace(FaceId{id}, refit);
    }

    if (auto e = validateMesh(result, "result")) return Error{"The result could not be built as a valid mesh (" + *e + ")."};
    if (auto p = solidProblem(result)) return Error{"The result would not be a solid: " + *p + "."};
    return result;
}

Mesh outerShells(const Mesh& m) {
    // Shells: faces connected through shared edges.
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
    std::map<std::uint32_t, std::vector<std::uint32_t>> shells;
    for (const auto& [fid, f] : m.faces()) shells[find(fid)].push_back(fid);
    if (shells.size() < 2) return m;

    std::map<std::uint32_t, Triangles> tris;
    for (const auto& [root, fs] : shells) {
        for (const std::uint32_t fid : fs) {
            for (const auto& t : triangulateFace(m, FaceId{fid})) {
                tris[root].push_back({m.vertex(t[0])->position, m.vertex(t[1])->position, m.vertex(t[2])->position});
            }
        }
    }
    std::set<std::uint32_t> dropFaces;
    for (const auto& [root, fs] : shells) {
        const auto& t = tris[root].front();
        const Vec3 probe = (t[0] + t[1] + t[2]) / 3.0;
        for (const auto& [other, ot] : tris) {
            if (other == root) continue;
            if (std::abs(winding(ot, probe)) > 0.5) {
                dropFaces.insert(fs.begin(), fs.end());
                break;
            }
        }
    }
    if (dropFaces.empty()) return m;

    Mesh out;
    std::set<std::uint32_t> keepEdges;
    std::set<std::uint32_t> keepVertices;
    for (const auto& [fid, f] : m.faces()) {
        if (dropFaces.contains(fid)) continue;
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

} // namespace pf::mesh
