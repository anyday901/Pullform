#include "core/geometry/mesh.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

namespace pf::mesh {

namespace {

constexpr double kPi = std::numbers::pi;

/// Orthonormal in-plane axes for a unit normal.
struct PlaneAxes {
    Vec3 u;
    Vec3 v;
};

PlaneAxes axesFor(const Vec3& n) {
    const Vec3 ax = std::abs(n.x) < 0.6 ? Vec3{1, 0, 0} : (std::abs(n.y) < 0.6 ? Vec3{0, 1, 0} : Vec3{0, 0, 1});
    const Vec3 u = glm::normalize(glm::cross(ax, n));
    return {u, glm::cross(n, u)};
}

Vec2 project(const PlaneAxes& axes, const Vec3& origin, const Vec3& p) {
    const Vec3 d = p - origin;
    return {glm::dot(d, axes.u), glm::dot(d, axes.v)};
}

double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

/// Distance from p to segment ab and the parameter of the closest point.
double segmentDistance(const Vec3& p, const Vec3& a, const Vec3& b, double& t) {
    const Vec3 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    t = len2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return glm::length(p - (a + ab * t));
}

double segmentDistance2d(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    const double t = len2 > 0.0 ? std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return glm::length(p - (a + ab * t));
}

/// Even-odd point in polygon.
bool insidePolygon(const Vec2& p, const std::vector<Vec2>& poly) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < x) inside = !inside;
        }
    }
    return inside;
}

bool onPolygon(const Vec2& p, const std::vector<Vec2>& poly, double eps) {
    for (std::size_t i = 0; i < poly.size(); ++i) {
        if (segmentDistance2d(p, poly[i], poly[(i + 1) % poly.size()]) <= eps) return true;
    }
    return false;
}

bool sameDirection(const Vec3& a, const Vec3& b) { return 1.0 - glm::dot(a, b) <= tol::kCoplanarNormal; }

} // namespace

// ---- Free functions ----------------------------------------------------------------

Vec3 loopNormal(const std::vector<Vec3>& loop) {
    Vec3 n{0.0};
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec3& a = loop[i];
        const Vec3& b = loop[(i + 1) % loop.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const double len = glm::length(n);
    return len > 0.0 ? n / len : Vec3{0.0};
}

double signedArea(const std::vector<Vec3>& loop, const Vec3& normal) {
    Vec3 sum{0.0};
    for (std::size_t i = 0; i < loop.size(); ++i) sum += glm::cross(loop[i], loop[(i + 1) % loop.size()]);
    return 0.5 * glm::dot(sum, normal);
}

// ---- Reading -----------------------------------------------------------------------

const Vertex* Mesh::vertex(VertexId id) const {
    const auto it = vertices_.find(id.value);
    return it == vertices_.end() ? nullptr : &it->second;
}
const Edge* Mesh::edge(EdgeId id) const {
    const auto it = edges_.find(id.value);
    return it == edges_.end() ? nullptr : &it->second;
}
const Face* Mesh::face(FaceId id) const {
    const auto it = faces_.find(id.value);
    return it == faces_.end() ? nullptr : &it->second;
}
const Curve* Mesh::curve(CurveId id) const {
    const auto it = curves_.find(id.value);
    return it == curves_.end() ? nullptr : &it->second;
}

VertexId Mesh::useStart(const EdgeUse& use) const {
    const Edge* e = edge(use.edge);
    if (!e) return {};
    return use.reversed ? e->v1 : e->v0;
}
VertexId Mesh::useEnd(const EdgeUse& use) const {
    const Edge* e = edge(use.edge);
    if (!e) return {};
    return use.reversed ? e->v0 : e->v1;
}

std::vector<VertexId> Mesh::loopVertices(const Loop& loop) const {
    std::vector<VertexId> out;
    out.reserve(loop.size());
    for (const auto& use : loop) out.push_back(useStart(use));
    return out;
}

std::vector<Vec3> Mesh::loopPositions(const Loop& loop) const {
    std::vector<Vec3> out;
    out.reserve(loop.size());
    for (const auto& use : loop) {
        const Vertex* v = vertex(useStart(use));
        out.push_back(v ? v->position : Vec3{0.0});
    }
    return out;
}

std::optional<EdgeId> Mesh::findEdge(VertexId a, VertexId b) const {
    for (const auto& [id, e] : edges_) {
        if ((e.v0 == a && e.v1 == b) || (e.v0 == b && e.v1 == a)) return EdgeId{id};
    }
    return std::nullopt;
}

std::vector<EdgeId> Mesh::edgesOfVertex(VertexId v) const {
    std::vector<EdgeId> out;
    for (const auto& [id, e] : edges_) {
        if (e.v0 == v || e.v1 == v) out.push_back(EdgeId{id});
    }
    return out;
}

std::vector<EdgeId> Mesh::edgesOfFace(FaceId id) const {
    std::vector<EdgeId> out;
    if (const Face* f = face(id)) {
        for (const auto& loop : f->loops) {
            for (const auto& use : loop) out.push_back(use.edge);
        }
    }
    return out;
}

std::vector<FaceId> Mesh::facesOfEdge(EdgeId id) const {
    std::vector<FaceId> out;
    for (const auto& [fid, f] : faces_) {
        bool uses = false;
        for (const auto& loop : f.loops) {
            for (const auto& use : loop) uses = uses || use.edge == id;
        }
        if (uses) out.push_back(FaceId{fid});
    }
    return out;
}

std::optional<VertexId> Mesh::findVertex(const Vec3& p) const {
    std::optional<VertexId> best;
    double bestDist = tol::kWeldDistance;
    for (const auto& [id, v] : vertices_) {
        const double d = glm::length(v.position - p);
        if (d <= bestDist) {
            bestDist = d;
            best = VertexId{id};
        }
    }
    return best;
}

// ---- Raw construction --------------------------------------------------------------

void Mesh::insertVertex(VertexId id, const Vec3& p) {
    vertices_[id.value] = Vertex{p};
    nextVertex_ = std::max(nextVertex_, id.value + 1);
}
void Mesh::insertEdge(EdgeId id, const Edge& e) {
    edges_[id.value] = e;
    nextEdge_ = std::max(nextEdge_, id.value + 1);
}
void Mesh::insertFace(FaceId id, const Face& f) {
    faces_[id.value] = f;
    nextFace_ = std::max(nextFace_, id.value + 1);
}
void Mesh::insertCurve(CurveId id, const Curve& c) {
    curves_[id.value] = c;
    nextCurve_ = std::max(nextCurve_, id.value + 1);
}
VertexId Mesh::appendVertex(const Vec3& p) {
    const VertexId id{nextVertex_};
    insertVertex(id, p);
    return id;
}
EdgeId Mesh::appendEdge(VertexId a, VertexId b) {
    const EdgeId id{nextEdge_};
    insertEdge(id, Edge{a, b, std::nullopt, false, false});
    return id;
}

Loop Mesh::makeLoop(const std::vector<VertexId>& vs) const {
    Loop loop;
    for (std::size_t i = 0; i < vs.size(); ++i) {
        const VertexId a = vs[i];
        const VertexId b = vs[(i + 1) % vs.size()];
        const auto e = findEdge(a, b);
        const Edge* found = e ? edge(*e) : nullptr;
        if (!found) return {};
        loop.push_back(EdgeUse{*e, found->v0 != a});
    }
    return loop;
}

std::optional<FaceId> Mesh::appendFace(const std::vector<std::vector<VertexId>>& loops, const Vec3& normal) {
    Face f;
    f.normal = normal;
    for (const auto& vs : loops) {
        Loop loop = makeLoop(vs);
        if (loop.empty()) return std::nullopt;
        f.loops.push_back(std::move(loop));
    }
    const FaceId id{nextFace_};
    insertFace(id, f);
    return id;
}

void Mesh::reserveIds(std::uint32_t v, std::uint32_t e, std::uint32_t f, std::uint32_t c) {
    nextVertex_ = std::max(nextVertex_, v);
    nextEdge_ = std::max(nextEdge_, e);
    nextFace_ = std::max(nextFace_, f);
    nextCurve_ = std::max(nextCurve_, c);
}

// ---- Modeling ----------------------------------------------------------------------

void Mesh::splitEdge(EdgeId id, VertexId at) {
    Edge& e = edges_.at(id.value);
    const VertexId b = e.v1;
    Edge second = e;
    second.v0 = at;
    second.v1 = b;
    e.v1 = at;
    const EdgeId secondId{nextEdge_};
    insertEdge(secondId, second);
    for (auto& [fid, f] : faces_) {
        for (auto& loop : f.loops) {
            for (std::size_t i = 0; i < loop.size(); ++i) {
                if (loop[i].edge != id) continue;
                if (!loop[i].reversed) {
                    loop.insert(loop.begin() + static_cast<std::ptrdiff_t>(i) + 1, EdgeUse{secondId, false});
                } else {
                    loop[i] = EdgeUse{secondId, true};
                    loop.insert(loop.begin() + static_cast<std::ptrdiff_t>(i) + 1, EdgeUse{id, true});
                }
                ++i;
            }
        }
    }
    if (second.curve) {
        if (auto it = curves_.find(second.curve->value); it != curves_.end()) {
            auto& list = it->second.edges;
            const auto pos = std::ranges::find(list, id);
            if (pos != list.end()) list.insert(pos + 1, secondId);
            ++it->second.segments;
        }
    }
}

VertexId Mesh::weldVertex(const Vec3& p) {
    if (const auto v = findVertex(p)) return *v;
    for (const auto& [id, e] : edges_) {
        double t = 0.0;
        const Vec3 a = vertices_.at(e.v0.value).position;
        const Vec3 b = vertices_.at(e.v1.value).position;
        if (segmentDistance(p, a, b, t) <= tol::kWeldDistance) {
            const Vec3 onEdge = a + (b - a) * t;
            const VertexId v = appendVertex(onEdge);
            splitEdge(EdgeId{id}, v);
            return v;
        }
    }
    return appendVertex(p);
}

namespace {

/// Walks the edges in `left` from `at`, one shared vertex to the next, and
/// returns them in order; edges it cannot reach stay in `left`.
std::vector<EdgeId> walkChain(const Mesh& m, std::optional<VertexId> at, std::set<std::uint32_t>& left) {
    std::vector<EdgeId> chain;
    const auto& edges = m.edges();
    while (at.has_value() && !left.empty()) {
        const VertexId here = *at;
        const auto next = std::ranges::find_if(left, [&edges, here](std::uint32_t e) {
            const auto it = edges.find(e);
            return it != edges.end() && (it->second.v0 == here || it->second.v1 == here);
        });
        if (next == left.end()) break;
        const Edge& edge = edges.at(*next);
        at = edge.v0 == here ? edge.v1 : edge.v0;
        chain.push_back(EdgeId{*next});
        left.erase(next);
    }
    return chain;
}

} // namespace

std::optional<AddEdgeResult> Mesh::addCurve(Curve curve, const std::vector<Vec3>& points, CurveId* id) {
    if (points.size() < 2) return std::nullopt;
    std::set<std::uint32_t> facesBefore;
    for (const auto& [fid, f] : faces_) facesBefore.insert(fid);
    std::set<std::uint32_t> left;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (const auto r = addEdge(points[i], points[i + 1])) {
            for (const EdgeId e : r->edges) left.insert(e.value);
        }
    }
    std::erase_if(left, [this](std::uint32_t e) { return !edges_.contains(e); });
    if (left.empty()) return std::nullopt;

    // Walk the drawn pieces from the first point, so the curve's edges are in
    // order even when a later segment split an earlier one.
    AddEdgeResult result;
    result.edges = walkChain(*this, findVertex(points.front()), left);
    for (const auto& [fid, f] : faces_) {
        if (!facesBefore.contains(fid)) result.createdFaces.push_back(FaceId{fid});
    }
    for (const auto fid : facesBefore) {
        if (!faces_.contains(fid)) result.removedFaces.push_back(FaceId{fid});
    }

    // Record the curve only over a clean chain of new, untagged edges (a curve
    // drawn over an existing curve leaves both as they were).
    if (left.empty() && std::ranges::none_of(result.edges, [this](EdgeId e) { return edges_.at(e.value).curve.has_value(); })) {
        const CurveId cid{nextCurve_};
        curve.edges = result.edges;
        curve.segments = static_cast<int>(result.edges.size());
        insertCurve(cid, curve);
        for (const EdgeId e : result.edges) edges_.at(e.value).curve = cid;
        if (id) *id = cid;
    }
    return result;
}

