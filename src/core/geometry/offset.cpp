#include "core/geometry/offset.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace pf::mesh {

namespace {

/// Orthonormal in-plane axes with u x v = n, so counter-clockwise about n is
/// counter-clockwise in (u, v).
struct PlaneFrame {
    Vec3 origin{0.0};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};

    [[nodiscard]] Vec2 to2d(const Vec3& p) const {
        const Vec3 d = p - origin;
        return {glm::dot(d, u), glm::dot(d, v)};
    }
    [[nodiscard]] Vec3 to3d(const Vec2& p) const { return origin + u * p.x + v * p.y; }
};

PlaneFrame frameFor(const Vec3& origin, const Vec3& n) {
    const Vec3 ax = std::abs(n.x) < 0.6 ? Vec3{1, 0, 0} : (std::abs(n.y) < 0.6 ? Vec3{0, 1, 0} : Vec3{0, 0, 1});
    const Vec3 u = glm::normalize(glm::cross(ax, n));
    return {origin, u, glm::cross(n, u)};
}

double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

double area2d(const std::vector<Vec2>& poly) {
    double a = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) a += cross2(poly[i], poly[(i + 1) % poly.size()]);
    return a * 0.5;
}

bool inside2d(const Vec2& p, const std::vector<Vec2>& poly) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

/// An edge moved sideways: the line through `point` along unit `dir`.
struct OffsetLine {
    Vec2 point{0.0};
    Vec2 dir{1.0, 0.0};
    std::size_t edge = 0; ///< source edge index in the loop
};

/// Where two consecutive offset lines meet (the mitred corner).
std::optional<Vec2> meet(const OffsetLine& a, const OffsetLine& b) {
    const double c = cross2(a.dir, b.dir);
    if (std::abs(c) < 1e-9) {
        // Parallel: an edge split in two keeps its corner where the second piece starts.
        if (glm::dot(a.dir, b.dir) > 0.0) return b.point;
        return std::nullopt; // folds back on itself: the loop closed up
    }
    const double t = cross2(b.point - a.point, b.dir) / c;
    return a.point + a.dir * t;
}

struct Offset2d {
    std::vector<Vec2> points;
    std::vector<std::size_t> edges;
};

/// Offsets a closed polygon by `d` to the left of each edge. Edges that turn
/// round (shrink past nothing) are dropped and their neighbours meet instead,
/// until none does. Nothing when fewer than three edges are left.
std::optional<Offset2d> offsetPolygon(const std::vector<Vec2>& poly, double d) {
    const std::size_t n = poly.size();
    std::vector<OffsetLine> lines;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 e = poly[(i + 1) % n] - poly[i];
        const double len = glm::length(e);
        if (len <= 0.0) continue;
        const Vec2 dir = e / len;
        lines.push_back({poly[i] + Vec2{-dir.y, dir.x} * d, dir, i});
    }
    for (std::size_t round = 0; round <= n; ++round) {
        const std::size_t m = lines.size();
        if (m < 3) return std::nullopt;
        std::vector<Vec2> corners(m);
        for (std::size_t i = 0; i < m; ++i) {
            const auto c = meet(lines[(i + m - 1) % m], lines[i]);
            if (!c) return std::nullopt;
            corners[i] = *c;
        }
        std::vector<bool> collapsed(m, false);
        bool any = false;
        for (std::size_t i = 0; i < m; ++i) {
            if (glm::dot(corners[(i + 1) % m] - corners[i], lines[i].dir) <= tol::kWeldDistance) {
                collapsed[i] = true;
                any = true;
            }
        }
        if (!any) {
            Offset2d out{std::move(corners), {}};
            for (const auto& l : lines) out.edges.push_back(l.edge);
            return out;
        }
        std::vector<OffsetLine> kept;
        for (std::size_t i = 0; i < m; ++i) {
            if (!collapsed[i]) kept.push_back(lines[i]);
        }
        lines = std::move(kept);
    }
    return std::nullopt;
}

