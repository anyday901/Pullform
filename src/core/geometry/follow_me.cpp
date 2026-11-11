#include "core/geometry/follow_me.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pf::mesh {

namespace {

bool sameCurve(const Mesh& m, EdgeId a, EdgeId b) {
    const Edge* ea = m.edge(a);
    const Edge* eb = m.edge(b);
    return ea && eb && ea->curve && eb->curve && *ea->curve == *eb->curve;
}

/// Drops repeated points and points where the path runs straight on, so a
/// straight run (an edge split by a vertex) sweeps as one face.
SweepPath simplified(const SweepPath& in) {
    SweepPath out;
    for (std::size_t i = 0; i < in.points.size(); ++i) {
        if (!out.points.empty() && glm::length(in.points[i] - out.points.back()) <= tol::kWeldDistance) continue;
        out.points.push_back(in.points[i]);
        out.smooth.push_back(i < in.smooth.size() && in.smooth[i]);
    }
    out.closed = in.closed;
    if (out.closed && out.points.size() > 1 && glm::length(out.points.back() - out.points.front()) <= tol::kWeldDistance) {
        out.points.pop_back();
        out.smooth.pop_back();
    }
    bool changed = true;
    while (changed && out.points.size() > 2) {
        changed = false;
        const std::size_t n = out.points.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (!out.closed && (i == 0 || i + 1 == n)) continue;
            const Vec3 p = out.points[(i + n - 1) % n];
            const Vec3 q = out.points[i];
            const Vec3 r = out.points[(i + 1) % n];
            const Vec3 din = glm::normalize(q - p);
            const Vec3 dout = glm::normalize(r - q);
            if (glm::dot(din, dout) >= 1.0 - 1e-12) {
                out.points.erase(out.points.begin() + static_cast<std::ptrdiff_t>(i));
                out.smooth.erase(out.smooth.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return out;
}

/// Where `p`, moving along `d`, meets the plane through `origin` with `normal`.
Vec3 project(const Vec3& p, const Vec3& d, const Vec3& origin, const Vec3& normal) {
    return p + d * (glm::dot(origin - p, normal) / glm::dot(d, normal));
}

Loop reversed(const Loop& loop) {
    Loop out(loop.rbegin(), loop.rend());
    for (EdgeUse& u : out) u.reversed = !u.reversed;
    return out;
}

/// Removes consecutive repeats (cyclically). Nothing when a vertex still repeats.
std::optional<std::vector<VertexId>> cleanRing(const std::vector<VertexId>& ring) {
    std::vector<VertexId> out;
    for (const VertexId v : ring) {
        if (out.empty() || out.back() != v) out.push_back(v);
    }
    while (out.size() > 1 && out.back() == out.front()) out.pop_back();
    std::set<std::uint32_t> seen;
    for (const VertexId v : out) {
        if (!seen.insert(v.value).second) return std::nullopt;
    }
    return out;
}

struct Planned {
    std::vector<VertexId> ring;
    Vec3 normal{0.0};
};

} // namespace

Expected<SweepPath> chainPath(const Mesh& m, const std::vector<EdgeId>& input) {
    std::vector<EdgeId> edges;
    for (const EdgeId e : input) {
        if (!m.edge(e)) return Error{"A path edge no longer exists."};
        if (std::ranges::find(edges, e) == edges.end()) edges.push_back(e);
    }
    if (edges.empty()) return Error{"Pick the path first: edges, or a face to go around its edge."};
    std::map<std::uint32_t, std::vector<EdgeId>> at;
    for (const EdgeId e : edges) {
        const Edge& edge = *m.edge(e);
        at[edge.v0.value].push_back(e);
        at[edge.v1.value].push_back(e);
    }
    std::vector<VertexId> ends;
    for (const auto& [v, es] : at) {
        if (es.size() > 2) return Error{"The path branches. Pick edges that make one line or one loop."};
        if (es.size() == 1) ends.push_back(VertexId{v});
    }
    if (!ends.empty() && ends.size() != 2) return Error{"The path is in pieces. Pick edges that join end to end."};

    SweepPath path;
    path.closed = ends.empty();
    VertexId v = path.closed ? m.edge(edges.front())->v0 : ends.front();
    std::vector<EdgeId> walked; // the edge arrived by at each point
    std::optional<EdgeId> came;
    std::set<std::uint32_t> used;
    while (true) {
        path.points.push_back(m.vertex(v)->position);
        const auto& es = at.at(v.value);
        std::optional<EdgeId> next;
        for (const EdgeId e : es) {
            if (!used.contains(e.value)) next = e;
        }
        bool smooth = false;
        if (came && next) smooth = sameCurve(m, *came, *next);
        path.smooth.push_back(smooth);
        if (!next) break;
        used.insert(next->value);
        const Edge& edge = *m.edge(*next);
        v = edge.v0 == v ? edge.v1 : edge.v0;
        came = next;
        walked.push_back(*next);
    }
    if (used.size() != edges.size()) return Error{"The path is in pieces. Pick edges that join end to end."};
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
    if (first->curve) {
        if (const Curve* c = m.curve(*first->curve)) {
            std::vector<EdgeId> out;
            for (const EdgeId e : c->edges) {
                if (m.edge(e)) out.push_back(e);
            }
            if (!out.empty()) return out;
        }
    }
    std::vector<EdgeId> out{start};
    for (const VertexId from : {first->v0, first->v1}) {
        VertexId v = from;
        EdgeId came = start;
        while (true) {
            const auto es = m.edgesOfVertex(v);
            if (es.size() != 2) break;
            const EdgeId next = es[0] == came ? es[1] : es[0];
            if (next == start || std::ranges::find(out, next) != out.end()) break; // round a loop
            out.push_back(next);
            const Edge& edge = *m.edge(next);
            v = edge.v0 == v ? edge.v1 : edge.v0;
            came = next;
        }
    }
    return out;
}

Expected<FollowMeResult> followMe(Mesh& m, FaceId profileId, const SweepPath& input) {
    const Face* found = m.face(profileId);
    if (!found) return Error{"The profile face no longer exists."};
    const Face profile = *found;
    SweepPath path = simplified(input);
    const std::size_t count = path.points.size();
    if (count < 2 || (path.closed && count < 3)) return Error{"The path needs at least one edge with length."};

    // The profile's points, once each, and its loops as indices into them.
    std::vector<VertexId> ids;
    std::vector<Vec3> points;
    std::vector<std::vector<std::size_t>> loops;
    std::vector<bool> profileSmooth; // the point is inside a curve of the profile
    std::vector<EdgeId> profileEdges;
    for (const Loop& loop : profile.loops) {
        std::vector<std::size_t> indices;
        const auto vs = m.loopVertices(loop);
        for (std::size_t i = 0; i < vs.size(); ++i) {
            auto it = std::ranges::find(ids, vs[i]);
            if (it == ids.end()) {
                ids.push_back(vs[i]);
                points.push_back(m.vertex(vs[i])->position);
                profileSmooth.push_back(false);
                it = ids.end() - 1;
            }
            const auto index = static_cast<std::size_t>(it - ids.begin());
            // Loop use i starts at vs[i]; the use before it ends there.
            if (sameCurve(m, loop[(i + loop.size() - 1) % loop.size()].edge, loop[i].edge)) profileSmooth[index] = true;
            indices.push_back(index);
        }
        for (const EdgeUse& u : loop) profileEdges.push_back(u.edge);
        loops.push_back(std::move(indices));
    }
    const bool freeProfile = std::ranges::all_of(profileEdges, [&m](EdgeId e) { return m.facesOfEdge(e).size() == 1; });

    // Start where the profile is: the nearer end of an open path, the nearest
    // point of a closed one.
    Vec3 centroid{0.0};
    for (const Vec3& p : points) centroid += p;
    centroid /= static_cast<double>(points.size());
    if (!path.closed) {
        if (glm::length(path.points.back() - centroid) < glm::length(path.points.front() - centroid)) {
            std::ranges::reverse(path.points);
            std::reverse(path.smooth.begin(), path.smooth.end());
        }
    } else {
        std::size_t best = 0;
        for (std::size_t i = 1; i < count; ++i) {
            if (glm::length(path.points[i] - centroid) < glm::length(path.points[best] - centroid)) best = i;
        }
        std::ranges::rotate(path.points, path.points.begin() + static_cast<std::ptrdiff_t>(best));
        std::rotate(path.smooth.begin(), path.smooth.begin() + static_cast<std::ptrdiff_t>(best), path.smooth.end());
        // Go round the way the profile faces: the first segment most across it.
        const Vec3 ahead = glm::normalize(path.points[1] - path.points[0]);
        const Vec3 behind = glm::normalize(path.points[count - 1] - path.points[0]);
        if (std::abs(glm::dot(profile.normal, behind)) > std::abs(glm::dot(profile.normal, ahead)) + 1e-9) {
            std::reverse(path.points.begin() + 1, path.points.end());
            std::reverse(path.smooth.begin() + 1, path.smooth.end());
        }
    }

    const std::size_t segments = path.closed ? count : count - 1;
    std::vector<Vec3> d(segments);
    for (std::size_t k = 0; k < segments; ++k) d[k] = glm::normalize(path.points[(k + 1) % count] - path.points[k]);
    const Vec3 firstDir = glm::normalize(path.points[1] - path.points[0]); // d[0], spelled so the optimiser sees it exists
    const double along = glm::dot(profile.normal, firstDir);
    if (std::abs(along) < 1e-6) {
        return Error{"The profile lies along the path. Draw the profile across the path, at its start."};
    }
    const bool forward = along > 0.0;

    const auto mitre = [&](std::size_t k) -> std::optional<Vec3> {
        const Vec3 sum = d[(k + segments - 1) % segments] + d[k % segments];
        if (glm::length(sum) < 1e-6) return std::nullopt;
        return glm::normalize(sum);
    };
    const auto turnsBack = Error{"The path turns back on itself. Pick a path without a U-turn."};

    // Cross-sections: the profile carried along each segment onto the next
    // point's mitre plane (or, at the end of an open path, the square end).
    std::vector<std::vector<Vec3>> sections(count);
    const auto carry = [](const std::vector<Vec3>& from, const Vec3& dir, const Vec3& origin, const Vec3& normal) {
        std::vector<Vec3> out;
        out.reserve(from.size());
        for (const Vec3& p : from) out.push_back(project(p, dir, origin, normal));
        return out;
    };
    if (!path.closed) {
        sections[0] = points;
        for (std::size_t k = 1; k + 1 < count; ++k) {
            const auto mk = mitre(k);
            if (!mk) return turnsBack;
            sections[k] = carry(sections[k - 1], d[k - 1], path.points[k], *mk);
        }
        sections[count - 1] = carry(sections[count - 2], d[count - 2], path.points[count - 1], d[count - 2]);
    } else {
        const auto m0 = mitre(0);
        if (!m0) return turnsBack;
        sections[0] = carry(points, firstDir, path.points[0], *m0);
        for (std::size_t k = 1; k < count; ++k) {
            const auto mk = mitre(k);
            if (!mk) return turnsBack;
            sections[k] = carry(sections[k - 1], d[k - 1], path.points[k], *mk);
        }
        const auto back = carry(sections[count - 1], d[count - 1], path.points[0], *m0);
        for (std::size_t i = 0; i < back.size(); ++i) {
            if (glm::length(back[i] - sections[0][i]) > tol::kWeldDistance) {
                return Error{"The path twists, so the sweep would not meet itself going round. Use an open path."};
            }
        }
    }
    for (const auto& section : sections) {
        for (const Vec3& p : section) {
            for (const double c : {p.x, p.y, p.z}) {
                if (!std::isfinite(c) || std::abs(c) > tol::kMaxCoordinate) {
                    return Error{"The sweep would reach outside the supported range of +/-10 km."};
                }
            }
        }
    }

    // Vertices: section points weld to existing vertices (the profile's own,
    // the path's, and points on the axis that every section shares).
    std::set<std::uint32_t> before;
    for (const auto& [e, edge] : m.edges()) before.insert(e);
    std::vector<std::vector<VertexId>> at(count);
    for (std::size_t k = 0; k < count; ++k) {
        for (const Vec3& p : sections[k]) {
            const auto v = m.findVertex(p);
            at[k].push_back(v ? *v : m.appendVertex(p));
        }
    }

    const auto positions = [&m](const std::vector<VertexId>& ring) {
        std::vector<Vec3> out;
        out.reserve(ring.size());
        for (const VertexId v : ring) out.push_back(m.vertex(v)->position);
        return out;
    };

    // Side faces, per profile edge: one per path segment.
    std::vector<Planned> planned;
    struct EdgeSweep {
        std::size_t a = 0;
        std::size_t b = 0;
    };
    std::vector<EdgeSweep> sweptEdges;
    for (const auto& loop : loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const std::size_t a = loop[i];
            const std::size_t b = loop[(i + 1) % loop.size()];
            sweptEdges.push_back({a, b});
            std::vector<Planned> faces;
            for (std::size_t k = 0; k < segments; ++k) {
                const std::size_t k1 = (k + 1) % count;
                std::vector<VertexId> ring = forward ? std::vector{at[k][a], at[k][b], at[k1][b], at[k1][a]}
                                                     : std::vector{at[k][a], at[k1][a], at[k1][b], at[k][b]};
                const auto clean = cleanRing(ring);
                if (!clean || clean->size() < 3) continue;
                const auto ps = positions(*clean);
                const Vec3 n = loopNormal(ps);
                if (glm::length(n) < 0.5 || std::abs(signedArea(ps, n)) < tol::kMinArea * 1e-4) continue;
                faces.push_back({*clean, n});
            }
            // A fan of triangles round one point, all in one plane (a lathe
            // profile's base or top): one face instead.
            const bool fan = faces.size() >= 2 && std::ranges::all_of(faces, [&](const Planned& f) {
                return f.ring.size() == 3 && 1.0 - glm::dot(f.normal, faces.front().normal) <= tol::kCoplanarNormal;
            });
            if (fan) {
                std::optional<VertexId> apex;
                for (const VertexId v : faces.front().ring) {
                    if (std::ranges::all_of(faces, [v](const Planned& f) { return std::ranges::find(f.ring, v) != f.ring.end(); })) apex = v;
                }
                if (apex) {
                    std::vector<VertexId> ring;
                    if (!path.closed) ring.push_back(*apex);
                    for (std::size_t k = 0; k < count; ++k) ring.push_back(at[k][a] == *apex ? at[k][b] : at[k][a]);
                    if (const auto clean = cleanRing(ring); clean && clean->size() >= 3) {
                        const auto ps = positions(*clean);
                        Vec3 n = loopNormal(ps);
                        std::vector<VertexId> oriented = *clean;
                        if (glm::dot(n, faces.front().normal) < 0.0) {
                            std::ranges::reverse(oriented);
                            n = -n;
                        }
                        faces = {Planned{oriented, n}};
                    }
                }
            }
            planned.insert(planned.end(), faces.begin(), faces.end());
        }
    }

    FollowMeResult result;
    std::set<std::uint32_t> reused;
    const auto ensureEdges = [&m](const std::vector<VertexId>& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const VertexId a = ring[i];
            const VertexId b = ring[(i + 1) % ring.size()];
            if (!m.findEdge(a, b)) m.appendEdge(a, b);
        }
    };
    const auto place = [&](const std::vector<std::vector<VertexId>>& rings, const Vec3& normal) -> bool {
        for (const auto& ring : rings) ensureEdges(ring);
        if (rings.size() == 1) {
            // Same corners as an existing face (a circle the vase stands on): reuse it.
            std::set<std::uint32_t> mine;
            for (const VertexId v : rings[0]) mine.insert(v.value);
            for (const auto& [fid, f] : m.faces()) {
                if (f.loops.size() != 1 || f.loops[0].size() != mine.size()) continue;
                std::set<std::uint32_t> theirs;
                for (const VertexId v : m.loopVertices(f.loops[0])) theirs.insert(v.value);
                if (theirs != mine) continue;
                Face turned = f;
                if (glm::dot(f.normal, normal) < 0.0) {
                    turned.loops[0] = reversed(f.loops[0]);
                    turned.normal = -f.normal;
                }
                const auto id = fid;
                m.insertFace(FaceId{id}, turned);
                reused.insert(id);
                return true;
            }
        }
        const auto face = m.appendFace(rings, normal);
        if (!face) return false;
        result.createdFaces.push_back(*face);
        return true;
    };
    for (const Planned& f : planned) {
        if (!place({f.ring}, f.normal)) return Error{"The sweep could not make its faces."};
    }

    const Vec3 startOut = -firstDir;
    if (!path.closed) {
        // The profile stays as the start cap, facing away from the sweep.
        if (glm::dot(profile.normal, startOut) < 0.0) {
            Face turned = profile;
            for (Loop& loop : turned.loops) loop = reversed(loop);
            turned.normal = -profile.normal;
            m.insertFace(profileId, turned);
        }
        // The end cap: the last section, facing on along the last segment.
        std::vector<std::vector<VertexId>> rings;
        for (const auto& loop : loops) {
            std::vector<VertexId> ring;
            for (const std::size_t i : loop) ring.push_back(at[count - 1][i]);
            rings.push_back(std::move(ring));
        }
        Vec3 n = loopNormal(positions(rings[0]));
        if (glm::dot(n, d[segments - 1]) < 0.0) {
            for (auto& ring : rings) std::ranges::reverse(ring);
            n = -n;
        }
        const std::size_t made = result.createdFaces.size();
        if (!place(rings, n)) return Error{"The sweep could not make its end face."};
        if (result.createdFaces.size() > made) result.endCap = result.createdFaces.back();
    } else if (freeProfile && !reused.contains(profileId.value)) {
        // Round a loop the profile ends up inside the sweep: it goes, with
        // any of its edges the sweep did not use.
        m.eraseFace(profileId);
        for (const EdgeId e : profileEdges) {
            if (m.edge(e) && m.facesOfEdge(e).empty()) m.eraseEdge(e);
        }
    }

    // Soften the edges along profile curves and across path curves.
    const auto soften = [&](VertexId a, VertexId b) {
        const auto e = m.findEdge(a, b);
        if (!e) return;
        const bool own = !before.contains(e->value) || std::ranges::find(profileEdges, *e) != profileEdges.end();
        if (!own || m.facesOfEdge(*e).size() < 2) return;
        Edge flagged = *m.edge(*e);
        flagged.soft = true;
        flagged.smooth = true;
        m.insertEdge(*e, flagged);
    };
    for (const auto& [a, b] : sweptEdges) {
        for (std::size_t k = 0; k < count; ++k) {
            if (path.smooth[k] && (path.closed || (k > 0 && k + 1 < count))) soften(at[k][a], at[k][b]);
            if (k < segments && profileSmooth[a]) soften(at[k][a], at[(k + 1) % count][a]);
        }
    }
    m.pruneCurves();
    return result;
}

} // namespace pf::mesh
