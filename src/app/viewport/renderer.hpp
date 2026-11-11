#pragma once

#include "core/geometry/solid.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace pf {

/// A colored line segment in world space (double precision).
struct LineSegment {
    LineSegment() = default;
    LineSegment(const Vec3& start, const Vec3& end, const glm::vec4& startColor, const glm::vec4& endColor = glm::vec4{-1.0f})
        : a(start), b(end), color(startColor), colorB(endColor) {}
    Vec3 a{0.0};
    Vec3 b{0.0};
    glm::vec4 color{1.0f};          ///< color at `a`
    glm::vec4 colorB{-1.0f};        ///< color at `b`; negative alpha means "same as color"
};

using MeshHandle = std::uint32_t;
inline constexpr MeshHandle kNoMesh = 0;

struct FrameSetup {
    glm::dmat4 view{1.0};
    glm::dmat4 projection{1.0};
    Vec3 eye{0.0};
    int x = 0, y = 0, width = 1, height = 1; ///< viewport rectangle in framebuffer pixels (origin bottom-left)
    glm::vec4 clearColor{0.95f, 0.96f, 0.97f, 1.0f};
};

/// The small rendering interface the viewport draws through. The OpenGL 3.3
/// implementation lives in gl_renderer.cpp; another backend (e.g. Vulkan) can
/// implement the same calls. Model data stays double precision until here:
/// implementations convert to float relative to a local origin, so precision
/// does not degrade far from the world origin.
class Renderer {
public:
    virtual ~Renderer() = default;
    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    virtual void beginFrame(const FrameSetup& frame) = 0;

    /// Uploads a mesh (faces with flat normals). Returns a handle owned by the renderer.
    virtual MeshHandle createMesh(const MeshData& mesh) = 0;
    virtual void destroyMesh(MeshHandle handle) = 0;

    /// Draws a whole mesh, lit, with back-face culling. Filled geometry is
    /// pushed slightly back in depth so edges drawn afterwards stay visible.
    virtual void drawMesh(MeshHandle handle, const glm::vec4& color) = 0;
    /// Draws an index range of a mesh (one logical face) as a translucent overlay.
    virtual void drawMeshOverlay(MeshHandle handle, std::uint32_t firstIndex, std::uint32_t indexCount,
                                 const glm::vec4& color) = 0;
    /// Draws line segments. With `depthTest` false they draw on top (previews).
    virtual void drawLines(const std::vector<LineSegment>& lines, bool depthTest) = 0;
    /// Draws line segments `widthPixels` wide on screen (profile edges,
    /// highlighted edges). Core-profile OpenGL has no reliable wide lines, so
    /// implementations draw each segment as a screen-aligned quad.
    virtual void drawWideLines(const std::vector<LineSegment>& lines, float widthPixels, bool depthTest) = 0;

    virtual void endFrame() = 0;
};

/// Creates the OpenGL 3.3 core renderer. Requires a current GL context with
/// functions already loaded.
std::unique_ptr<Renderer> createGlRenderer();

} // namespace pf