/// Whether closed segments ab and cd touch or cross.
bool segmentsMeet(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const double eps = tol::kWeldDistance;
    const auto side = [](const Vec2& p, const Vec2& q, const Vec2& r) { return cross2(q - p, r - p); };
    const auto distToSeg = [](const Vec2& p, const Vec2& s0, const Vec2& s1) {
        const Vec2 e = s1 - s0;
        const double len2 = glm::dot(e, e);
        const double t = len2 > 0.0 ? std::clamp(glm::dot(p - s0, e) / len2, 0.0, 1.0) : 0.0;
        return glm::length(p - (s0 + e * t));
    };
    const double d1 = side(c, d, a);
    const double d2 = side(c, d, b);
    const double d3 = side(a, b, c);
    const double d4 = side(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
    return distToSeg(a, c, d) <= eps || distToSeg(b, c, d) <= eps || distToSeg(c, a, b) <= eps || distToSeg(d, a, b) <= eps;
}

bool selfCrossing(const std::vector<Vec2>& p) {
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue; // neighbours share a corner
            if (segmentsMeet(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return true;
        }
    }
    return false;
}

bool loopsCross(const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < b.size(); ++j) {
            if (segmentsMeet(a[i], a[(i + 1) % a.size()], b[j], b[(j + 1) % b.size()])) return true;
        }
    }
    return false;
}

Loop reversed(const Loop& loop) {
    Loop out;
    for (auto it = loop.rbegin(); it != loop.rend(); ++it) out.push_back(EdgeUse{it->edge, !it->reversed});
    return out;
}

Face flipped(const Face& f) {
    Face out{{}, -f.normal};
    for (const auto& l : f.loops) out.loops.push_back(reversed(l));
    return out;
}

bool sameEdges(const Loop& a, const Loop& b) {
    if (a.size() != b.size()) return false;
    std::set<std::uint32_t> sa;
    std::set<std::uint32_t> sb;
    for (const auto& u : a) sa.insert(u.edge.value);
    for (const auto& u : b) sb.insert(u.edge.value);
    return sa == sb;
}

bool coplanar(const Face& a, const Vec3& normal) { return std::abs(glm::dot(a.normal, normal)) >= 1.0 - 1e-9; }

/// The curve an offset edge follows, or nothing for a straight edge.
std::optional<CurveId> curveOf(const Mesh& m, const Loop& loop, std::size_t edge) {
    const Edge* e = m.edge(loop[edge].edge);
    return e ? e->curve : std::nullopt;
}

/// The same curve kind and plane through `points` (k + 1 points, ends
/// included), with the radius of its inner points; nothing when there are none.
std::optional<Curve> offsetCurve(const Curve& original, const std::vector<Vec3>& points) {
    if (points.size() < 3) return std::nullopt;
    double radius = 0.0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i) radius += glm::length(points[i] - original.center);
    radius /= static_cast<double>(points.size() - 2);
    if (radius < tol::kMinDimension) return std::nullopt;
    const Vec3 y = glm::cross(original.normal, original.xAxis);
    const auto angleOf = [&](const Vec3& p) {
        const Vec3 d = p - original.center;
        return std::atan2(glm::dot(d, y), glm::dot(d, original.xAxis));
    };
    double sweep = 0.0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec3 a = points[i] - original.center;
        const Vec3 b = points[i + 1] - original.center;
        sweep += std::atan2(glm::dot(glm::cross(a, b), original.normal), glm::dot(a, b));
    }
    Curve c = original;
    c.radius = radius;
    c.start = angleOf(points.front());
    c.sweep = sweep;
    c.segments = static_cast<int>(points.size() - 1);
    c.edges.clear();
    return c;
}

