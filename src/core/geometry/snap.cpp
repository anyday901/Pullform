#include "core/geometry/snap.hpp"

#include "core/document/tolerance.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <map>
#include <unordered_set>
#include <utility>

namespace pf {

namespace {

struct ObjectIdHash {
    std::size_t operator()(const ObjectId& id) const noexcept { return std::hash<std::uint64_t>{}(id.value); }
};

// An endpoint wins over a midpoint unless the midpoint is clearly nearer the
// pointer (its angular distance below this fraction of the endpoint's).
constexpr double kEndpointPreference = 0.6;
// Candidates whose angular distances differ by less than this fraction of the
// snap radius count as the same screen position.
constexpr double kSameOnScreen = 0.02;

int indexOf(std::vector<Vec3>& vertices, const Vec3& p) {
    // Corners shared between faces are computed once per object (bottom/top
    // arrays in buildFaces), so exact comparison is reliable.
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (vertices[i] == p) return static_cast<int>(i);
    }
    vertices.push_back(p);
    return static_cast<int>(vertices.size() - 1);
}

// Point of segment a-b closest to the line through `origin` along unit `dir`.
// Nothing for a degenerate segment.
std::optional<Vec3> closestOnSegment(const Vec3& a, const Vec3& b, const Vec3& origin, const Vec3& dir) {
    const Vec3 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    if (len2 <= 0.0) return std::nullopt;
    const Vec3 w = a - origin;
    const double bd = glm::dot(ab, dir);
    const double denom = len2 - bd * bd; // |ab|^2 |dir|^2 - (ab.dir)^2
    double u = 0.0;
    if (denom > 1e-12 * len2) {
        u = (bd * glm::dot(w, dir) - glm::dot(w, ab)) / denom;
    } else {
        u = 0.5; // parallel to the ray: every point is as close; take the middle
    }
    return a + ab * std::clamp(u, 0.0, 1.0);
}

// Running best candidate of SnapIndex::nearest.
struct NearestSearch {
    NearestSearch(const Ray& ray, double radiusPerDistance, std::optional<double> occluderDistance)
        : origin(ray.origin), dir(glm::normalize(ray.direction)), radius(radiusPerDistance), occluder(occluderDistance) {}

    Vec3 origin;
    Vec3 dir; ///< normalized
    double radius; ///< radians
    std::optional<double> occluder;
    SnapResult best;
    bool found = false;
    double bestScore = 0.0;
    double bestDepth = 0.0;

    /// Bounding-sphere test: false when no feature of `f` can be within the radius.
    [[nodiscard]] bool mayReach(const SnapFeatures& f) const {
        const Vec3 w = f.center - origin;
        const double t = glm::dot(w, dir);
        return glm::length(w - dir * t) <= f.radius + radius * (std::max(t, 0.0) + f.radius);
    }

    void consider(SnapKind kind, ObjectId id, int feature, const Vec3& p) {
        // Angular distance of p from the ray: perpendicular distance over depth.
        const Vec3 w = p - origin;
        const double t = glm::dot(w, dir);
        if (t <= 0.0) return;
        const double s = glm::length(w - dir * t) / t;
        if (s > radius) return;
        if (occluder && t > *occluder + radius * t) return; // behind the face under the pointer
        const double weighted = kind == SnapKind::Endpoint ? s * kEndpointPreference : s;
        if (!beats(weighted, t)) return;
        bestScore = weighted;
        bestDepth = t;
        best = SnapResult{p, kind, id, feature, p, {}};
        found = true;
    }

    [[nodiscard]] bool beats(double weighted, double depth) const {
        if (!found) return true;
        // Points that look the same on screen (a vertical edge seen from
        // above) go to the one nearest the eye.
        const double slack = kSameOnScreen * radius;
        if (weighted > bestScore + slack) return false;
        return weighted < bestScore - slack || depth < bestDepth;
    }
};

} // namespace

