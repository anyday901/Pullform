#pragma once

#include "core/commands/editor.hpp"
#include "core/geometry/element_pick.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/region_select.hpp"
#include "core/geometry/snap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pf {

/// What the viewport reports for each pointer event. The tool layer never sees
/// windowing or GPU types, so tests can drive it with synthetic events.
struct PointerInput {
    Ray ray;                     ///< world-space pick ray through the cursor
    std::optional<PickHit> pick; ///< logical face under the cursor, if any
    Vec2 screen{0.0};            ///< cursor position in window pixels (tells clicks from drags)
    /// Size of one screen pixel at unit distance along the ray (radians per
    /// pixel), so snap tolerances can be given in pixels. The viewport fills
    /// it from the camera; 0 turns endpoint, midpoint and inference snapping
    /// off for this event (grid snap still applies).
    double pixelAngle = 0.0;
    bool shift = false; ///< Shift held (Select: add or remove the clicked object)
    bool ctrl = false;  ///< Ctrl (Cmd on macOS) held (Select: add the clicked object)
    /// Projects a world point to the same screen coordinates as `screen`, for
    /// window and crossing selection. Empty in most tests (no projection).
    ScreenProjector project;
    /// Vertex, edge or face under the cursor (pixel tolerances, occlusion), if
    /// the viewport resolved it; tools fall back to `pick` when it is empty.
    std::optional<ElementHit> element;
    /// Position of this press in a rapid series at one spot: 1 single, 2
    /// double, 3 triple click. Only meaningful on pointerDown.
    int clicks = 1;
};

struct SnapSettings {
    bool enabled = true;  ///< grid snap
    double step = 0.1;    ///< grid increment in meters (100 mm)
    bool points = true;   ///< endpoint, midpoint and inference snapping
    bool guides = true;   ///< construction guides shown and snapped to (View > Guides)
    double radiusPx = 12; ///< how close (screen pixels) the pointer must be to a point
    static constexpr std::size_t kMaxReferences = 4; ///< remembered inference reference points

    /// The point with x and y rounded to the grid increment (when grid snap is on).
    [[nodiscard]] Vec3 toGrid(const Vec3& p) const;
    [[nodiscard]] double toGrid(double v) const;
    [[nodiscard]] bool gridOn() const { return enabled && step > 0.0; }
};

/// Endpoint, midpoint and inference snapping against an editor's document.
/// Keeps the snap-candidate index in step with the document and remembers the
/// points recently snapped to (inference references); references from a
/// replaced project (New / Open) are dropped.
class PointSnapper {
public:
    PointSnapper(const Editor& editor, const SnapSettings& settings) : editor_(editor), settings_(settings) {}

    /// Whether point snapping applies to this event.
    [[nodiscard]] bool active(const PointerInput& input) const { return settings_.points && input.pixelAngle > 0.0; }

    /// Snap radius in meters at `at`, from the pixel radius.
    [[nodiscard]] double tolerance(const PointerInput& input, const Vec3& at) const;

    /// Nearest endpoint, midpoint, guide point or guide crossing (not of
    /// `exclude`), remembered as a reference; else the nearest point on a
    /// guide line (not remembered).
    std::optional<SnapResult> point(const PointerInput& input, std::optional<ObjectId> exclude = std::nullopt);

    /// Ground snap (z = 0): a point snap projected down, else X/Y inference
    /// with a remembered reference (skipping `skip`, the rectangle's own first
    /// corner), else the grid.
    std::optional<SnapResult> ground(const PointerInput& input, std::optional<Vec3> skip = std::nullopt);

    /// Height inference along a vertical axis: another object's corner whose
    /// height is within the snap radius of `axisPoint`.
    std::optional<SnapResult> height(const PointerInput& input, const Vec3& axisPoint, ObjectId exclude);

    [[nodiscard]] const std::vector<Vec3>& references() const { return references_; }
    void clearReferences() { references_.clear(); }

private:
    void dropStaleReferences();
    void remember(const Vec3& p);
    void infer(const Vec3& raw, double tolerance, std::optional<Vec3> skip, SnapResult& res);

    const Editor& editor_;
    const SnapSettings& settings_;
    SnapIndex index_;
    std::vector<Vec3> references_;
    std::uint64_t referencesGeneration_ = 0; ///< Editor::projectGeneration the references belong to
};

} // namespace pf
