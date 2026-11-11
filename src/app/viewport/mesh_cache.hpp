#pragma once

#include "app/viewport/renderer.hpp"
#include "core/document/document.hpp"
#include "core/geometry/element_pick.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/topology.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace pf {

/// Derived render meshes for the objects of a document, one per object ID,
/// uploaded through a Renderer. Kept in step with the document by sync(),
/// which is called on every hover and every frame, so it must stay cheap:
///  - When the document's object list is the same as at the last sync (same
///    object pointers in the same order), sync() returns after one pass of
///    pointer comparisons and does nothing else.
///  - Otherwise it drops meshes of removed objects and rebuilds only the
///    objects whose (immutable) document entry was replaced, using an ID set,
///    so the cost is O(n log n) rather than O(n^2) in the object count.
class MeshCache {
public:
    struct Entry {
        ObjectPtr source; ///< the document entry the mesh was built from
        MeshData mesh;
        ElementTopology topology; ///< vertices, edges and faces for element picking and drawing
        MeshHandle handle = kNoMesh;
    };

    explicit MeshCache(Renderer& renderer) : renderer_(renderer) {}
    ~MeshCache() { clear(); }
    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;

    /// Brings the cache in line with `document`. Returns true when anything
    /// was checked in detail (the object list had changed).
    bool sync(const Document& document);

    /// Destroys every mesh.
    void clear();

    /// View > Hidden Geometry: soft and hidden edges and hidden faces are built
    /// into the meshes and topologies (drawn faint and picked). Changing it
    /// drops every mesh, so the next sync rebuilds them all.
    void setShowHidden(bool show);
    [[nodiscard]] bool showHidden() const { return showHidden_; }

    /// Nearest hit of `ray` against every cached mesh.
    [[nodiscard]] std::optional<PickHit> pick(const Ray& ray) const;

    /// Vertex, edge or face under the pointer across every cached object (see pickElement).
    [[nodiscard]] std::optional<ElementHit> pickElement(const Ray& ray, const ElementPickOptions& options) const;

    /// The cached entry for an object, if any.
    [[nodiscard]] const Entry* find(ObjectId id) const;

    [[nodiscard]] const std::map<ObjectId, Entry>& entries() const { return entries_; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    Renderer& renderer_;
    std::map<ObjectId, Entry> entries_;
    std::vector<ObjectPtr> synced_; ///< document.objects as of the last sync
    bool showHidden_ = false;
};

} // namespace pf