const char* snapKindName(SnapKind kind) {
    using enum SnapKind;
    switch (kind) {
    case None: return "";
    case Grid: return "Grid";
    case OnFace: return "On Face";
    case Inference: return "From Point";
    case Axis: return "On Axis";
    case Parallel: return "Parallel to Edge";
    case Perpendicular: return "Perpendicular to Edge";
    case OnEdge: return "On Edge";
    case Intersection: return "Intersection";
    case Center: return "Center";
    case OnGuide: return "On Guide";
    case GuidePoint: return "Guide Point";
    case Midpoint: return "Midpoint";
    case Endpoint: return "Endpoint";
    }
    return "";
}

std::string snapLabel(const SnapResult& snap) {
    static constexpr const char* kAxisNames[] = {"On X Axis", "On Y Axis", "On Z Axis"};
    std::string text = snapKindName(snap.kind);
    const auto& c = snap.constraint;
    // Axis lines name their axis; a remembered point names the axis it lines up on.
    if (snap.kind == SnapKind::Axis && c && c->axis >= 0 && c->axis < 3) text = kAxisNames[c->axis];
    if (snap.locked && !text.empty()) text += " (locked)";
    return text;
}

Vec3 SnapEdge::direction() const {
    const Vec3 d = b - a;
    const double len = glm::length(d);
    return len > tol::kLengthEpsilon ? d / len : Vec3{0.0};
}

namespace {

// A mesh group snaps to every vertex and edge, including loose edges that
// bound no face yet (a Line tool chain before it closes), plus curve centers.
SnapFeatures meshFeatures(const ProfileObject& object) {
    SnapFeatures f;
    const mesh::Mesh& m = *object.mesh;
    std::map<std::uint32_t, int> index;
    // A vertex only hidden edges meet is hidden with them: it does not snap.
    std::map<std::uint32_t, bool> visible;
    for (const auto& [id, e] : m.edges()) {
        for (const auto v : {e.v0.value, e.v1.value}) visible[v] = visible[v] || !e.hidden;
    }
    for (const auto& [id, v] : m.vertices()) {
        if (const auto it = visible.find(id); it != visible.end() && !it->second) continue;
        index.emplace(id, static_cast<int>(f.vertices.size()));
        f.vertices.push_back(localToWorld(object, v.position));
    }
    for (const auto& [id, e] : m.edges()) {
        if (e.soft || e.hidden) continue; // hidden geometry neither draws nor snaps
        const auto a = index.find(e.v0.value);
        const auto b = index.find(e.v1.value);
        if (a == index.end() || b == index.end() || a->second == b->second) continue;
        f.edges.emplace_back(a->second, b->second);
    }
    for (const auto& [id, c] : m.curves()) f.curveCenters.push_back(localToWorld(object, c.center));
    return f;
}

void computeBounds(SnapFeatures& f) {
    if (f.vertices.empty() && f.curveCenters.empty()) return;
    Vec3 lo = f.vertices.empty() ? f.curveCenters.front() : f.vertices.front();
    Vec3 hi = lo;
    for (const auto& v : f.vertices) {
        lo = glm::min(lo, v);
        hi = glm::max(hi, v);
    }
    for (const auto& v : f.curveCenters) {
        lo = glm::min(lo, v);
        hi = glm::max(hi, v);
    }
    f.center = (lo + hi) * 0.5;
    f.radius = glm::length(hi - lo) * 0.5;
}

} // namespace

SnapFeatures snapFeatures(const ProfileObject& object, std::optional<double> heightOverride) {
    if (object.isMeshGroup()) {
        SnapFeatures f = meshFeatures(object);
        computeBounds(f);
        return f;
    }
    // A profile object (rectangle or box): its logical faces.
    SnapFeatures f;
    const auto faces = buildFaces(object, heightOverride);
    for (const auto& face : faces) {
        const auto n = face.loop.size();
        std::vector<int> ids;
        ids.reserve(n);
        for (const auto& p : face.loop) ids.push_back(indexOf(f.vertices, p));
        for (std::size_t i = 0; i < n; ++i) {
            const int a = ids[i];
            const int b = ids[(i + 1) % n];
            const bool seen = std::ranges::any_of(f.edges, [a, b](const std::pair<int, int>& e) {
                return (e.first == a && e.second == b) || (e.first == b && e.second == a);
            });
            if (!seen && a != b) f.edges.emplace_back(a, b);
        }
    }
    // Curve centers come from the mesh (a circle or polygon profile makes one
    // per loop; drawn arcs keep theirs), at the previewed height if there is one.
    const ProfileObject* source = &object;
    ProfileObject resized;
    if (heightOverride && !object.isMeshGroup() && object.extrusion) {
        resized = object;
        resized.extrusion->height = *heightOverride;
        source = &resized;
    }
    if (const auto m = mesh::objectMesh(*source); !m->curves().empty()) {
        for (const auto& [id, c] : m->curves()) f.curveCenters.push_back(localToWorld(*source, c.center));
    }
    computeBounds(f);
    return f;
}