std::optional<AddEdgeResult> Mesh::addEdge(const Vec3& a, const Vec3& b) {
    if (glm::length(b - a) <= tol::kWeldDistance) return std::nullopt;
    const VertexId va = weldVertex(a);
    const VertexId vb = weldVertex(b);
    if (va == vb) return std::nullopt;
    const Vec3 pa = vertices_.at(va.value).position;
    const Vec3 pb = vertices_.at(vb.value).position;
    const Vec3 dir = pb - pa;
    const double len = glm::length(dir);

    // Points along the new edge where it must be split.
    std::vector<std::pair<double, VertexId>> stops;
    for (const auto& [id, v] : vertices_) {
        if (id == va.value || id == vb.value) continue;
        double t = 0.0;
        if (segmentDistance(v.position, pa, pb, t) <= tol::kWeldDistance && t * len > tol::kWeldDistance &&
            (1.0 - t) * len > tol::kWeldDistance) {
            stops.emplace_back(t, VertexId{id});
        }
    }
    // Existing edges the new one crosses (closest points within the weld
    // distance, strictly inside both segments).
    struct Crossing {
        EdgeId edge;
        double t;
        Vec3 point;
    };
    std::vector<Crossing> crossings;
    for (const auto& [id, e] : edges_) {
        if (e.v0 == va || e.v1 == va || e.v0 == vb || e.v1 == vb) continue;
        const Vec3 c = vertices_.at(e.v0.value).position;
        const Vec3 d = vertices_.at(e.v1.value).position;
        const Vec3 u = dir;
        const Vec3 w = d - c;
        const Vec3 r = pa - c;
        const double A = glm::dot(u, u);
        const double B = glm::dot(u, w);
        const double C = glm::dot(w, w);
        const double D = glm::dot(u, r);
        const double E = glm::dot(w, r);
        const double den = A * C - B * B;
        if (den <= 1e-12 * A * C) continue; // parallel: overlaps are handled by the vertex stops
        const double s = (B * E - C * D) / den;
        const double t = (A * E - B * D) / den;
        if (s <= 0.0 || s >= 1.0 || t <= 0.0 || t >= 1.0) continue;
        const Vec3 p1 = pa + u * s;
        const Vec3 p2 = c + w * t;
        if (glm::length(p1 - p2) > tol::kWeldDistance) continue;
        const double wl = std::sqrt(C);
        if (s * len <= tol::kWeldDistance || (1 - s) * len <= tol::kWeldDistance) continue;
        if (t * wl <= tol::kWeldDistance || (1 - t) * wl <= tol::kWeldDistance) continue;
        crossings.push_back({EdgeId{id}, s, (p1 + p2) * 0.5});
    }
    for (const auto& c : crossings) {
        const VertexId v = appendVertex(c.point);
        splitEdge(c.edge, v);
        stops.emplace_back(c.t, v);
    }
    std::ranges::sort(stops, [](const auto& x, const auto& y) { return x.first < y.first; });

    std::vector<VertexId> chain{va};
    for (const auto& [t, v] : stops) {
        if (chain.back() != v) chain.push_back(v);
    }
    if (chain.back() != vb) chain.push_back(vb);

    AddEdgeResult result;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        if (const auto e = connect(chain[i], chain[i + 1], result)) result.edges.push_back(*e);
    }
    return result;
}

