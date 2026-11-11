#include "app/viewport/mesh_cache.hpp"

#include <unordered_set>

namespace pf {

namespace {

struct ObjectIdHash {
    std::size_t operator()(const ObjectId& id) const noexcept { return std::hash<std::uint64_t>{}(id.value); }
};

} // namespace

bool MeshCache::sync(const Document& document) {
    // Fast path: objects are immutable and every edit replaces the pointer, so
    // an identical pointer list means nothing to rebuild.
    if (document.objects == synced_) return false;

    std::unordered_set<ObjectId, ObjectIdHash> live;
    live.reserve(document.objects.size());
    for (const auto& obj : document.objects) {
        if (obj) live.insert(obj->id);
    }

    // Drop meshes for objects that no longer exist.
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!live.contains(it->first)) {
            renderer_.destroyMesh(it->second.handle);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    // Rebuild meshes whose document entry changed (pointer identity).
    for (const auto& obj : document.objects) {
        if (!obj) continue;
        auto& cached = entries_[obj->id];
        if (cached.source == obj) continue;
        if (cached.handle != kNoMesh) renderer_.destroyMesh(cached.handle);
        cached.source = obj;
        cached.mesh = buildMesh(*obj);
        cached.topology = buildTopology(*obj);
        cached.handle = renderer_.createMesh(cached.mesh);
    }
    synced_ = document.objects;
    return true;
}

void MeshCache::clear() {
    for (auto& [id, cached] : entries_) {
        if (cached.handle != kNoMesh) renderer_.destroyMesh(cached.handle);
    }
    entries_.clear();
    synced_.clear();
}

std::optional<PickHit> MeshCache::pick(const Ray& ray) const {
    std::optional<PickHit> best;
    for (const auto& [id, cached] : entries_) {
        const auto hit = pickObject(ray, *cached.source, cached.mesh);
        if (hit && (!best || hit->distance < best->distance)) best = hit;
    }
    return best;
}

std::optional<ElementHit> MeshCache::pickElement(const Ray& ray, const ElementPickOptions& options) const {
    std::vector<PickTarget> targets;
    targets.reserve(entries_.size());
    for (const auto& [id, cached] : entries_) targets.push_back({id, &cached.topology});
    return pf::pickElement(ray, targets, options);
}

const MeshCache::Entry* MeshCache::find(ObjectId id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : &it->second;
}

} // namespace pf