bool SnapIndex::sync(const Document& document) {
    if (document.objects == synced_) return false;
    std::unordered_set<ObjectId, ObjectIdHash> live;
    live.reserve(document.objects.size());
    for (const auto& obj : document.objects) {
        if (obj) live.insert(obj->id);
    }
    std::erase_if(entries_, [&live](const auto& kv) { return !live.contains(kv.first); });
    for (const auto& obj : document.objects) {
        if (!obj) continue;
        auto& entry = entries_[obj->id];
        if (entry.source == obj) continue;
        entry.source = obj;
        entry.features = snapFeatures(*obj);
    }
    synced_ = document.objects;
    return true;
}

void SnapIndex::clear() {
    entries_.clear();
    synced_.clear();
}

const SnapFeatures* SnapIndex::find(ObjectId id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second.features;
}

std::optional<SnapResult> SnapIndex::nearest(const Ray& ray, double radiusPerDistance, std::optional<double> occluder,
                                             std::optional<ObjectId> exclude) const {
    if (!(radiusPerDistance > 0.0)) return std::nullopt;
    NearestSearch search(ray, radiusPerDistance, occluder);
    for (const auto& [id, entry] : entries_) {
        if (exclude && *exclude == id) continue;
        const auto& f = entry.features;
        if (!search.mayReach(f)) continue;
        for (std::size_t i = 0; i < f.vertices.size(); ++i) {
            search.consider(SnapKind::Endpoint, id, static_cast<int>(i), f.vertices[i]);
        }
        for (std::size_t i = 0; i < f.edges.size(); ++i) {
            search.consider(SnapKind::Midpoint, id, static_cast<int>(i), f.midpoint(i));
        }
        for (std::size_t i = 0; i < f.curveCenters.size(); ++i) {
            search.consider(SnapKind::Center, id, static_cast<int>(i), f.curveCenters[i]);
        }
    }
    if (!search.found) return std::nullopt;
    SnapResult best = std::move(search.best);
    if (best.kind == SnapKind::Midpoint && best.objectId) {
        // A midpoint knows its edge: it can serve as a parallel reference.
        const auto& f = entries_.at(*best.objectId).features;
        const auto [a, b] = f.edges[static_cast<std::size_t>(best.feature)];
        best.edge = SnapEdge{*best.objectId, best.feature, f.vertices[static_cast<std::size_t>(a)], f.vertices[static_cast<std::size_t>(b)]};
    }
    return best;
}