std::optional<EdgeId> Mesh::connect(VertexId a, VertexId b, AddEdgeResult& result) {
    if (const auto existing = findEdge(a, b)) return existing;
    const EdgeId id = appendEdge(a, b);
    faceEdge(id, result);
    return id;
}

std::optional<std::vector<EdgeUse>> Mesh::traceLeft(const EdgeUse& start, const Vec3& n, const Vec3& origin,
                                                    const std::vector<EdgeId>& allowed) const {
    const PlaneAxes axes = axesFor(n);
    const auto pos2 = [&](VertexId v) { return project(axes, origin, vertices_.at(v.value).position); };
    std::vector<EdgeUse> cycle{start};
    std::set<std::uint32_t> visited{useStart(start).value};
    EdgeUse current = start;
    for (std::size_t guard = 0; guard <= allowed.size() + 1; ++guard) {
        const VertexId from = useStart(current);
        const VertexId at = useEnd(current);
        const Vec2 back = pos2(from) - pos2(at);
        std::optional<EdgeUse> best;
        double bestAngle = std::numeric_limits<double>::infinity();
        for (const EdgeId eid : allowed) {
            if (eid == current.edge) continue;
            const Edge& e = edges_.at(eid.value);
            if (e.v0 != at && e.v1 != at) continue;
            const EdgeUse use{eid, e.v0 != at};
            const Vec2 out = pos2(useEnd(use)) - pos2(at);
            double ccw = std::atan2(cross2(back, out), glm::dot(back, out));
            if (ccw <= 0.0) ccw += 2.0 * kPi;
            const double cw = 2.0 * kPi - ccw; // clockwise turn from the way we came
            if (cw < bestAngle) {
                bestAngle = cw;
                best = use;
            }
        }
        if (!best) return std::nullopt; // dead end
        if (*best == start) return cycle;
        const VertexId next = useStart(*best);
        if (next != at) return std::nullopt;
        if (!visited.insert(useEnd(*best).value).second && useEnd(*best) != useStart(start)) return std::nullopt;
        cycle.push_back(*best);
        current = *best;
    }
    return std::nullopt;
}

namespace {

/// Removes edges hanging off the allowed set (vertices with one allowed edge),
/// so traces never wander into dead ends.
std::vector<EdgeId> pruneDangling(const Mesh& m, std::vector<EdgeId> edges) {
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::uint32_t, int> degree;
        for (const EdgeId e : edges) {
            ++degree[m.edge(e)->v0.value];
            ++degree[m.edge(e)->v1.value];
        }
        std::vector<EdgeId> kept;
        for (const EdgeId e : edges) {
            if (degree[m.edge(e)->v0.value] < 2 || degree[m.edge(e)->v1.value] < 2) {
                changed = true;
            } else {
                kept.push_back(e);
            }
        }
        edges = std::move(kept);
    }
    return edges;
}

struct FaceRegion {
    PlaneAxes axes;
    Vec3 origin;
    std::vector<std::vector<Vec2>> loops;
};

FaceRegion regionOf(const Mesh& m, const Face& f) {
    FaceRegion r{axesFor(f.normal), m.loopPositions(f.loops[0]).front(), {}};
    for (const auto& loop : f.loops) {
        std::vector<Vec2> pts;
        for (const auto& p : m.loopPositions(loop)) pts.push_back(project(r.axes, r.origin, p));
        r.loops.push_back(std::move(pts));
    }
    return r;
}

bool onPlane(const Mesh& m, const Face& f, const Vec3& p) {
    const Vec3 o = m.loopPositions(f.loops[0]).front();
    return std::abs(glm::dot(p - o, f.normal)) <= tol::kPlanarity;
}

enum class Where { Outside, Boundary, Inside };

Where classify(const Mesh& m, const Face& f, const Vec3& p) {
    if (!onPlane(m, f, p)) return Where::Outside;
    const FaceRegion r = regionOf(m, f);
    const Vec2 q = project(r.axes, r.origin, p);
    for (const auto& loop : r.loops) {
        if (onPolygon(q, loop, tol::kWeldDistance)) return Where::Boundary;
    }
    if (!insidePolygon(q, r.loops[0])) return Where::Outside;
    for (std::size_t i = 1; i < r.loops.size(); ++i) {
        if (insidePolygon(q, r.loops[i])) return Where::Outside;
    }
    return Where::Inside;
}

/// A point strictly inside a face (centroid of its largest triangle).
std::optional<Vec3> interiorPoint(const Mesh& m, const Face& f) {
    Mesh tmp;
    // Triangulate through a throwaway mesh holding just this face.
    for (const auto& loop : f.loops) {
        for (const auto& use : loop) {
            const Edge* e = m.edge(use.edge);
            const Vertex* v0 = e ? m.vertex(e->v0) : nullptr;
            const Vertex* v1 = e ? m.vertex(e->v1) : nullptr;
            if (!v0 || !v1) return std::nullopt; // the face refers to a missing element
            tmp.insertVertex(e->v0, v0->position);
            tmp.insertVertex(e->v1, v1->position);
            tmp.insertEdge(use.edge, *e);
        }
    }
    tmp.insertFace(FaceId{1}, f);
    const auto tris = triangulateFace(tmp, FaceId{1});
    double best = 0.0;
    std::optional<Vec3> point;
    for (const auto& t : tris) {
        const Vertex* va = tmp.vertex(t[0]);
        const Vertex* vb = tmp.vertex(t[1]);
        const Vertex* vc = tmp.vertex(t[2]);
        if (!va || !vb || !vc) continue;
        const Vec3 a = va->position;
        const Vec3 b = vb->position;
        const Vec3 c = vc->position;
        const double area = glm::length(glm::cross(b - a, c - a));
        if (area > best) {
            best = area;
            point = (a + b + c) / 3.0;
        }
    }
    return point;
}

bool simpleLoop(const Mesh& m, const Loop& loop) {
    std::set<std::uint32_t> seen;
    for (const auto v : m.loopVertices(loop)) {
        if (!seen.insert(v.value).second) return false;
    }
    return loop.size() >= 3;
}

Loop reversedLoop(const Loop& loop) {
    Loop out;
    for (auto it = loop.rbegin(); it != loop.rend(); ++it) out.push_back(EdgeUse{it->edge, !it->reversed});
    return out;
}

bool sameEdgeSet(const Loop& a, const Loop& b) {
    if (a.size() != b.size()) return false;
    std::set<std::uint32_t> sa;
    std::set<std::uint32_t> sb;
    for (const auto& u : a) sa.insert(u.edge.value);
    for (const auto& u : b) sb.insert(u.edge.value);
    return sa == sb;
}

