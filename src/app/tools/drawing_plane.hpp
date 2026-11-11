#pragma once

#include "app/tools/tool.hpp"
#include "core/geometry/plane.hpp"

#include <optional>
#include <string>

namespace pf {

/// Snaps the pointer to a point on `plane`: an endpoint or midpoint is
/// projected onto it (with a guide from the feature when it is off the
/// plane), else the pointer ray meets the plane and the point lines up with a
/// remembered point along the plane's axes (inference), else it snaps to the
/// grid in plane coordinates. On the ground this is exactly PointSnapper::ground.
/// `skip` is a point not to infer from (the shape's own first point). Nothing
/// when the ray misses the plane.
std::optional<SnapResult> snapOnPlane(ToolContext& ctx, const PointerInput& input, const PlaneBasis& plane,
                                      const std::optional<Vec3>& skip = std::nullopt);

/// The plane of the face under the pointer, facing the viewer, or nothing.
std::optional<PlaneBasis> pickedPlane(const Document& doc, const PointerInput& input);

/// Which plane a drawing tool draws on, shared by Rectangle, Line and the
/// curve tools.
///
/// Before drawing starts, the plane follows the pointer: the face under it
/// (tilted or vertical faces too), else the ground. The arrow keys lock it:
/// Right a plane facing the red axis (X), Left the green axis (Y), Up a
/// horizontal plane, Down the plane last hovered; the same key again releases
/// the lock. An axis plane goes through the snapped point under the pointer.
///
/// begin() fixes the plane for the gesture (the first click), so later points
/// stay on it even when the pointer crosses other faces; an arrow key during
/// the gesture turns the plane about the first point. end() drops the gesture
/// and the lock.
class DrawingPlane {
public:
    /// A point under the pointer and the plane it lies on.
    struct Hit {
        Vec3 point{0.0};
        PlaneBasis plane;
        std::optional<ObjectId> object; ///< the object whose face gives the plane (not a lock or the ground)
    };

    /// Arrow keys (see above). Returns whether the key was used.
    bool key(ToolKey key);

    /// Before drawing: the snapped point and plane under the pointer; shows
    /// the snap marker. Nothing when the pointer reaches no plane.
    std::optional<Hit> hover(ToolContext& ctx, const PointerInput& input);

    /// Starts a gesture on `plane` at `anchor` (on the plane). `object` is the
    /// object whose face the plane came from, if any (Hit::object).
    void begin(const PlaneBasis& plane, const Vec3& anchor, std::optional<ObjectId> object = std::nullopt);
    /// While drawing: the snapped pointer point on the gesture's plane; shows
    /// the snap marker. `skip` is a point not to infer from.
    std::optional<Vec3> point(ToolContext& ctx, const PointerInput& input, const std::optional<Vec3>& skip = std::nullopt);
    /// Ends the gesture and releases any lock.
    void end();

    [[nodiscard]] bool drawing() const { return plane_.has_value(); }
    /// The gesture's plane (empty before begin()).
    [[nodiscard]] const std::optional<PlaneBasis>& plane() const { return plane_; }
    /// The object whose face the gesture draws on, while the plane is still
    /// that face's (an arrow key that turns the plane drops it): a shape drawn
    /// there goes into that object's edges and faces.
    [[nodiscard]] std::optional<ObjectId> target() const { return plane_ && plane_ == original_ ? target_ : std::nullopt; }
    /// The axis whose plane is locked by an arrow key, if any.
    [[nodiscard]] const std::optional<MoveAxis>& axisLock() const { return axis_; }
    /// Whether Down locked the hovered plane.
    [[nodiscard]] bool faceLocked() const { return faceLock_.has_value(); }
    /// " Locked to the red plane." and the like, or empty: appended to hints.
    [[nodiscard]] std::string lockHint() const;

private:
    /// The plane facing `axis` through `point`, facing the viewer.
    static PlaneBasis axisPlane(MoveAxis axis, const Vec3& point, const Vec3& viewDirection);

    std::optional<MoveAxis> axis_;
    std::optional<PlaneBasis> faceLock_;
    std::optional<PlaneBasis> lastHovered_;
    std::optional<PlaneBasis> plane_;    ///< the gesture's plane
    std::optional<PlaneBasis> original_; ///< the plane begin() was given, for releasing a lock mid-gesture
    Vec3 anchor_{0.0};
    std::optional<ObjectId> target_;
    Vec3 lastView_{0.0, 0.0, -1.0};
};

} // namespace pf
