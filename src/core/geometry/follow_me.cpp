#include "core/geometry/follow_me.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <map>
#include <set>

namespace pf::mesh {

namespace {

using Ring = std::vector<VertexId>;
using Section = std::vector<Vec3>;

bool sameCurve(const Mesh& m, EdgeId a, EdgeId b) {
    const Edge* ea = m.edge(a);
    const Edge* eb = m.edge(b);
    return ea && eb && ea->curve && eb->curve && *ea->curve == *eb->curve;
}

/// The first point where the path runs straight on (an edge split by a vertex), if any.
std::optional<std::size_t> straightPoint(const SweepPath& path) {
    const std::size_t n = path.points.size();
    if (n <= 2) return std::nullopt;
    for (std::size_t i = 0; i < n; ++i) {
        if (!path.closed && (i == 0 || i + 1 == n)) continue;
        const Vec3 din = glm::normalize(path.points[i] - path.points[(i + n - 1) % n]);
        const Vec3 dout = glm::normalize(path.points[(i + 1) % n] - path.points[i]);
        if (glm::dot(din, dout) >= 1.0 - 1e-12) return i;
    }
    return std::nullopt;
}

/// Drops repeated points and points where the path runs straight on, so a
/// straight run sweeps as one face.
SweepPath simplified(const SweepPath& in) {
    SweepPath out;
    out.closed = in.closed;
    for (std::size_t i = 0; i < in.points.size(); ++i) {
        if (!out.points.empty() && glm::length(in.points[i] - out.points.back()) <= tol::kWeldDistance) continue;
        out.points.push_back(in.points[i]);
        out.smooth.push_back(i < in.smooth.size() && in.smooth[i]);
    }
    if (out.closed && out.points.size() > 1 && glm::length(out.points.back() - out.points.front()) <= tol::kWeldDistance) {
        out.points.pop_back();
        out.smooth.pop_back();
    }
    while (const auto i = straightPoint(out)) {
        out.points.erase(out.points.begin() + static_cast<std::ptrdiff_t>(*i));
        out.smooth.erase(out.smooth.begin() + static_cast<std::ptrdiff_t>(*i));
    }
    return out;
}

/// Where `p`, moving along `d`, meets the plane through `origin` with `normal`.
Vec3 project(const Vec3& p, const Vec3& d, const Vec3& origin, const Vec3& normal) {
    return p + d * (glm::dot(origin - p, normal) / glm::dot(d, normal));
}

/// Every point of `from` carried along `dir` onto the plane through `origin` with `normal`.
Section carry(const Section& from, const Vec3& dir, const Vec3& origin, const Vec3& normal) {
    Section out;
    out.reserve(from.size());
    for (const Vec3& p : from) out.push_back(project(p, dir, origin, normal));
    return out;
}

bool inRange(const Vec3& p) {
    return std::ranges::all_of(std::array{p.x, p.y, p.z},
                               [](double c) { return std::isfinite(c) && std::abs(c) <= tol::kMaxCoordinate; });
}

Loop reversed(const Loop& loop) {
    Loop out(loop.rbegin(), loop.rend());
    for (EdgeUse& u : out) u.reversed = !u.reversed;
    return out;
}

/// Removes consecutive repeats (cyclically). Nothing when a vertex still repeats.
std::optional<Ring> cleanRing(const Ring& ring) {
    Ring out;
    for (const VertexId v : ring) {
        if (out.empty() || out.back() != v) out.push_back(v);
    }
    while (out.size() > 1 && out.back() == out.front()) out.pop_back();
    std::set<std::uint32_t> seen;
    if (!std::ranges::all_of(out, [&seen](VertexId v) { return seen.insert(v.value).second; })) return std::nullopt;
    return out;
}

/// The other end of `edge` from `v`.
VertexId across(const Edge& edge, VertexId v) { return edge.v0 == v ? edge.v1 : edge.v0; }

// ---- The profile ------------------------------------------------------------------------

/// The profile's points, once each, and its loops as indices into them.
struct Profile {
    Face face;
    std::vector<VertexId> ids;
    Section points;
    std::vector<std::vector<std::size_t>> loops;
    std::vector<bool> smooth; ///< per point: inside a curve of the profile
    std::vector<EdgeId> edges;
    bool free = false; ///< no other face uses its edges
    Vec3 centroid{0.0};
};

std::size_t pointIndex(const Mesh& m, Profile& p, VertexId v) {
    if (const auto it = std::ranges::find(p.ids, v); it != p.ids.end()) return static_cast<std::size_t>(it - p.ids.begin());
    p.ids.push_back(v);
    p.points.push_back(m.vertex(v)->position);
    p.smooth.push_back(false);
    return p.ids.size() - 1;
}

Profile readProfile(const Mesh& m, const Face& face) {
    Profile p;
    p.face = face;
    for (const Loop& loop : face.loops) {
        std::vector<std::size_t> indices;
        const auto vs = m.loopVertices(loop);
        for (std::size_t i = 0; i < vs.size(); ++i) {
            const std::size_t index = pointIndex(m, p, vs[i]);
            // Loop use i starts at vs[i]; the use before it ends there.
            if (sameCurve(m, loop[(i + loop.size() - 1) % loop.size()].edge, loop[i].edge)) p.smooth[index] = true;
            indices.push_back(index);
        }
        for (const EdgeUse& u : loop) p.edges.push_back(u.edge);
        p.loops.push_back(std::move(indices));
    }
    p.free = std::ranges::all_of(p.edges, [&m](EdgeId e) { return m.facesOfEdge(e).size() == 1; });
    for (const Vec3& q : p.points) p.centroid += q;
    p.centroid /= static_cast<double>(p.points.size());
    return p;
}

// ---- The path ------------------------------------------------------------------------------

/// Starts where the profile is: the nearer end of an open path, or the nearest
/// point of a closed one, going round the way the profile faces.
void orientPath(SweepPath& path, const Profile& profile) {
    const Vec3 c = profile.centroid;
    if (!path.closed) {
        if (glm::length(path.points.back() - c) < glm::length(path.points.front() - c)) {
            std::ranges::reverse(path.points);
            std::reverse(path.smooth.begin(), path.smooth.end()); // vector<bool> has no ranges overload
        }
        return;
    }
    const auto nearest = std::ranges::min_element(path.points, {}, [&c](const Vec3& p) { return glm::length(p - c); });
    const auto best = nearest - path.points.begin();
    std::ranges::rotate(path.points, path.points.begin() + best);
    std::rotate(path.smooth.begin(), path.smooth.begin() + best, path.smooth.end()); // vector<bool>: no ranges overload
    const Vec3 ahead = glm::normalize(path.points[1] - path.points[0]);
    const Vec3 behind = glm::normalize(path.points.back() - path.points[0]);
    const Vec3 n = profile.face.normal;
    if (std::abs(glm::dot(n, behind)) > std::abs(glm::dot(n, ahead)) + 1e-9) {
        std::reverse(path.points.begin() + 1, path.points.end());
        std::reverse(path.smooth.begin() + 1, path.smooth.end());
    }
}

/// The path oriented, with its segment directions.
struct Route {
    SweepPath path;
    std::size_t count = 0;    ///< points
    std::size_t segments = 0; ///< count for a loop, count - 1 for an open path
    std::vector<Vec3> d;      ///< unit direction of each segment
    Vec3 first{0.0};          ///< d[0]