bool loopTouches(const Mesh& m, const Loop& a, const Loop& b) {
    const auto va = m.loopVertices(a);
    const auto vb = m.loopVertices(b);
    const std::set<VertexId> sb(vb.begin(), vb.end());
    return std::ranges::any_of(va, [&](VertexId v) { return sb.contains(v); });
}

/// True when every vertex of `inner` is strictly inside the outer loop `outer`
/// (projected on the plane of `normal`).
bool loopInside(const Mesh& m, const Loop& inner, const Loop& outer, const Vec3& normal) {
    const PlaneAxes axes = axesFor(normal);
    const Vec3 o = m.loopPositions(outer).front();
    std::vector<Vec2> poly;
    for (const auto& p : m.loopPositions(outer)) poly.push_back(project(axes, o, p));
    for (const auto& p : m.loopPositions(inner)) {
        const Vec2 q = project(axes, o, p);
        if (onPolygon(q, poly, tol::kWeldDistance) || !insidePolygon(q, poly)) return false;
    }
    return true;
}

} // namespace

void Mesh::faceEdge(EdgeId id, AddEdgeResult& result) {
    const Edge e = edges_.at(id.value);
    const Vec3 pa = vertices_.at(e.v0.value).position;
    const Vec3 pb = vertices_.at(e.v1.value).position;
    const Vec3 mid = (pa + pb) * 0.5;
    const EdgeUse forward{id, false};
    const EdgeUse backward{id, true};

    // Case 1: the edge runs across an existing face, so it may split it or close
    // an island inside it.
    // Usually one face holds the edge; in a mesh whose faces cross (before
    // Intersect Faces cuts them) an edge on the crossing line lies in two, and
    // each is split on its own.
    std::vector<std::uint32_t> under;
    for (const auto& [fidValue, f] : faces_) {
        if (classify(*this, f, mid) != Where::Inside) continue;
        if (classify(*this, f, pa) == Where::Outside || classify(*this, f, pb) == Where::Outside) continue;
        under.push_back(fidValue);
    }
    for (const std::uint32_t fidValue : under) {
        if (!faces_.contains(fidValue)) continue;
        const Face f = faces_.at(fidValue);
        const FaceId fid{fidValue};

        std::vector<EdgeId> allowed;
        for (const auto& [eid, other] : edges_) {
            const Vec3 a = vertices_.at(other.v0.value).position;
            const Vec3 b = vertices_.at(other.v1.value).position;
            if (classify(*this, f, a) == Where::Outside || classify(*this, f, b) == Where::Outside) continue;
            if (classify(*this, f, (a + b) * 0.5) == Where::Outside) continue;
            allowed.push_back(EdgeId{eid});
        }
        allowed = pruneDangling(*this, allowed);
        if (std::ranges::find(allowed, id) == allowed.end()) continue; // dangling: no new face

        const Vec3 origin = loopPositions(f.loops[0]).front();
        std::vector<Loop> cycles;
        for (const EdgeUse& start : {forward, backward}) {
            auto c = traceLeft(start, f.normal, origin, allowed);
            if (c && simpleLoop(*this, *c) && signedArea(loopPositions(*c), f.normal) > tol::kMinArea * 0.5) {
                cycles.push_back(*c);
            }
        }
        if (cycles.size() == 2 && !sameEdgeSet(cycles[0], cycles[1])) {
            // Split: two faces replace f; each hole goes to the face containing it.
            Face f1{{cycles[0]}, f.normal};
            Face f2{{cycles[1]}, f.normal};
            bool holeTouches = false;
            for (std::size_t h = 1; h < f.loops.size() && !holeTouches; ++h) {
                holeTouches = loopTouches(*this, f.loops[h], cycles[0]) || loopTouches(*this, f.loops[h], cycles[1]);
            }
            if (holeTouches) continue;
            for (std::size_t h = 1; h < f.loops.size(); ++h) {
                const Loop outer = reversedLoop(f.loops[h]);
                (loopInside(*this, outer, cycles[0], f.normal) ? f1 : f2).loops.push_back(f.loops[h]);
            }
            faces_.erase(fidValue);
            const FaceId id1{nextFace_};
            insertFace(id1, f1);
            const FaceId id2{nextFace_};
            insertFace(id2, f2);
            result.removedFaces.push_back(fid);
            result.createdFaces.push_back(id1);
            result.createdFaces.push_back(id2);
            continue;
        }
        if (cycles.size() == 1) {
            // An island: a closed loop inside f that does not touch its boundary.
            const Loop& c = cycles[0];
            if (std::ranges::any_of(f.loops, [&](const Loop& loop) { return loopTouches(*this, loop, c); })) continue;
            Face inner{{c}, f.normal};
            Face outer{{f.loops[0]}, f.normal};
            for (std::size_t h = 1; h < f.loops.size(); ++h) {
                if (loopInside(*this, reversedLoop(f.loops[h]), c, f.normal)) {
                    inner.loops.push_back(f.loops[h]);
                } else {
                    outer.loops.push_back(f.loops[h]);
                }
            }
            outer.loops.push_back(reversedLoop(c));
            faces_[fidValue] = outer;
            const FaceId innerId{nextFace_};
            insertFace(innerId, inner);
            result.createdFaces.push_back(innerId);
        }
    }
    if (!under.empty()) return;

    // Case 2: no face under the edge. Try every plane the edge spans with a
    // neighbouring edge and close the smallest loop on each side.
    const Vec3 dir = pb - pa;
    std::vector<Vec3> normals;
    for (const VertexId end : {e.v0, e.v1}) {
        for (const EdgeId other : edgesOfVertex(end)) {
            if (other == id) continue;
            const Edge& o = edges_.at(other.value);
            const Vec3 d = vertices_.at(o.v1.value).position - vertices_.at(o.v0.value).position;
            Vec3 n = glm::cross(dir, d);
            const double len = glm::length(n);
            if (len <= 1e-12 * glm::length(dir) * glm::length(d)) continue;
            n /= len;
            // Prefer +Z (then +Y, +X) so horizontal faces face up.
            const Vec3 pref{1e-3, 1e-2, 1.0};
            if (glm::dot(n, pref) < 0.0) n = -n;
            if (std::ranges::none_of(normals, [&](const Vec3& m) { return std::abs(glm::dot(m, n)) >= 1.0 - tol::kCoplanarNormal; })) {
                normals.push_back(n);
            }
        }
    }

    for (const Vec3& n : normals) {
        std::vector<EdgeId> allowed;
        for (const auto& [eid, other] : edges_) {
            const Vec3 a = vertices_.at(other.v0.value).position;
            const Vec3 b = vertices_.at(other.v1.value).position;
            if (std::abs(glm::dot(a - pa, n)) <= tol::kPlanarity && std::abs(glm::dot(b - pa, n)) <= tol::kPlanarity) {
                allowed.push_back(EdgeId{eid});
            }
        }
        allowed = pruneDangling(*this, allowed);
        if (std::ranges::find(allowed, id) == allowed.end()) continue;
        for (const EdgeUse& start : {forward, backward}) {
            auto c = traceLeft(start, n, pa, allowed);
            if (!c || !simpleLoop(*this, *c)) continue;
            if (signedArea(loopPositions(*c), n) <= tol::kMinArea * 0.5) continue;
            const bool exists = std::ranges::any_of(faces_, [&](const auto& kv) {
                return std::ranges::any_of(kv.second.loops, [&](const Loop& l) { return sameEdgeSet(l, *c); });
            });
            if (exists) continue;

            Face candidate{{*c}, n};
            // Coplanar faces lying wholly inside the new loop become its holes
            // (only the outermost ones: a face inside one of those stays as is).
            std::vector<FaceId> enclosed;
            for (const auto& [ofid, of] : faces_) {
                if (std::abs(glm::dot(of.normal, n)) < 1.0 - tol::kCoplanarNormal || !onPlane(*this, of, pa)) continue;
                if (loopTouches(*this, of.loops[0], *c) || !loopInside(*this, of.loops[0], *c, n)) continue;
                enclosed.push_back(FaceId{ofid});
            }
            for (const FaceId inner : enclosed) {
                const Face& of = faces_.at(inner.value);
                const bool nested = std::ranges::any_of(enclosed, [&](FaceId other) {
                    return other != inner && loopInside(*this, of.loops[0], faces_.at(other.value).loops[0], of.normal);
                });
                if (nested) continue;
                Loop hole = of.loops[0];
                if (glm::dot(of.normal, n) > 0.0) hole = reversedLoop(hole);
                candidate.loops.push_back(hole);
            }
            // The new face must not cover an existing coplanar face.
            const auto sample = interiorPoint(*this, candidate);
            if (!sample) continue;
            const bool covered = std::ranges::any_of(faces_, [&](const auto& kv) {
                return std::abs(glm::dot(kv.second.normal, n)) >= 1.0 - tol::kCoplanarNormal &&
                       classify(*this, kv.second, *sample) != Where::Outside;
            });
            if (covered) continue;
            const FaceId fid{nextFace_};
            insertFace(fid, candidate);
            result.createdFaces.push_back(fid);
        }
    }
}

