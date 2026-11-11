#pragma once

#include "app/tools/tool_controller.hpp"
#include "app/viewport/camera.hpp"
#include "app/viewport/labels.hpp"
#include "app/viewport/mesh_cache.hpp"
#include "app/viewport/renderer.hpp"
#include "core/commands/editor.hpp"

#include <optional>
#include <vector>

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
    /// Top-left corner of the viewport in window coordinates.
    [[nodiscard]] Vec2 origin() const { return origin_; }
    [[nodiscard]] double aspect() const { return size_.y > 0 ? size_.x / size_.y : 1.0; }
    /// Converts a window position to viewport-local pixels.
    [[nodiscard]] Vec2 toLocal(const Vec2& windowPos) const { return windowPos - origin_; }

    /// Builds the tool input for a window position: the camera ray and a CPU
    /// ray cast against the document resolved to object and logical face.
    [[nodiscard]] PointerInput pointerAt(const Vec2& windowPos, const Document& document);

    void render(const Editor& editor, const ToolController& tools);

    /// Text labels for dimensions and the live measurement, in window coordinates.
    [[nodiscard]] std::vector<ViewportLabel> labels(const Editor& editor) const;

    /// Grid spacing currently drawn (meters), for the status bar.
    [[nodiscard]] double gridStep() const { return gridStep_; }

private:
    using CachedMesh = MeshCache::Entry;

    void drawGrid(std::vector<LineSegment>& lines);
    static void appendEdges(std::vector<LineSegment>& lines, const MeshData& mesh, const glm::vec4& color);
    /// Radians per pixel: world size of one pixel at unit distance from the eye.
    [[nodiscard]] double pixelAngle() const;
    void drawSnap(std::vector<LineSegment>& lines, const SnapResult& snap) const;
    void appendDimension(std::vector<LineSegment>& lines, const Vec3& a, const Vec3& b, const Vec3& offset,
                         const glm::vec4& color) const;
    [[nodiscard]] std::optional<Vec2> toWindow(const Vec3& world) const;
    void drawFaceOverlay(const CachedMesh& cached, const FaceId& face, const glm::vec4& color);
    /// Draws a rotate/scale preview object (or the original in red when the
    /// current value is invalid).
    void drawTransformResult(const std::optional<ProfileObject>& result, bool error, const ProfileObject* original,
                             std::vector<LineSegment>& lines);

    /// Rotate/scale preview of a multi-selection (`ids` non-empty): every
    /// result, or every original in red when the value is invalid. Falls back
    /// to drawTransformResult for one object.
    void drawTransformGroup(const std::vector<ProfileObject>& group, const std::vector<ObjectId>& ids,
                            const std::optional<ProfileObject>& result, bool error, ObjectId primary, const Document& doc,
                            std::vector<LineSegment>& lines);
    /// Creates a mesh for this frame's preview; released at the next frame.
    MeshHandle previewMesh(const MeshData& mesh);
    void releasePreviewMeshes();

    Renderer& renderer_;
    Camera camera_;
    MeshCache meshes_;
    std::vector<MeshHandle> previewHandles_;
    Vec2 origin_{0.0};
    Vec2 size_{1.0};
    Vec2 framebufferScale_{1.0};
    int framebufferHeight_ = 1;
    double gridStep_ = 0.1;
};

} // namespace pf