    /// Unit normal of the mitre plane at point k, or nothing at a U-turn.
    [[nodiscard]] std::optional<Vec3> mitre(std::size_t k) const {
        const Vec3 sum = d[(k + segments - 1) % segments] + d[k % segments];
        if (glm::length(sum) < 1e-6) return std::nullopt;
        return glm::normalize(sum);
    }
};

Route makeRoute(SweepPath path) {
    Route r;
    r.count = path.points.size();
    r.segments = path.closed ? r.count : r.count - 1;
    r.d.resize(r.segments);
    for (std::size_t k = 0; k < r.segments; ++k) r.d[k] = glm::normalize(path.points[(k + 1) % r.count] - path.points[k]);
    r.first = glm::normalize(path.points[1] - path.points[0]); // d[0], spelled so the optimiser sees it exists
    r.path = std::move(path);
    return r;
}

const Error kTurnsBack{"The path turns back on itself. Pick a path without a U-turn."};

/// Cross-sections: the profile carried along each segment onto the next point's
/// mitre plane (or, at the end of an open path, the square end).
Expected<std::vector<Section>> openSections(const Route& r, const Section& profile) {
    std::vector<Section> s{profile};
    for (std::size_t k = 1; k + 1 < r.count; ++k) {
        const auto mk = r.mitre(k);
        if (!mk) return kTurnsBack;
        s.push_back(carry(s.back(), r.d[k - 1], r.path.points[k], *mk));
    }
    const Vec3& last = r.d.back();
    s.push_back(carry(s.back(), last, r.path.points.back(), last));
    return s;
}

Expected<std::vector<Section>> closedSections(const Route& r, const Section& profile) {
    const auto m0 = r.mitre(0);
    if (!m0) return kTurnsBack;
    std::vector<Section> s{carry(profile, r.first, r.path.points[0], *m0)};
    for (std::size_t k = 1; k < r.count; ++k) {
        const auto mk = r.mitre(k);
        if (!mk) return kTurnsBack;
        s.push_back(carry(s.back(), r.d[k - 1], r.path.points[k], *mk));
    }
    const Section back = carry(s.back(), r.d.back(), r.path.points[0], *m0);
    for (std::size_t i = 0; i < back.size(); ++i) {
        if (glm::length(back[i] - s.front()[i]) > tol::kWeldDistance) {
            return Error{"The path twists, so the sweep would not meet itself going round. Use an open path."};
        }
    }
    return s;
}

Expected<std::vector<Section>> sweepSections(const Route& r, const Section& profile) {
    auto sections = r.path.closed ? closedSections(r, profile) : openSections(r, profile);
    if (!sections) return sections;
    for (const auto& section : *sections) {
        if (!std::ranges::all_of(section, inRange)) return Error{"The sweep would reach outside the supported range of +/-10 km."};
    }
    return sections;
}

// ---- Building the faces -------------------------------------------------------------------------

struct Planned {
    Ring ring;
    Vec3 normal{0.0};
};

/// Writes the sweep into the mesh: welded section vertices, then faces.
class Builder {
public:
    Builder(Mesh& m, const Profile& profile, const Route& route, const std::vector<Section>& sections)
        : m_(m), profile_(profile), route_(route) {
        for (const auto& [e, edge] : m.edges()) before_.insert(e);
        for (const Section& section : sections) {
            Ring vs;
            for (const Vec3& p : section) vs.push_back(m.findVertex(p).value_or(VertexId{0}));
            for (std::size_t i = 0; i < vs.size(); ++i) {
                if (vs[i].value == 0) vs[i] = m.appendVertex(section[i]);
            }
            at_.push_back(std::move(vs));
        }
        forward_ = glm::dot(profile.face.normal, route.first) > 0.0;
    }

