#pragma once

#include "core/document/project_file.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/mat4x4.hpp>

#include <optional>

namespace pf {

/// Orbit camera around a target point, Z-up. Pure math (no GPU), so picking
/// rays and navigation are unit-testable. Screen coordinates are window pixels
/// with the origin at the top-left of the viewport.
class Camera {
public:
    Camera();

    [[nodiscard]] Vec3 target() const { return target_; }
    [[nodiscard]] double distance() const { return distance_; }
    [[nodiscard]] Vec3 position() const;
    [[nodiscard]] Vec3 forward() const; ///< unit vector from the eye toward the target
    [[nodiscard]] glm::dmat4 view() const;
    [[nodiscard]] glm::dmat4 projection(double aspect) const;
    [[nodiscard]] double nearPlane() const;
    [[nodiscard]] double farPlane() const;
    [[nodiscard]] double fovY() const { return fovY_; }

    /// World-space ray through a pixel of a viewport of the given size.
    [[nodiscard]] Ray rayThrough(const Vec2& pixel, const Vec2& viewportSize) const;

    /// Pixel (viewport-local, origin top-left) where a world point appears, or
    /// nothing when it is behind the eye. Inverse of rayThrough.
    [[nodiscard]] std::optional<Vec2> project(const Vec3& world, const Vec2& viewportSize) const;
    /// World length of one pixel at the depth of `world` (for constant-size marks).
    [[nodiscard]] double worldPerPixel(const Vec3& world, const Vec2& viewportSize) const;

    /// Orbit by a pointer delta in pixels.
    void orbit(const Vec2& deltaPixels);
    /// Pan so the point under the cursor follows it.
    void pan(const Vec2& deltaPixels, const Vec2& viewportSize);
    /// Zoom by wheel steps (positive = in), toward the point under `pixel`.
    void zoomAt(double steps, const Vec2& pixel, const Vec2& viewportSize);
    /// Frames the bounds (or the default view when empty).
    void fit(const Bounds& bounds, double aspect);
    void reset();

    [[nodiscard]] CameraView toView() const;
    void setFromView(const CameraView& view);

private:
    Vec3 target_{0.0};
    double distance_ = 8.0;
    double yaw_ = 0.0;   ///< radians around +Z, measured from +X toward +Y
    double pitch_ = 0.0; ///< radians above the XY plane
    double fovY_ = 0.0;
};

} // namespace pf