std::optional<EraseEdgeResult> Mesh::eraseEdge(EdgeId id) {
    const auto it = edges_.find(id.value);
    if (it == edges_.end()) return std::nullopt;
    const Edge e = it->second;
    EraseEdgeResult result;
    const auto users = facesOfEdge(id);

    const auto loopIndex = [&](const Face& f) {
        for (std::size_t l = 0; l < f.loops.size(); ++l) {
            for (const auto& u : f.loops[l]) {
                if (u.edge == id) return l;
            }
        }
        return std::size_t{0};
    };

    bool merged = false;
    if (users.size() == 2) {
        const Face f1 = faces_.at(users[0].value);
        const Face f2 = faces_.at(users[1].value);
        const bool coplanar = sameDirection(f1.normal, f2.normal) && onPlane(*this, f1, loopPositions(f2.loops[0]).front());
        const std::size_t l1 = loopIndex(f1);
        const std::size_t l2 = loopIndex(f2);
        if (coplanar && l1 == 0 && l2 == 0) {
            // Splice the outer loops at the edge, then drop back-and-forth spurs
            // left by any other shared edges.
            const auto withoutEdge = [&](const Loop& loop) {
                const auto pos = std::ranges::find_if(loop, [&](const EdgeUse& u) { return u.edge == id; });
                Loop out(pos + 1, loop.end());
                out.insert(out.end(), loop.begin(), pos);
                return out;
            };
            Loop joined = withoutEdge(f1.loops[0]);
            const Loop second = withoutEdge(f2.loops[0]);
            joined.insert(joined.end(), second.begin(), second.end());
            bool spur = true;
            while (spur && joined.size() >= 2) {
                spur = false;
                for (std::size_t i = 0; i < joined.size(); ++i) {
                    const std::size_t j = (i + 1) % joined.size();
                    if (joined[i].edge == joined[j].edge) {
                        if (j == 0) {
                            joined.erase(joined.begin() + static_cast<std::ptrdiff_t>(i));
                            joined.erase(joined.begin());
                        } else {
                            joined.erase(joined.begin() + static_cast<std::ptrdiff_t>(i), joined.begin() + static_cast<std::ptrdiff_t>(j) + 1);
                        }
                        spur = true;
                        break;
                    }
                }
            }
            if (simpleLoop(*this, joined)) {
                Face face{{joined}, f1.normal};
                for (std::size_t h = 1; h < f1.loops.size(); ++h) face.loops.push_back(f1.loops[h]);
                for (std::size_t h = 1; h < f2.loops.size(); ++h) face.loops.push_back(f2.loops[h]);
                faces_.erase(users[0].value);
                faces_.erase(users[1].value);
                const FaceId fid{nextFace_};
                insertFace(fid, face);
                result.removedFaces = users;
                result.mergedFace = fid;
                merged = true;
            }
        } else if (coplanar && (l1 == 0) != (l2 == 0)) {
            // A hole and the inner face that fills it: the inner face is absorbed.
            const FaceId outerId = l1 == 0 ? users[1] : users[0];
            const FaceId innerId = l1 == 0 ? users[0] : users[1];
            Face outer = faces_.at(outerId.value);
            const Face inner = faces_.at(innerId.value);
            outer.loops.erase(outer.loops.begin() + static_cast<std::ptrdiff_t>(loopIndex(outer)));
            for (std::size_t h = 1; h < inner.loops.size(); ++h) outer.loops.push_back(inner.loops[h]);
            faces_[outerId.value] = outer;
            faces_.erase(innerId.value);
            result.removedFaces.push_back(innerId);
            result.mergedFace = outerId;
            merged = true;
        }
    }
    if (!merged) {
        for (const FaceId f : users) faces_.erase(f.value);
        result.removedFaces = users;
    }
    edges_.erase(id.value);
    for (const VertexId v : {e.v0, e.v1}) {
        if (edgesOfVertex(v).empty()) vertices_.erase(v.value);
    }
    pruneCurves();
    return result;
}

bool Mesh::eraseFace(FaceId id) { return faces_.erase(id.value) > 0; }

void Mesh::pruneCurves() {
    for (auto it = curves_.begin(); it != curves_.end();) {
        const auto& list = it->second.edges;
        bool ok = !list.empty();
        // The walk starts at the first edge's end that the second edge does not
        // share (the first edge may run against the curve's direction).
        std::optional<VertexId> last;
        if (list.size() >= 2) {
            const Edge* first = edge(list[0]);
            const Edge* second = edge(list[1]);
            if (first && second && (first->v0 == second->v0 || first->v0 == second->v1)) last = first->v1;
        }
        for (const EdgeId eid : list) {
            const Edge* e = edge(eid);
            if (!e || !e->curve || e->curve->value != it->first) {
                ok = false;
                break;
            }
            if (last && *last != e->v0 && *last != e->v1) {
                ok = false;
                break;
            }
            last = (last && *last == e->v1) ? e->v0 : e->v1;
        }
        if (ok) {
            ++it;
            continue;
        }
        for (auto& [eid, edge] : edges_) {
            if (edge.curve && edge.curve->value == it->first) edge.curve.reset();
        }
        it = curves_.erase(it);
    }
}

void Mesh::scale(const Vec3& f) {
    for (auto& [id, v] : vertices_) v.position *= f;
    for (auto& [id, face] : faces_) {
        const Vec3 n = loopNormal(loopPositions(face.loops[0]));
        if (glm::length(n) > 0.0) face.normal = n;
    }
    for (auto& [id, c] : curves_) {
        c.center *= f; // curves only survive uniform scales meaningfully; radius follows x
        c.radius *= std::abs(f.x);
    }
}

void Mesh::translate(const Vec3& offset) {
    for (auto& [id, v] : vertices_) v.position += offset;
    for (auto& [id, c] : curves_) c.center += offset;
}

