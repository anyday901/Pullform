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
    [[nodiscard]] double aspect() const { return size_.y > 0 ? size_.x / size_.y : 1.0; }
    /// Converts a window position to viewport-local pixels.
    [[nodiscard]] Vec2 toLocal(const Vec2& windowPos) const { return windowPos - origin_; }

    /// Builds the tool input for a window position: the camera ray and a CPU
    /// ray cast against the document resolved to object and logical face.
    [[nodiscard]] PointerInput pointerAt(const Vec2& windowPos, const Document& document);

    void render(const Editor& editor, const ToolController& tools);

    /// Text labels for dimensions and the live measurement, in window coordinates.
    [[nodiscard]] std::vector<ViewportLabel> labels(const Editor& editor) const;

    /// Window position of a world point, or nothing when it is behind the eye.
    [[nodiscard]] std::optional<Vec2> toWindow(const Vec3& world) const;

    /// Grid spacing currently drawn (meters), for the status bar.
    [[nodiscard]] double gridStep() const { return gridStep_; }

    /// "Profiles" edge style: outline (silhouette and border) edges drawn
    /// thicker than the other edges, as in hand drawing. Off by default.
    [[nodiscard]] bool& profileEdges() { return profileEdges_; }
    [[nodiscard]] bool profileEdges() const { return profileEdges_; }

    /// View > Hidden Geometry: soft and hidden edges drawn dashed, hidden faces
    /// drawn, and both picked, so they can be selected and unhidden. Off by default.
    [[nodiscard]] bool& hiddenGeometry() { return hiddenGeometry_; }
    [[nodiscard]] bool hiddenGeometry() const { return hiddenGeometry_; }

    static constexpr float kProfileWidthPx = 2.5f;   ///< profile edge width
    static constexpr float kHighlightWidthPx = 3.0f; ///< hovered and selected edge width

private:
    using CachedMesh = MeshCache::Entry;

    void drawGrid(std::vector<LineSegment>& lines);
    static void appendEdges(std::vector<LineSegment>& lines, const MeshData& mesh, const glm::vec4& color);
    /// Radians per pixel: world size of one pixel at unit distance from the eye.
    [[nodiscard]] double pixelAngle() const;
    void drawSnap(std::vector<LineSegment>& lines, const SnapResult& snap) const;
    void appendDimension(std::vector<LineSegment>& lines, const Vec3& a, const Vec3& b, const Vec3& offset,
                         const glm::vec4& color) const;
    /// A guide line (dashed, out to the far distance) or a guide point (a small cross).
    void appendGuide(std::vector<LineSegment>& lines, const Guide& guide, const glm::vec4& color) const;
    /// A dashed segment, dashes about 4 pixels long where it is nearest the eye.
    void appendDashed(std::vector<LineSegment>& lines, const Vec3& a, const Vec3& b, const glm::vec4& color) const;
    /// A protractor circle in the plane with unit `normal`, with 15-degree
    /// ticks, starting along `spoke` (its length is the radius).
    void appendProtractor(std::vector<LineSegment>& lines, const Vec3& center, const Vec3& normal,
                          const std::optional<Vec3>& spoke, const glm::vec4& color) const;
    void drawFaceOverlay(const CachedMesh& cached, const FaceId& face, const glm::vec4& color);
    /// Hovered and selected vertices, edges and faces (Select tool).
    void drawElements(const Editor& editor, const std::vector<ObjectId>& previewed, std::vector<LineSegment>& onTop);
    /// A small screen-facing square around a vertex.
    void appendVertexMarker(std::vector<LineSegment>& lines, const Vec3& p, double pixels, const glm::vec4& color) const;
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
    bool profileEdges_ = false;
    bool hiddenGeometry_ = false;
};

} // namespace pf