    /// The faces one profile edge (points a to b) sweeps out.
    [[nodiscard]] std::vector<Planned> planEdge(std::size_t a, std::size_t b) const {
        std::vector<Planned> faces;
        for (std::size_t k = 0; k < route_.segments; ++k) {
            if (auto f = sideFace(a, b, k)) faces.push_back(std::move(*f));
        }
        if (auto disc = fanAsOneFace(faces, a, b)) return {std::move(*disc)};
        return faces;
    }

    /// Every profile edge's side faces.
    bool sides() {
        std::vector<Planned> planned;
        for (const auto& loop : profile_.loops) {
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto faces = planEdge(loop[i], loop[(i + 1) % loop.size()]);
                planned.insert(planned.end(), faces.begin(), faces.end());
            }
        }
        return std::ranges::all_of(planned, [this](const Planned& f) { return place({f.ring}, f.normal); });
    }

    /// Adds a face (edges as needed), or reuses an existing face with the same corners.
    bool place(const std::vector<Ring>& rings, const Vec3& normal) {
        for (const Ring& ring : rings) ensureEdges(ring);
        if (rings.size() == 1 && reuse(rings[0], normal)) return true;
        const auto face = m_.appendFace(rings, normal);
        if (!face) return false;
        result_.createdFaces.push_back(*face);
        return true;
    }

