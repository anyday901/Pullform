#pragma once

#include "app/tools/tool_controller.hpp"
#include "app/viewport/camera.hpp"
#include "app/viewport/mesh_cache.hpp"
#include "app/viewport/renderer.hpp"
#include "core/commands/editor.hpp"

#include <optional>

namespace pf {

/// The 3D modeling view: camera, derived GPU meshes, picking and drawing of
/// the grid, axes, objects, highlights and previews. Meshes are rebuilt only
/// for objects whose immutable document entry changed (see MeshCache).
class Viewport {
public:
    explicit Viewport(Renderer& renderer);
    ~Viewport();
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    Camera& camera() { return camera_; }
    [[nodiscard]] const Camera& camera() const { return camera_; }

    /// Viewport rectangle in window coordinates, plus the framebuffer scale
    /// (framebuffer pixels per window unit; 2 on Retina displays).
    void setRect(const Vec2& origin, const Vec2& size, const Vec2& framebufferScale, int framebufferHeight);
    [[nodiscard]] bool contains(const Vec2& windowPos) const;
    [[nodiscard]] Vec2 size() const { return size_; }
    [[nodiscard]] double aspect() const { return size_.y > 0 ? size_.x / size_.y : 1.0; }
    /// Converts a window position to viewport-local pixels.
    [[nodiscard]] Vec2 toLocal(const Vec2& windowPos) const { return windowPos - origin_; }

    /// Builds the tool input for a window position: the camera ray and a CPU
    /// ray cast against the document resolved to object and logical face.
    [[nodiscard]] PointerInput pointerAt(const Vec2& windowPos, const Document& document);

    void render(const Editor& editor, const ToolController& tools);

    /// Grid spacing currently drawn (meters), for the status bar.
    [[nodiscard]] double gridStep() const { return gridStep_; }

private:
    using CachedMesh = MeshCache::Entry;

    void drawGrid(std::vector<LineSegment>& lines);
    static void appendEdges(std::vector<LineSegment>& lines, const MeshData& mesh, const glm::vec4& color);
    void drawFaceOverlay(const CachedMesh& cached, const FaceId& face, const glm::vec4& color);

    Renderer& renderer_;
    Camera camera_;
    MeshCache meshes_;
    MeshHandle previewHandle_ = kNoMesh;
    Vec2 origin_{0.0};
    Vec2 size_{1.0};
    Vec2 framebufferScale_{1.0};
    int framebufferHeight_ = 1;
    double gridStep_ = 0.1;
};

} // namespace pf