std::optional<PushPullResult> Mesh::pushPull(FaceId id, double distance) {
    const auto it = faces_.find(id.value);
    if (it == faces_.end() || !std::isfinite(distance) || std::abs(distance) < tol::kMinDimension) return std::nullopt;
    const Face face = it->second;
    const Vec3 n = face.normal;
    const Vec3 offset = n * distance;

    std::vector<VertexId> ring; // every vertex of the face, once
    std::vector<EdgeId> own;    // every edge of the face
    for (const Loop& loop : face.loops) {
        for (const VertexId v : loopVertices(loop)) {
            if (std::ranges::find(ring, v) == ring.end()) ring.push_back(v);
        }
        for (const EdgeUse& use : loop) own.push_back(use.edge);
    }
    const bool free = std::ranges::all_of(own, [this](EdgeId e) { return facesOfEdge(e).size() == 1; }) &&
                      std::ranges::all_of(ring, [this, &own](VertexId v) {
                          return std::ranges::all_of(edgesOfVertex(v), [&own](EdgeId e) { return std::ranges::find(own, e) != own.end(); });
                      });

    if (!free) {
        // Stretch: the face is the end of a prism. Every edge of the face is
        // shared with a wall, every other edge at its vertices runs along the
        // normal and every other face there contains the normal, so moving the
        // face keeps all of them planar. Anything else attached is refused.
        if (!std::ranges::all_of(own, [this](EdgeId e) { return facesOfEdge(e).size() >= 2; })) return std::nullopt;
        for (const VertexId v : ring) {
            const Vec3 p = vertices_.at(v.value).position;
            for (const EdgeId e : edgesOfVertex(v)) {
                if (std::ranges::find(own, e) != own.end()) continue;
                const Edge& edge = edges_.at(e.value);
                const VertexId w = edge.v0 == v ? edge.v1 : edge.v0;
                const Vec3 along = vertices_.at(w.value).position - p;
                const double s = glm::dot(along, n);
                if (glm::length(along - n * s) > tol::kPlanarity) return std::nullopt; // not along the normal
                // The wall keeps its direction and a usable length.
                if (s * (s - distance) <= 0.0 || std::abs(s - distance) < tol::kMinDimension) return std::nullopt;
            }
        }
        for (const auto& [fid, other] : faces_) {
            if (fid == id.value) continue;
            const auto vs = [&] {
                std::vector<VertexId> all;
                for (const Loop& l : other.loops) {
                    const auto lv = loopVertices(l);
                    all.insert(all.end(), lv.begin(), lv.end());
                }
                return all;
            }();
            const bool touches = std::ranges::any_of(vs, [&ring](VertexId v) { return std::ranges::find(ring, v) != ring.end(); });
            if (touches && std::abs(glm::dot(other.normal, n)) > tol::kCoplanarNormal) return std::nullopt;
        }
        for (const VertexId v : ring) vertices_.at(v.value).position += offset;
        return PushPullResult{id, {}, true};
    }

    // Extrude a free face: a cap at the offset, one side face per loop edge,
    // and the original face stays as the base, turned to face away.
    PushPullResult result{id, {}, false};
    std::map<std::uint32_t, VertexId> top;
    for (const VertexId v : ring) top[v.value] = appendVertex(vertices_.at(v.value).position + offset);
    for (const VertexId v : ring) appendEdge(v, top.at(v.value));
    for (const Loop& loop : face.loops) {
        for (const EdgeUse& use : loop) appendEdge(top.at(useStart(use).value), top.at(useEnd(use).value));
    }
    const bool up = distance > 0.0;
    for (const Loop& loop : face.loops) {
        for (const EdgeUse& use : loop) {
            const VertexId a = useStart(use);
            const VertexId b = useEnd(use);
            const Vec3 side = glm::normalize(glm::cross(vertices_.at(b.value).position - vertices_.at(a.value).position, n));
            const VertexId ta = top.at(a.value);
            const VertexId tb = top.at(b.value);
            const auto sideFace = up ? appendFace({{a, b, tb, ta}}, side) : appendFace({{ta, tb, b, a}}, side);
            if (sideFace) result.createdFaces.push_back(*sideFace);
        }
    }
    std::vector<std::vector<VertexId>> capLoops;
    for (const Loop& loop : face.loops) {
        std::vector<VertexId> vs;
        for (const VertexId v : loopVertices(loop)) vs.push_back(top.at(v.value));
        if (!up) std::ranges::reverse(vs);
        capLoops.push_back(std::move(vs));
    }
    const auto cap = appendFace(capLoops, up ? n : -n);
    if (!cap) return std::nullopt;
    result.face = *cap;
    result.createdFaces.push_back(*cap);
    if (up) {
        Face& base = faces_.at(id.value);
        for (Loop& loop : base.loops) loop = reversedLoop(loop);
        base.normal = -n;
    }
    return result;
}

// ---- Validation --------------------------------------------------------------------

std::optional<std::string> validateMesh(const Mesh& m, std::string_view pathView) {
    const std::string path(pathView);
    for (const auto& [id, v] : m.vertices()) {
        const auto& p = v.position;
        for (const double c : {p.x, p.y, p.z}) {
            if (!std::isfinite(c) || std::abs(c) > tol::kMaxCoordinate) {
                return path + ".vertices: vertex " + std::to_string(id) + " is outside the 10 km model range";
            }
        }
        if (id == 0 || id >= m.nextVertexId()) return path + ".vertices: vertex id " + std::to_string(id) + " is not below next";
    }
    std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (const auto& [id, e] : m.edges()) {
        const std::string where = path + ".edges: edge " + std::to_string(id);
        if (id == 0 || id >= m.nextEdgeId()) return where + " has an id that is not below next";
        const Vertex* a = m.vertex(e.v0);
        const Vertex* b = m.vertex(e.v1);
        if (!a || !b) return where + " refers to a missing vertex";
        if (e.v0 == e.v1 || glm::length(a->position - b->position) <= tol::kWeldDistance) return where + " has zero length";
        const auto key = std::minmax(e.v0.value, e.v1.value);
        if (!pairs.insert(key).second) return where + " duplicates another edge";
        if (e.curve && !m.curve(*e.curve)) return where + " refers to a missing curve";
    }
    for (const auto& [id, f] : m.faces()) {
        const std::string where = path + ".faces: face " + std::to_string(id);
        if (id == 0 || id >= m.nextFaceId()) return where + " has an id that is not below next";
        if (f.loops.empty()) return where + " has no outer loop";
        const double nlen = glm::length(f.normal);
        if (!std::isfinite(nlen) || std::abs(nlen - 1.0) > 1e-6) return where + " has no unit normal";
        std::set<std::uint32_t> faceVertices;
        Vec3 origin{0.0};
        for (std::size_t l = 0; l < f.loops.size(); ++l) {
            const auto& loop = f.loops[l];
            if (loop.size() < 3) return where + " has a loop with fewer than 3 edges";
            for (std::size_t i = 0; i < loop.size(); ++i) {
                if (!m.edge(loop[i].edge)) return where + " uses a missing edge";
            }
            for (std::size_t i = 0; i < loop.size(); ++i) {
                if (m.useEnd(loop[i]) != m.useStart(loop[(i + 1) % loop.size()])) return where + " has a loop that is not closed";
            }
            for (const auto v : m.loopVertices(loop)) {
                if (!faceVertices.insert(v.value).second) return where + " visits a vertex twice";
            }
            const auto pts = m.loopPositions(loop);
            if (l == 0) origin = pts.front();
            for (const auto& p : pts) {
                if (std::abs(glm::dot(p - origin, f.normal)) > tol::kPlanarity) return where + " is not planar";
            }
            const double area = signedArea(pts, f.normal);
            if (l == 0 && area <= 0.0) return where + " outer loop is not counter-clockwise about its normal";
            if (l > 0 && area >= 0.0) return where + " has a hole that is not clockwise";
            if (std::abs(area) < tol::kMinArea * 0.5) return where + " is too small";
        }
        const PlaneAxes axes = axesFor(f.normal);
        std::vector<Vec2> outer;
        for (const auto& p : m.loopPositions(f.loops[0])) outer.push_back(project(axes, origin, p));
        for (std::size_t l = 1; l < f.loops.size(); ++l) {
            for (const auto& p : m.loopPositions(f.loops[l])) {
                if (!insidePolygon(project(axes, origin, p), outer)) return where + " has a hole outside its outer loop";
            }
        }
    }
    for (const auto& [id, c] : m.curves()) {
        const std::string where = path + ".curves: curve " + std::to_string(id);
        if (id == 0 || id >= m.nextCurveId()) return where + " has an id that is not below next";
        if (!(c.radius > 0.0) || !std::isfinite(c.radius) || c.segments < 1) return where + " has no radius or segments";
        for (const EdgeId e : c.edges) {
            if (!m.edge(e)) return where + " refers to a missing edge";
        }
    }
    return std::nullopt;
}