    /// The last section as the far cap of an open sweep, facing on along the last segment.
    bool endCap() {
        std::vector<Ring> rings;
        for (const auto& loop : profile_.loops) {
            Ring ring;
            for (const std::size_t i : loop) ring.push_back(at_.back()[i]);
            rings.push_back(std::move(ring));
        }
        Vec3 n = loopNormal(positions(rings[0]));
        if (glm::dot(n, route_.d.back()) < 0.0) {
            for (Ring& ring : rings) std::ranges::reverse(ring);
            n = -n;
        }
        const std::size_t made = result_.createdFaces.size();
        if (!place(rings, n)) return false;
        if (result_.createdFaces.size() > made) result_.endCap = result_.createdFaces.back();
        return true;
    }

    /// Softens the edges along profile curves and across path curves.
    void softenCurves() {
        const auto& path = route_.path;
        for (const auto& loop : profile_.loops) {
            for (std::size_t i = 0; i < loop.size(); ++i) softenEdge(loop[i], loop[(i + 1) % loop.size()], path);
        }
    }

    [[nodiscard]] bool reused(FaceId id) const { return reused_.contains(id.value); }
    [[nodiscard]] FollowMeResult result() const { return result_; }

private:
    [[nodiscard]] Section positions(const Ring& ring) const {
        Section out;
        out.reserve(ring.size());
        for (const VertexId v : ring) out.push_back(m_.vertex(v)->position);
        return out;
    }

    [[nodiscard]] std::optional<Planned> sideFace(std::size_t a, std::size_t b, std::size_t k) const {
        const std::size_t k1 = (k + 1) % route_.count;
        const Ring ring = forward_ ? Ring{at_[k][a], at_[k][b], at_[k1][b], at_[k1][a]}
                                   : Ring{at_[k][a], at_[k1][a], at_[k1][b], at_[k][b]};
        const auto clean = cleanRing(ring);
        if (!clean || clean->size() < 3) return std::nullopt;
        const auto ps = positions(*clean);
        const Vec3 n = loopNormal(ps);
        if (glm::length(n) < 0.5 || std::abs(signedArea(ps, n)) < tol::kMinArea * 1e-4) return std::nullopt;
        return Planned{*clean, n};
    }

    /// A fan of triangles round one point, all in one plane (a lathe profile's
    /// base or lip), as one face.
    [[nodiscard]] std::optional<Planned> fanAsOneFace(const std::vector<Planned>& faces, std::size_t a, std::size_t b) const {
        if (faces.size() < 2) return std::nullopt;
        const Vec3 n0 = faces.front().normal;
        const bool fan = std::ranges::all_of(faces, [&n0](const Planned& f) {
            return f.ring.size() == 3 && 1.0 - glm::dot(f.normal, n0) <= tol::kCoplanarNormal;
        });
        if (!fan) return std::nullopt;
        const auto apex = std::ranges::find_if(faces.front().ring, [&faces](VertexId v) {
            return std::ranges::all_of(faces, [v](const Planned& f) { return std::ranges::find(f.ring, v) != f.ring.end(); });
        });
        if (apex == faces.front().ring.end()) return std::nullopt;
        Ring ring;
        if (!route_.path.closed) ring.push_back(*apex);
        for (const Ring& section : at_) ring.push_back(section[a] == *apex ? section[b] : section[a]);
        const auto clean = cleanRing(ring);
        if (!clean || clean->size() < 3) return std::nullopt;
        Ring oriented = *clean;
        Vec3 n = loopNormal(positions(oriented));
        if (glm::dot(n, n0) < 0.0) {
            std::ranges::reverse(oriented);
            n = -n;
        }
        return Planned{oriented, n};
    }

    void ensureEdges(const Ring& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const VertexId a = ring[i];
            const VertexId b = ring[(i + 1) % ring.size()];
            if (!m_.findEdge(a, b)) m_.appendEdge(a, b);
        }
    }

