#pragma once

#include "core/commands/editor.hpp"
#include "core/geometry/element_pick.hpp"
#include "core/geometry/inference.hpp"
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
    bool shift = false; ///< Shift held (Select: add or remove the clicked object; drawing tools: lock the inference)
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
    static constexpr std::size_t kMaxReferenceEdges = 2; ///< remembered parallel/perpendicular reference edges

    /// The point with x and y rounded to the grid increment (when grid snap is on).
    [[nodiscard]] Vec3 toGrid(const Vec3& p) const;
    [[nodiscard]] double toGrid(double v) const;
    [[nodiscard]] bool gridOn() const { return enabled && step > 0.0; }
};

/// Options for PointSnapper::infer.
struct InferOptions {
    /// Where the gesture started (a line's first point, the tape measure's
    /// start): adds axis, parallel and perpendicular lines through it.
    std::optional<Vec3> from;
    /// Keep the point on this plane (a tool drawing on a face). Without one
    /// the point may be anywhere in space; a miss falls back to the ground.
    std::optional<InferencePlane> plane;
    /// Skip this object's features (the one being edited).
    std::optional<ObjectId> exclude;
    /// Ground fallback: a point not to infer from (see ground()).
    std::optional<Vec3> skip;
};

/// The shared inference engine for every tool, against an editor's document
/// (see core/geometry/inference.hpp for what it infers). Keeps the
/// snap-candidate index in step with the document; remembers the points and
/// edges recently snapped to (inference references), dropped when the project
/// is replaced (New / Open); and holds the Shift lock: while Shift is held,
/// the line or plane of the inference shown when it was pressed constrains
/// every later point until Shift is released.
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

    /// Full inference for a 3D point, for any tool: a point feature, an edge
    /// intersection, a point on an edge, a line from `options.from`, a
    /// remembered point along an axis; else the face under the pointer (On
    /// Face, lockable to its plane), the drawing plane, or the ground
    /// (ground()). Shift locks the constraint of the last inference shown.
    /// Nothing only when the ray reaches neither the model nor the ground.
    std::optional<SnapResult> infer(const PointerInput& input, const InferOptions& options = {});

    [[nodiscard]] const std::vector<Vec3>& references() const { return references_; }
    [[nodiscard]] const std::vector<SnapEdge>& referenceEdges() const { return referenceEdges_; }
    /// The Shift-locked constraint, if any.
    [[nodiscard]] const std::optional<SnapConstraint>& lock() const { return lock_; }
    /// Forgets the references and any lock (tool change).
    void clearReferences() {
        references_.clear();
        referenceEdges_.clear();
        lock_.reset();
        lastConstraint_.reset();
    }
    /// Drops the Shift lock (end of a gesture).
    void unlock() {
        lock_.reset();
        lastConstraint_.reset();
    }

private:
    void dropStaleReferences();
    void remember(const Vec3& p);
    void rememberEdge(const SnapEdge& e);
    void remember(const SnapResult& hit);
    void updateLock(const PointerInput& input);
    void infer(const Vec3& raw, double tolerance, std::optional<Vec3> skip, SnapResult& res);
    [[nodiscard]] std::optional<SnapResult> onFace(const PickHit& pick) const;

    const Editor& editor_;
    const SnapSettings& settings_;
    SnapIndex index_;
    std::vector<Vec3> references_;
    std::vector<SnapEdge> referenceEdges_;
    std::optional<SnapConstraint> lock_;           ///< Shift lock
    std::optional<SnapConstraint> lastConstraint_; ///< of the last inference shown (what Shift would lock)
    std::uint64_t referencesGeneration_ = 0; ///< Editor::projectGeneration the references belong to
};

} // namespace pf