// ---- Triangulation -----------------------------------------------------------------

std::vector<std::array<VertexId, 3>> triangulateFace(const Mesh& m, FaceId fid) {
    std::vector<std::array<VertexId, 3>> out;
    const Face* f = m.face(fid);
    if (!f || f->loops.empty()) return out;
    const PlaneAxes axes = axesFor(f->normal);
    const Vec3 origin = m.loopPositions(f->loops[0]).front();
    struct Pt {
        VertexId id;
        Vec2 p;
    };
    const auto loop2d = [&](const Loop& loop) {
        std::vector<Pt> pts;
        for (const auto v : m.loopVertices(loop)) {
            if (const Vertex* vertex = m.vertex(v)) pts.push_back({v, project(axes, origin, vertex->position)});
        }
        return pts;
    };
    std::vector<Pt> poly = loop2d(f->loops[0]);

    // Bridge holes into the outer loop, rightmost hole first.
    std::vector<std::vector<Pt>> holes;
    for (std::size_t l = 1; l < f->loops.size(); ++l) holes.push_back(loop2d(f->loops[l]));
    const auto rightmost = [](const std::vector<Pt>& h) {
        return static_cast<std::size_t>(std::ranges::max_element(h, [](const Pt& a, const Pt& b) { return a.p.x < b.p.x; }) - h.begin());
    };
    std::ranges::sort(holes, [&](const auto& a, const auto& b) { return a[rightmost(a)].p.x > b[rightmost(b)].p.x; });
    const auto segmentsCross = [](const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
        const double d1 = cross2(b - a, c - a);
        const double d2 = cross2(b - a, d - a);
        const double d3 = cross2(d - c, a - c);
        const double d4 = cross2(d - c, b - c);
        return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
    };
    for (std::size_t h = 0; h < holes.size(); ++h) {
        const auto& hole = holes[h];
        const std::size_t mi = rightmost(hole);
        const Vec2 mp = hole[mi].p;
        std::optional<std::size_t> best;
        double bestDist = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Vec2 pp = poly[i].p;
            const double d = glm::length(pp - mp);
            if (d >= bestDist || d <= 0.0) continue;
            bool blocked = false;
            const auto checkLoop = [&](const std::vector<Pt>& lp) {
                for (std::size_t k = 0; k < lp.size() && !blocked; ++k) {
                    const Vec2 a = lp[k].p;
                    const Vec2 b = lp[(k + 1) % lp.size()].p;
                    if (segmentsCross(mp, pp, a, b)) blocked = true;
                }
            };
            checkLoop(poly);
            for (std::size_t o = h; o < holes.size() && !blocked; ++o) checkLoop(holes[o]);
            // The bridge must run through the face: its midpoint inside the
            // current outer polygon and outside the holes still to be bridged.
            const Vec2 midpoint = (mp + pp) * 0.5;
            std::vector<Vec2> outline;
            for (const auto& q : poly) outline.push_back(q.p);
            bool between = insidePolygon(midpoint, outline);
            for (std::size_t o = h + 1; o < holes.size() && between; ++o) {
                std::vector<Vec2> hp;
                for (const auto& q : holes[o]) hp.push_back(q.p);
                if (insidePolygon(midpoint, hp)) between = false;
            }
            if (!blocked && between) {
                best = i;
                bestDist = d;
            }
        }
        if (!best.has_value()) continue; // cannot bridge; the hole is left out of the triangulation
        std::vector<Pt> merged(poly.begin(), poly.begin() + static_cast<std::ptrdiff_t>(*best) + 1);
        for (std::size_t k = 0; k <= hole.size(); ++k) merged.push_back(hole[(mi + k) % hole.size()]);
        merged.push_back(poly[*best]);
        merged.insert(merged.end(), poly.begin() + static_cast<std::ptrdiff_t>(*best) + 1, poly.end());
        poly = std::move(merged);
    }

    // Ear clipping.
    std::vector<std::size_t> idx(poly.size());
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    const auto inTri = [](const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
        const double e = 1e-15;
        return cross2(b - a, p - a) >= -e && cross2(c - b, p - b) >= -e && cross2(a - c, p - c) >= -e;
    };
    std::size_t guard = 0;
    while (idx.size() > 3 && guard < poly.size() * poly.size() + 10) {
        ++guard;
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const std::size_t ia = idx[(i + idx.size() - 1) % idx.size()];
            const std::size_t ib = idx[i];
            const std::size_t ic = idx[(i + 1) % idx.size()];
            const Vec2 a = poly[ia].p;
            const Vec2 b = poly[ib].p;
            const Vec2 c = poly[ic].p;
            const double cr = cross2(b - a, c - b);
            if (cr <= 0.0) {
                // Collinear (zero-area) corner: drop it without a triangle.
                if (cr == 0.0 || std::abs(cr) < 1e-18) {
                    if (glm::dot(b - a, c - b) > 0.0) {
                        idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
                        clipped = true;
                        break;
                    }
                }
                continue;
            }
            bool ear = true;
            for (const std::size_t k : idx) {
                if (k == ia || k == ib || k == ic) continue;
                const Vec2 p = poly[k].p;
                if (p == a || p == b || p == c) continue;
                if (inTri(p, a, b, c)) {
                    ear = false;
                    break;
                }
            }
            if (!ear) continue;
            out.push_back({poly[ia].id, poly[ib].id, poly[ic].id});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) {
        const Vec2 a = poly[idx[0]].p;
        const Vec2 b = poly[idx[1]].p;
        const Vec2 c = poly[idx[2]].p;
        if (cross2(b - a, c - b) > 0.0) out.push_back({poly[idx[0]].id, poly[idx[1]].id, poly[idx[2]].id});
    }
    return out;
}

Triangulation triangulate(const Mesh& m) {
    Triangulation t;
    std::map<std::uint32_t, std::uint32_t> index;
    for (const auto& [id, v] : m.vertices()) {
        index[id] = static_cast<std::uint32_t>(t.positions.size());
        t.positions.push_back(v.position);
        t.vertexIds.push_back(VertexId{id});
    }
    for (const auto& [fid, f] : m.faces()) {
        for (const auto& tri : triangulateFace(m, FaceId{fid})) {
            for (const auto v : tri) t.indices.push_back(index.at(v.value));
            t.triangleFaces.push_back(FaceId{fid});
        }
    }
    return t;
}