    [[nodiscard]] std::optional<std::uint32_t> faceWithCorners(const Ring& ring) const {
        const std::set<std::uint32_t> mine = [&ring] {
            std::set<std::uint32_t> s;
            for (const VertexId v : ring) s.insert(v.value);
            return s;
        }();
        for (const auto& [fid, f] : m_.faces()) {
            if (f.loops.size() != 1 || f.loops[0].size() != mine.size()) continue;
            std::set<std::uint32_t> theirs;
            for (const VertexId v : m_.loopVertices(f.loops[0])) theirs.insert(v.value);
            if (theirs == mine) return fid;
        }
        return std::nullopt;
    }

    /// Same corners as an existing face (a circle the vase stands on): turn it to face `normal`.
    bool reuse(const Ring& ring, const Vec3& normal) {
        const auto fid = faceWithCorners(ring);
        if (!fid) return false;
        Face turned = *m_.face(FaceId{*fid});
        if (glm::dot(turned.normal, normal) < 0.0) {
            turned.loops[0] = reversed(turned.loops[0]);
            turned.normal = -turned.normal;
        }
        m_.insertFace(FaceId{*fid}, turned);
        reused_.insert(*fid);
        return true;
    }

    void softenEdge(std::size_t a, std::size_t b, const SweepPath& path) {
        for (std::size_t k = 0; k < route_.count; ++k) {
            const bool inside = path.closed || (k > 0 && k + 1 < route_.count);
            if (path.smooth[k] && inside) soften(at_[k][a], at_[k][b]);
            if (k < route_.segments && profile_.smooth[a]) soften(at_[k][a], at_[(k + 1) % route_.count][a]);
        }
    }

    void soften(VertexId a, VertexId b) {
        const auto e = m_.findEdge(a, b);
        if (!e) return;
        const bool own = !before_.contains(e->value) || std::ranges::find(profile_.edges, *e) != profile_.edges.end();
        if (!own || m_.facesOfEdge(*e).size() < 2) return;
        Edge flagged = *m_.edge(*e);
        flagged.soft = true;
        flagged.smooth = true;
        m_.insertEdge(*e, flagged);
    }

    Mesh& m_;
    const Profile& profile_;
    const Route& route_;
    std::vector<Ring> at_; ///< the vertex of each profile point at each path point
    std::set<std::uint32_t> before_;
    std::set<std::uint32_t> reused_;
    bool forward_ = true;
    FollowMeResult result_;
};

/// The profile stays as the start cap of an open sweep, facing away from it.
void turnStartCap(Mesh& m, FaceId id, const Profile& profile, const Vec3& firstDir) {
    if (glm::dot(profile.face.normal, firstDir) <= 0.0) return;
    Face turned = profile.face;
    for (Loop& loop : turned.loops) loop = reversed(loop);
    turned.normal = -profile.face.normal;
    m.insertFace(id, turned);
}

/// Round a loop the profile ends up inside the sweep: it goes, with any of its
/// edges the sweep did not use.
void removeProfile(Mesh& m, FaceId id, const Profile& profile) {
    m.eraseFace(id);
    for (const EdgeId e : profile.edges) {
        if (m.edge(e) && m.facesOfEdge(e).empty()) m.eraseEdge(e);
    }
}

/// The edges of `edges` at each of their vertices; an error when the path branches or breaks.
Expected<std::map<std::uint32_t, std::vector<EdgeId>>> edgesAtVertices(const Mesh& m, const std::vector<EdgeId>& edges) {
    std::map<std::uint32_t, std::vector<EdgeId>> at;
    for (const EdgeId e : edges) {
        const Edge& edge = *m.edge(e);
        at[edge.v0.value].push_back(e);
        at[edge.v1.value].push_back(e);
    }
    if (std::ranges::any_of(at, [](const auto& entry) { return entry.second.size() > 2; })) {
        return Error{"The path branches. Pick edges that make one line or one loop."};
    }
    return at;
}

const Error kPieces{"The path is in pieces. Pick edges that join end to end."};