/// Draws one offset loop into `m`, keeping arcs and circles as curves.
void drawLoop(Mesh& m, const Loop& source, const OffsetLoop& loop) {
    const std::size_t n = loop.points.size();
    const std::size_t sourceCount = source.size();
    // Offset edge i continues a curve run from edge i - 1 when both follow the
    // same curve along consecutive source edges.
    std::vector<std::optional<CurveId>> tags(n);
    for (std::size_t i = 0; i < n; ++i) tags[i] = curveOf(m, source, loop.sourceEdges[i]);
    const auto continues = [&](std::size_t i) {
        const std::size_t prev = (i + n - 1) % n;
        return tags[i].has_value() && tags[i] == tags[prev] && (loop.sourceEdges[prev] + 1) % sourceCount == loop.sourceEdges[i];
    };
    std::size_t start = 0;
    while (start < n && continues(start)) ++start;
    const bool wholeCurve = start == n; // a full circle or polygon
    if (wholeCurve) start = 0;

    std::size_t i = 0;
    while (i < n) {
        std::size_t k = 1; // edges in this run
        while (i + k < n && continues((start + i + k) % n)) ++k;
        const std::size_t first = (start + i) % n;
        std::vector<Vec3> points;
        for (std::size_t j = 0; j <= k; ++j) points.push_back(loop.points[(first + j) % n]);
        const Curve* original = tags[first] ? m.curve(*tags[first]) : nullptr;
        const auto curve = original && k >= 2 ? offsetCurve(*original, points) : std::nullopt;
        if (curve) {
            m.addCurve(*curve, points);
        } else {
            for (std::size_t j = 0; j < k; ++j) m.addEdge(points[j], points[j + 1]);
        }
        i += k;
    }
}

} // namespace

Expected<std::vector<OffsetLoop>> offsetLoops(const Mesh& mesh, FaceId id, double distance) {
    const Face* f = mesh.face(id);
    if (!f) return Error{"That face no longer exists."};
    if (!std::isfinite(distance) || std::abs(distance) < tol::kMinDimension) {
        return Error{"Offset distance must be at least 0.1 mm."};
    }
    const PlaneFrame frame = frameFor(mesh.loopPositions(f->loops[0]).front(), f->normal);
    std::vector<OffsetLoop> out;
    std::vector<std::vector<Vec2>> shapes;
    for (std::size_t l = 0; l < f->loops.size(); ++l) {
        std::vector<Vec2> poly;
        for (const Vec3& p : mesh.loopPositions(f->loops[l])) poly.push_back(frame.to2d(p));
        const double before = area2d(poly);
        auto moved = offsetPolygon(poly, distance);
        if (!moved) {
            if (l == 0) return Error{"The offset is too large for this face."};
            continue; // a hole shrinking to nothing is simply gone
        }
        std::vector<Vec2> points = std::move(moved->points);
        const double after = area2d(points);
        // The loop closed up, or turned inside out (its winding flipped).
        if (std::abs(after) <= tol::kMinArea || (after > 0.0) != (before > 0.0)) {
            if (l == 0) return Error{"The offset is too large for this face."};
            continue;
        }
        if (selfCrossing(points)) return Error{"The offset is too large for this face: its edges would cross."};
        OffsetLoop loop;
        loop.source = l;
        for (const Vec2& p : points) loop.points.push_back(frame.to3d(p));
        loop.sourceEdges = std::move(moved->edges);
        shapes.push_back(std::move(points));
        out.push_back(std::move(loop));
    }
    for (std::size_t a = 0; a < shapes.size(); ++a) {
        for (std::size_t b = a + 1; b < shapes.size(); ++b) {
            if (loopsCross(shapes[a], shapes[b])) return Error{"The offset is too large for this face: its loops would cross."};
        }
    }
    // Inward, the holes grow: each must stay inside the shrunken outer loop.
    for (std::size_t h = 1; h < shapes.size(); ++h) {
        if (!inside2d(shapes[h].front(), shapes[0])) return Error{"The offset is too large for this face."};
    }
    return out;
}

