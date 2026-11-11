#pragma once

#include "app/viewport/renderer.hpp"
#include "core/document/document.hpp"
#include "core/geometry/rectangle.hpp"

#include <cstddef>
#include <memory>
#include <set>

namespace pf::test {

/// A Renderer that draws nothing and counts mesh uploads, so the viewport's
/// mesh cache can be tested and timed without a GPU.
class CountingRenderer final : public Renderer {
public:
    void beginFrame(const FrameSetup&) override {}
    MeshHandle createMesh(const MeshData&) override {
        ++created;
        const MeshHandle h = next++;
        live.insert(h);
        return h;
    }
    void destroyMesh(MeshHandle handle) override {
        ++destroyed;
        live.erase(handle);
    }
    void drawMesh(MeshHandle, const glm::vec4&) override {}
    void drawMeshOverlay(MeshHandle, std::uint32_t, std::uint32_t, const glm::vec4&) override {}
    void drawLines(const std::vector<LineSegment>&, bool) override {}
    void drawWideLines(const std::vector<LineSegment>&, float, bool) override {}
    void endFrame() override {}

    std::size_t created = 0;
    std::size_t destroyed = 0;
    std::set<MeshHandle> live;

private:
    MeshHandle next = 1;
};

/// A w x d x h box with its corner at (x, y, 0), as a document entry.
inline ObjectPtr boxObject(std::uint64_t id, double x, double y, double w = 0.5, double d = 0.5, double h = 0.5) {
    auto obj = std::make_shared<ProfileObject>();
    obj->id = ObjectId{id};
    obj->name = "Box";
    obj->profile.boundary = rectangleBoundary(w, d);
    obj->extrusion = Extrusion{h};
    obj->transform.translation = {x, y, 0.0};
    return obj;
}

/// A document with `count` boxes on a square grid, 1 m apart.
inline Document boxGrid(std::size_t count) {
    Document doc;
    std::size_t side = 1;
    while (side * side < count) ++side;
    for (std::size_t i = 0; i < count; ++i) {
        doc.objects.push_back(boxObject(doc.nextId++, static_cast<double>(i % side), static_cast<double>(i / side)));
    }
    return doc;
}

} // namespace pf::test