Connected connectedElements(const Mesh& m, VertexId start) {
    Connected out;
    if (!m.vertex(start)) return out;
    std::set<VertexId> seen{start};
    std::vector<VertexId> stack{start};
    std::set<EdgeId> edges;
    while (!stack.empty()) {
        const VertexId v = stack.back();
        stack.pop_back();
        for (const EdgeId e : m.edgesOfVertex(v)) {
            edges.insert(e);
            const Edge* edge = m.edge(e);
            if (!edge) continue;
            const VertexId other = edge->v0 == v ? edge->v1 : edge->v0;
            if (seen.insert(other).second) stack.push_back(other);
        }
    }
    out.vertices.assign(seen.begin(), seen.end());
    out.edges.assign(edges.begin(), edges.end());
    for (const auto& [fid, f] : m.faces()) {
        if (!f.loops.empty() && edges.contains(f.loops[0].front().edge)) out.faces.push_back(FaceId{fid});
    }
    return out;
}

// ---- Profile objects ---------------------------------------------------------------

Mesh meshFromProfile(const ProfileObject& o) {
    Mesh m;
    const auto& plane = o.profile.plane;
    const Vec3 n = glm::normalize(glm::cross(plane.xAxis, plane.yAxis));
    const auto local = [&](const Vec2& p, double offset) { return plane.origin + plane.xAxis * p.x + plane.yAxis * p.y + n * offset; };
    const auto count = static_cast<std::uint32_t>(o.profile.boundary.size());
    // A circle or polygon keeps its true shape beside the segments: curve 1 is
    // the base loop (edges 1..n), curve 2 the cap loop (edges n+1..2n). A
    // circle's side edges are soft and smooth, so a cylinder reads as one surface.
    const bool round = o.profile.kind == ProfileKind::Circle;
    const bool regular = round || o.profile.kind == ProfileKind::Polygon;
    const auto curveOf = [regular](std::uint32_t id) { return regular ? std::optional<CurveId>(CurveId{id}) : std::nullopt; };
    const auto addCurve = [&](std::uint32_t id, std::uint32_t firstEdge, double offset) {
        if (!regular || count == 0) return;
        Curve c;
        c.kind = round ? CurveKind::Circle : CurveKind::Polygon;
        c.center = plane.origin + n * offset;
        c.normal = n;
        c.xAxis = plane.xAxis;
        c.radius = glm::length(o.profile.boundary[0]);
        c.start = std::atan2(o.profile.boundary[0].y, o.profile.boundary[0].x);
        c.sweep = 2.0 * std::numbers::pi;
        c.segments = static_cast<int>(count);
        for (std::uint32_t i = 0; i < count; ++i) c.edges.push_back(EdgeId{firstEdge + i});
        m.insertCurve(CurveId{id}, c);
    };
    for (std::uint32_t i = 0; i < count; ++i) m.insertVertex(VertexId{i + 1}, local(o.profile.boundary[i], 0.0));
    for (std::uint32_t i = 0; i < count; ++i) {
        m.insertEdge(EdgeId{i + 1}, Edge{VertexId{i + 1}, VertexId{(i + 1) % count + 1}, curveOf(1), false, false});
    }
    addCurve(1, 1, 0.0);
    if (!o.extrusion) {
        Face f{{}, n};
        std::vector<EdgeUse> loop;
        for (std::uint32_t i = 0; i < count; ++i) loop.push_back(EdgeUse{EdgeId{i + 1}, false});
        f.loops.push_back(loop);
        m.insertFace(FaceId{1}, f);
        return m;
    }
    const double h = o.extrusion->height;
    for (std::uint32_t i = 0; i < count; ++i) m.insertVertex(VertexId{count + i + 1}, local(o.profile.boundary[i], h));
    for (std::uint32_t i = 0; i < count; ++i) {
        m.insertEdge(EdgeId{count + i + 1}, Edge{VertexId{count + i + 1}, VertexId{count + (i + 1) % count + 1}, curveOf(2), false, false});
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        m.insertEdge(EdgeId{2 * count + i + 1}, Edge{VertexId{i + 1}, VertexId{count + i + 1}, {}, round, round});
    }
    addCurve(2, count + 1, h);
    // Base: bottom loop reversed (faces -n). Cap: top loop. Side i: i, i+1, top i+1, top i.
    Face base{{}, -n};
    Loop baseLoop;
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t i = (2 * count - 2 - k) % count; // edges n-2 .. 0, n-1, walked backwards
        baseLoop.push_back(EdgeUse{EdgeId{i + 1}, true});
    }
    base.loops.push_back(baseLoop);
    m.insertFace(FaceId{1}, base);
    Face cap{{}, n};
    Loop capLoop;
    for (std::uint32_t i = 0; i < count; ++i) capLoop.push_back(EdgeUse{EdgeId{count + i + 1}, false});
    cap.loops.push_back(capLoop);
    m.insertFace(FaceId{2}, cap);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t j = (i + 1) % count;
        const Vec3 a = local(o.profile.boundary[i], 0.0);
        const Vec3 b = local(o.profile.boundary[j], 0.0);
        Face side{{Loop{EdgeUse{EdgeId{i + 1}, false}, EdgeUse{EdgeId{2 * count + j + 1}, false}, EdgeUse{EdgeId{count + i + 1}, true},
                        EdgeUse{EdgeId{2 * count + i + 1}, true}}},
                  glm::normalize(glm::cross(b - a, n))};
        m.insertFace(FaceId{3 + i}, side);
    }
    return m;
}

FaceId faceIdFor(pf::FaceId face) {
    switch (face.kind) {
    case FaceKind::Profile:
    case FaceKind::Base: return FaceId{1};
    case FaceKind::Cap: return FaceId{2};
    case FaceKind::Side: return FaceId{3 + static_cast<std::uint32_t>(face.side)};
    case FaceKind::Mesh: return FaceId{static_cast<std::uint32_t>(face.side)};
    }
    return FaceId{1};
}

std::optional<pf::FaceId> semanticFace(const ProfileObject& o, FaceId face) {
    if (o.mesh) return o.mesh->face(face) ? std::optional<pf::FaceId>(pf::FaceId::meshFace(face.value)) : std::nullopt;
    const auto n = static_cast<std::uint32_t>(o.profile.boundary.size());
    if (face.value == 1) return o.extrusion ? pf::FaceId::base() : pf::FaceId::profile();
    if (!o.extrusion) return std::nullopt;
    if (face.value == 2) return pf::FaceId::cap();
    if (face.value >= 3 && face.value < 3 + n) return pf::FaceId::sideAt(static_cast<int>(face.value - 3));
    return std::nullopt;
}

MeshPtr objectMesh(const ProfileObject& o) {
    if (o.mesh) return o.mesh;
    return std::make_shared<const Mesh>(meshFromProfile(o));
}

} // namespace pf::mesh

namespace pf {

bool operator==(const ProfileObject& a, const ProfileObject& b) {
    if (!(a.id == b.id && a.name == b.name && a.profile == b.profile && a.extrusion == b.extrusion && a.transform == b.transform)) {
        return false;
    }
    if (!a.mesh || !b.mesh) return !a.mesh && !b.mesh;
    return a.mesh == b.mesh || *a.mesh == *b.mesh;
}

} // namespace pf