double signedBoundaryDistance(const Mesh& mesh, FaceId id, const Vec3& point) {
    const Face* f = mesh.face(id);
    if (!f) return 0.0;
    const PlaneFrame frame = frameFor(mesh.loopPositions(f->loops[0]).front(), f->normal);
    const Vec2 p = frame.to2d(point);
    double nearest = std::numeric_limits<double>::infinity();
    bool inside = false;
    for (std::size_t l = 0; l < f->loops.size(); ++l) {
        std::vector<Vec2> poly;
        for (const Vec3& q : mesh.loopPositions(f->loops[l])) poly.push_back(frame.to2d(q));
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Vec2 a = poly[i];
            const Vec2 e = poly[(i + 1) % poly.size()] - a;
            const double len2 = glm::dot(e, e);
            const double t = len2 > 0.0 ? std::clamp(glm::dot(p - a, e) / len2, 0.0, 1.0) : 0.0;
            nearest = std::min(nearest, glm::length(p - (a + e * t)));
        }
        const bool in = inside2d(p, poly);
        if (l == 0) inside = in;
        else if (in) inside = false;
    }
    return inside ? nearest : -nearest;
}

Expected<OffsetResult> offsetFace(Mesh& mesh, FaceId id, double distance) {
    auto loops = offsetLoops(mesh, id, distance);
    if (!loops) return Error{loops.error()};
    const Face original = *mesh.face(id);
    const Vec3 normal = original.normal;
    Mesh m = mesh;
    const std::uint32_t firstEdge = m.nextEdgeId();
    const std::uint32_t firstFace = m.nextFaceId();

    OffsetResult result;
    for (const OffsetLoop& loop : *loops) {
        const Loop& source = original.loops[loop.source];
        // A hole with no face in it (on an outward offset, the shrunken hole is drawn there).
        const bool emptyHole = loop.source > 0 && std::ranges::none_of(m.faces(), [&](const auto& kv) {
            return sameEdges(kv.second.loops[0], source);
        });
        const std::uint32_t facesBefore = m.nextFaceId();
        drawLoop(m, source, loop);
        std::vector<FaceId> made;
        for (const auto& [fid, f] : m.faces()) {
            if (fid >= facesBefore && coplanar(f, normal)) made.push_back(FaceId{fid});
        }
        if (distance < 0.0 && loop.source == 0) {
            // The ring around the face holds the face's outer loop as a hole.
            for (const FaceId fid : made) {
                const Face& f = *m.face(fid);
                if (std::ranges::any_of(f.loops, [&](const Loop& l) { return sameEdges(l, source); })) result.ring = fid;
            }
        }
        if (distance < 0.0 && emptyHole) {
            // Drawing the shrunken hole in empty space filled it with a face;
            // the face belongs between the hole and its offset instead.
            for (const FaceId fid : made) {
                const Face& f = *m.face(fid);
                if (f.loops.size() != 1) continue;
                Loop inner = f.loops[0];
                if (glm::dot(f.normal, normal) > 0.0) inner = reversed(inner);
                m.insertFace(fid, Face{{reversed(source), inner}, normal});
                break;
            }
        }
    }
    if (distance > 0.0 && m.face(id)) result.ring = id;
    if (distance < 0.0) {
        // A hole that closed up on the way out is filled, as a smaller offset
        // would have filled all but its middle. A hole with a face in it already is left as it is.
        for (std::size_t h = 1; h < original.loops.size(); ++h) {
            if (std::ranges::any_of(*loops, [h](const OffsetLoop& l) { return l.source == h; })) continue;
            const Loop& hole = original.loops[h];
            const bool filled = std::ranges::any_of(m.faces(), [&](const auto& kv) { return sameEdges(kv.second.loops[0], hole); });
            if (!filled) m.insertFace(FaceId{m.nextFaceId()}, Face{{reversed(hole)}, normal});
        }
    }

    // Faces made in the face's plane face the same way as it.
    for (const auto& [fid, f] : m.faces()) {
        if (fid < firstFace) continue;
        if (coplanar(f, normal) && glm::dot(f.normal, normal) < 0.0) m.insertFace(FaceId{fid}, flipped(f));
        result.createdFaces.push_back(FaceId{fid});
    }
    for (const auto& [eid, e] : m.edges()) {
        if (eid >= firstEdge) result.createdEdges.push_back(EdgeId{eid});
    }
    if (auto e = validateMesh(m, "offset")) return Error{"The offset could not be drawn here (" + *e + ")."};
    mesh = std::move(m);
    return result;
}

} // namespace pf::mesh