/// Walks from `v` along `at`'s edges, each once, until no unused edge is left.
std::vector<EdgeId> walk(const Mesh& m, const std::map<std::uint32_t, std::vector<EdgeId>>& at, VertexId v, SweepPath& path) {
    std::vector<EdgeId> walked;
    std::set<std::uint32_t> used;
    while (true) {
        path.points.push_back(m.vertex(v)->position);
        const auto& es = at.at(v.value);
        const auto next = std::ranges::find_if(es, [&used](EdgeId e) { return !used.contains(e.value); });
        const bool more = next != es.end();
        path.smooth.push_back(more && !walked.empty() && sameCurve(m, walked.back(), *next));
        if (!more) return walked;
        used.insert(next->value);
        v = across(*m.edge(*next), v);
        walked.push_back(*next);
    }
}

/// From `v`, the edges continuing `came` through vertices with exactly two edges.
void extendRun(const Mesh& m, EdgeId start, VertexId v, std::vector<EdgeId>& out) {
    EdgeId came = start;
    for (auto es = m.edgesOfVertex(v); es.size() == 2; es = m.edgesOfVertex(v)) {
        const EdgeId next = es[0] == came ? es[1] : es[0];
        if (std::ranges::find(out, next) != out.end()) return; // round a loop
        out.push_back(next);
        v = across(*m.edge(next), v);
        came = next;
    }
}

} // namespace

Expected<SweepPath> chainPath(const Mesh& m, const std::vector<EdgeId>& input) {
    std::vector<EdgeId> edges;
    for (const EdgeId e : input) {
        if (!m.edge(e)) return Error{"A path edge no longer exists."};
        if (std::ranges::find(edges, e) == edges.end()) edges.push_back(e);
    }
    if (edges.empty()) return Error{"Pick the path first: edges, or a face to go around its edge."};
    const auto at = edgesAtVertices(m, edges);
    if (!at) return Error{at.error()};
    std::vector<VertexId> ends;
    for (const auto& [v, es] : *at) {
        if (es.size() == 1) ends.emplace_back(VertexId{v});
    }
    if (!ends.empty() && ends.size() != 2) return kPieces;

    SweepPath path;
    path.closed = ends.empty();
    const auto walked = walk(m, *at, path.closed ? m.edge(edges.front())->v0 : ends.front(), path);
    if (walked.size() != edges.size()) return kPieces;
    if (path.closed) {
        path.points.pop_back(); // back at the start
        path.smooth.pop_back();
        path.smooth.front() = sameCurve(m, walked.back(), walked.front());
    }
    return path;
}

std::vector<EdgeId> pathThrough(const Mesh& m, EdgeId start) {
    const Edge* first = m.edge(start);
    if (!first) return {};
    const Curve* curve = first->curve ? m.curve(*first->curve) : nullptr;
    if (curve) {
        std::vector<EdgeId> out;
        std::ranges::copy_if(curve->edges, std::back_inserter(out), [&m](EdgeId e) { return m.edge(e) != nullptr; });
        if (!out.empty()) return out;
    }
    std::vector<EdgeId> out{start};
    extendRun(m, start, first->v0, out);
    extendRun(m, start, first->v1, out);
    return out;
}

Expected<FollowMeResult> followMe(Mesh& m, FaceId profileId, const SweepPath& input) {
    const Face* found = m.face(profileId);
    if (!found) return Error{"The profile face no longer exists."};
    const Profile profile = readProfile(m, *found);
    SweepPath path = simplified(input);
    if (path.points.size() < 2 || (path.closed && path.points.size() < 3)) {
        return Error{"The path needs at least one edge with length."};
    }
    orientPath(path, profile);
    const Route route = makeRoute(std::move(path));
    if (std::abs(glm::dot(profile.face.normal, route.first)) < 1e-6) {
        return Error{"The profile lies along the path. Draw the profile across the path, at its start."};
    }
    const auto sections = sweepSections(route, profile.points);
    if (!sections) return Error{sections.error()};

    Builder build(m, profile, route, *sections);
    if (!build.sides()) return Error{"The sweep could not make its faces."};
    if (!route.path.closed) {
        turnStartCap(m, profileId, profile, route.first);
        if (!build.endCap()) return Error{"The sweep could not make its end face."};
    } else if (profile.free && !build.reused(profileId)) {
        removeProfile(m, profileId, profile);
    }
    build.softenCurves();
    m.pruneCurves();
    return build.result();
}

} // namespace pf::mesh
