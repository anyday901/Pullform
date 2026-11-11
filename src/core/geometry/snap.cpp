#include "core/geometry/snap.hpp"

#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <unordered_set>

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

} // namespace

const char* snapKindName(SnapKind kind) {
    switch (kind) {
    case SnapKind::None: return "";
    case SnapKind::Grid: return "Grid";
    case SnapKind::OnFace: return "On face";
    case SnapKind::Inference: return "Inference";
    case SnapKind::Midpoint: return "Midpoint";
    case SnapKind::Endpoint: return "Endpoint";
    }
    return "";
}

SnapFeatures snapFeatures(const ProfileObject& object, std::optional<double> heightOverride) {
    SnapFeatures f;
    for (const auto& face : buildFaces(object, heightOverride)) {
        const auto n = face.loop.size();
        std::vector<int> ids;
        ids.reserve(n);
        for (const auto& p : face.loop) ids.push_back(indexOf(f.vertices, p));
        for (std::size_t i = 0; i < n; ++i) {
            const int a = ids[i];
            const int b = ids[(i + 1) % n];
            const bool seen = std::any_of(f.edges.begin(), f.edges.end(), [a, b](const std::pair<int, int>& e) {
                return (e.first == a && e.second == b) || (e.first == b && e.second == a);
            });
            if (!seen && a != b) f.edges.emplace_back(a, b);
        }
    }
    if (!f.vertices.empty()) {
        Vec3 lo = f.vertices.front(), hi = lo;
        for (const auto& v : f.vertices) {
            lo = glm::min(lo, v);
            hi = glm::max(hi, v);
        }
        f.center = (lo + hi) * 0.5;
        f.radius = glm::length(hi - lo) * 0.5;
    }
    return f;
}

bool SnapIndex::sync(const Document& document) {
    if (document.objects == synced_) return false;
    std::unordered_set<ObjectId, ObjectIdHash> live;
    live.reserve(document.objects.size());
    for (const auto& obj : document.objects) {
        if (obj) live.insert(obj->id);
    }
    std::erase_if(entries_, [&](const auto& kv) { return !live.contains(kv.first); });
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
    const Vec3 dir = glm::normalize(ray.direction);

    std::optional<SnapResult> best;
    double bestScore = 0.0;
    double bestDepth = 0.0;
    const auto consider = [&](SnapKind kind, ObjectId id, int feature, const Vec3& p) {
        // Angular distance of p from the ray: perpendicular distance over depth.
        const Vec3 w = p - ray.origin;
        const double t = glm::dot(w, dir);
        if (t <= 0.0) return;
        const double s = glm::length(w - dir * t) / t;
        if (s > radiusPerDistance) return;
        if (occluder && t > *occluder + radiusPerDistance * t) return; // behind the face under the pointer
        const double weighted = kind == SnapKind::Endpoint ? s * kEndpointPreference : s;
        if (best) {
            // Points that look the same on screen (a vertical edge seen from
            // above) go to the one nearest the eye.
            const double slack = kSameOnScreen * radiusPerDistance;
            if (weighted > bestScore + slack) return;
            if (weighted >= bestScore - slack && t >= bestDepth) return;
        }
        bestScore = weighted;
        bestDepth = t;
        best = SnapResult{p, kind, id, feature, p, {}};
    };

    for (const auto& [id, entry] : entries_) {
        if (exclude && *exclude == id) continue;
        const auto& f = entry.features;
        // Bounding-sphere early out: skip objects nowhere near the ray.
        const Vec3 w = f.center - ray.origin;
        const double t = glm::dot(w, dir);
        const double perp = glm::length(w - dir * t);
        if (perp > f.radius + radiusPerDistance * (std::max(t, 0.0) + f.radius)) continue;
        for (std::size_t i = 0; i < f.vertices.size(); ++i) {
            consider(SnapKind::Endpoint, id, static_cast<int>(i), f.vertices[i]);
        }
        for (std::size_t i = 0; i < f.edges.size(); ++i) {
            consider(SnapKind::Midpoint, id, static_cast<int>(i), f.midpoint(i));
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
    return std::nullopt;
}

} // namespace pf