std::optional<SnapResult> SnapIndex::nearestEdge(const Ray& ray, double radiusPerDistance, std::optional<double> occluder,
                                                 std::optional<ObjectId> exclude, std::optional<SnapResult>* second) const {
    if (!(radiusPerDistance > 0.0)) return std::nullopt;
    const Vec3 origin = ray.origin;
    const Vec3 dir = glm::normalize(ray.direction);
    struct Candidate {
        double score = 0.0;
        double depth = 0.0;
        SnapResult result;
    };
    std::optional<Candidate> best;
    std::optional<Candidate> next;
    NearestSearch reach(ray, radiusPerDistance, occluder);
    for (const auto& [id, entry] : entries_) {
        if (exclude && *exclude == id) continue;
        const auto& f = entry.features;
        if (!reach.mayReach(f)) continue;
        for (std::size_t i = 0; i < f.edges.size(); ++i) {
            const Vec3& a = f.vertices[static_cast<std::size_t>(f.edges[i].first)];
            const Vec3& b = f.vertices[static_cast<std::size_t>(f.edges[i].second)];
            const auto p = closestOnSegment(a, b, origin, dir);
            if (!p) continue;
            const Vec3 w = *p - origin;
            const double t = glm::dot(w, dir);
            if (t <= 0.0) continue;
            const double s = glm::length(w - dir * t) / t;
            if (s > radiusPerDistance) continue;
            if (occluder && t > *occluder + radiusPerDistance * t) continue;
            Candidate c{s, t, SnapResult{*p, SnapKind::OnEdge, id, -1, *p, {}}};
            c.result.edge = SnapEdge{id, static_cast<int>(i), a, b};
            c.result.constraint = SnapConstraint{SnapConstraint::Type::Line, a, c.result.edge->direction(), SnapKind::OnEdge, -1, c.result.edge};
            // Edges that look the same on screen go to the one nearest the eye.
            const double slack = kSameOnScreen * radiusPerDistance;
            const auto beats = [&](const std::optional<Candidate>& o) {
                return !o || c.score < o->score - slack || (c.score <= o->score + slack && c.depth < o->depth);
            };
            if (beats(best)) {
                next = std::move(best);
                best = std::move(c);
            } else if (beats(next)) {
                next = std::move(c);
            }
        }
    }
    if (second) *second = next ? std::optional(next->result) : std::nullopt;
    if (!best) return std::nullopt;
    return best->result;
}

std::vector<SnapEdge> SnapIndex::edgesAt(const Vec3& point) const {
    std::vector<SnapEdge> out;
    for (const auto& [id, entry] : entries_) {
        const auto& f = entry.features;
        if (glm::length(point - f.center) > f.radius + tol::kWeldDistance) continue;
        for (std::size_t i = 0; i < f.edges.size(); ++i) {
            const Vec3& a = f.vertices[static_cast<std::size_t>(f.edges[i].first)];
            const Vec3& b = f.vertices[static_cast<std::size_t>(f.edges[i].second)];
            const Vec3 ab = b - a;
            const double len2 = glm::dot(ab, ab);
            if (len2 <= 0.0) continue;
            const double u = std::clamp(glm::dot(point - a, ab) / len2, 0.0, 1.0);
            if (glm::length(a + ab * u - point) <= tol::kWeldDistance) out.push_back(SnapEdge{id, static_cast<int>(i), a, b});
        }
    }
    return out;
}

std::optional<SnapResult> SnapIndex::nearestHeight(const Vec3& axisPoint, double tolerance,
                                                   std::optional<ObjectId> exclude) const {
    std::optional<SnapResult> best;
    double bestDz = tolerance;
    double bestPlan = 0.0;
    for (const auto& [id, entry] : entries_) {
        if (exclude && *exclude == id) continue;
        const auto& vertices = entry.features.vertices;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3& v = vertices[i];
            const double dz = std::abs(v.z - axisPoint.z);
            const double plan = glm::length(Vec2{v.x - axisPoint.x, v.y - axisPoint.y});
            const bool sameHeight = best && std::abs(dz - bestDz) <= tol::kLengthEpsilon;
            const bool better = sameHeight ? plan < bestPlan : dz < bestDz;
            if (!better) continue;
            bestDz = dz;
            bestPlan = plan;
            best = SnapResult{v, SnapKind::Inference, id, static_cast<int>(i), v, {}};
        }
    }
    return best;
}

std::optional<Vec3> resolveSnapFeature(const Document& document, ObjectId id, SnapKind kind, int feature) {
    const auto* o = document.find(id);
    if (!o || feature < 0) return std::nullopt;
    const auto f = snapFeatures(*o);
    const auto i = static_cast<std::size_t>(feature);
    if (kind == SnapKind::Endpoint && i < f.vertices.size()) return f.vertices[i];
    if (kind == SnapKind::Midpoint && i < f.edges.size()) return f.midpoint(i);
    if (kind == SnapKind::Center && i < f.curveCenters.size()) return f.curveCenters[i];
    return std::nullopt;
}

} // namespace pf
