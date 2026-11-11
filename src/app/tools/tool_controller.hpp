#pragma once

#include "app/tools/measure_tool.hpp"
#include "core/commands/editor.hpp"
#include "core/geometry/pick.hpp"
#include "app/tools/snapper.hpp"
#include "core/geometry/snap.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

enum class Tool { Select, Rectangle, PushPull, Move, Orbit, TapeMeasure, Dimension };

std::string_view toolName(Tool tool);

/// Interaction state machines for the modeling tools.
///
/// Pointer movement only updates the editor's transient preview; a completed
/// gesture commits exactly one operation through Editor::commit. Click-click
/// and press-drag-release both work. cancel() (Escape, or a tool change)
/// discards the preview without touching the document or history. Numeric
/// entry calls the same operations as the mouse.
class ToolController {
public:
    static constexpr double kDragThresholdPx = 5.0;

    explicit ToolController(Editor& editor) : editor_(editor), snapper_(editor, snap_), measure_(editor) {}

    [[nodiscard]] Tool tool() const { return tool_; }
    void setTool(Tool tool);

    [[nodiscard]] SnapSettings& snap() { return snap_; }
    [[nodiscard]] const SnapSettings& snap() const { return snap_; }

    /// True while a gesture is in progress (first corner placed, push/pull, move or a measurement started).
    [[nodiscard]] bool active() const {
        return rect_.has_value() || push_.has_value() || move_.has_value() || measure_.active();
    }

    /// Escape: cancels the active gesture. Returns whether anything was cancelled.
    bool cancel();

    void pointerDown(const PointerInput& input);
    void pointerMove(const PointerInput& input);
    void pointerUp(const PointerInput& input);

    /// Exact rectangle size. `depth` empty means `width` holds "w, d". With a
    /// first corner placed, the size is drawn from it toward the pointer;
    /// otherwise from the origin. Returns an error message, or nothing on success.
    std::optional<std::string> submitRectangleSize(std::string_view width, std::string_view depth = {});

    /// Exact push/pull distance: incremental from the face's current position
    /// (for a planar face it equals the total height). Applies to the active
    /// gesture, or else to the selected object.
    std::optional<std::string> submitPushDistance(std::string_view distance);

    /// Move axis lock (X, Y or Z key, or the arrow keys). Locking the axis that
    /// is already locked releases it. The lock lasts until the move is
    /// committed or the tool changes, so it can be set before the first click.
    void toggleAxisLock(MoveAxis axis);
    [[nodiscard]] const std::optional<MoveAxis>& axisLock() const { return axisLock_; }

    /// Move copy mode (tap Ctrl, or Option on macOS). While on, committing a
    /// move places a duplicate at the destination and leaves the original where
    /// it is: one undo entry, and the copy becomes the selection. Like the axis
    /// lock, it can be set before the first click and lasts until the move is
    /// committed or the tool changes.
    void toggleMoveCopy();
    [[nodiscard]] bool moveCopy() const { return moveCopy_; }

    /// Exact move. A single length moves along the locked axis or, while a move
    /// is in progress without a lock, along the current pointer direction.
    /// "x, y, z" moves by that vector. Applies to the active gesture, or else to
    /// the selected object.
    std::optional<std::string> submitMoveDistance(std::string_view distance);

    /// Sets the total extrusion height of an object (properties panel).
    std::optional<std::string> setTotalHeight(ObjectId id, std::string_view height);

    /// Duplicates the selected object (Edit > Duplicate). The copy is placed
    /// beside the original along +X, one object width over, and becomes the
    /// selection, so repeating it builds a row. One undo entry.
    std::optional<std::string> duplicateSelection();

    /// Typed value for the tape measure or dimension tool (the dimension line offset).
    std::optional<std::string> submitMeasure(std::string_view text) { return measure_.submit(text); }

    [[nodiscard]] MeasureTool& measureTool() { return measure_; }

    /// Explanation when a face cannot be push/pulled in this milestone.
    static std::optional<std::string> pushableReason(const FaceId& face);

    /// Snaps a pointer to a 3D point for any tool, in priority order: an
    /// endpoint or midpoint of an object near the pointer, a point on the face
    /// under the pointer, then the ground (with inference and grid snap as for
    /// snapGround). Points snapped to are remembered as inference references.
    SnapResult snapPointer(const PointerInput& input);

    /// Snaps a pointer to the ground plane (z = 0): an endpoint or midpoint
    /// projected straight down (with a vertical guide), else alignment with a
    /// reference point along X or Y (inference guides), else the grid.
    /// Nothing when the ray does not reach the ground.
    std::optional<SnapResult> snapGround(const PointerInput& input);

    /// The snap shown in the viewport for the last pointer event (marker and
    /// inference guides), if any.
    [[nodiscard]] const std::optional<SnapResult>& snapIndicator() const { return indicator_; }

    /// Points recently snapped to; inference guides align with these.
    [[nodiscard]] const std::vector<Vec3>& inferenceReferences() const { return snapper_.references(); }

    /// Status bar hint for the current tool and gesture state.
    [[nodiscard]] std::string hint() const;

private:
    struct RectGesture {
        Vec3 start{0.0};
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };
    struct PushGesture {
        ObjectId objectId;
        FaceKind face = FaceKind::Profile;
        Vec3 anchor{0.0};
        double startHeight = 0.0;
        double baseZ = 0.0; ///< world height of the profile plane
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };

    struct MoveGesture {
        ObjectId objectId;
        Vec3 anchor{0.0};           ///< picked point the guide line starts from
        Vec3 startTranslation{0.0}; ///< object translation when the gesture began
        Vec2 pressScreen{0.0};
        bool pressed = false;
        std::optional<Ray> lastRay; ///< re-evaluates the preview when the axis lock changes
    };

    [[nodiscard]] std::string toolHint() const;
    [[nodiscard]] std::optional<Vec3> groundPoint(const PointerInput& input);
    std::optional<double> snapPushHeight(const PointerInput& input, std::optional<double> raw);

    /// Snaps for the tape measure and dimension tools and shows the marker.
    /// Empty when the pointer reaches neither the model nor the ground (an
    /// unsnapped ground point, with grid snap off, is still a point).
    std::optional<SnapResult> measureSnap(const PointerInput& input);

    void selectDown(const PointerInput& input);
    void rectDown(const PointerInput& input);
    void rectMove(const PointerInput& input);
    void rectFinish(const PointerInput& input);
    bool commitRectangle(const RectanglePlacement& placement);
    void pushDown(const PointerInput& input);
    void pushMove(const PointerInput& input);
    void pushFinish();
    void setPushPreview(double delta);
    bool commitHeight(ObjectId id, FaceKind face, double height);
    void moveDown(const PointerInput& input);
    void moveMove(const PointerInput& input);
    void moveFinish();
    void updateMovePreview(const Ray& ray);
    [[nodiscard]] std::optional<Vec3> moveOffset(const Ray& ray) const;
    /// Every move (mouse or typed) commits here: one undo entry.
    bool commitMove(ObjectId id, const Vec3& offset);
    [[nodiscard]] std::string lastError(const char* fallback) const;

    Editor& editor_;
    Tool tool_ = Tool::Select;
    SnapSettings snap_;
    std::optional<RectGesture> rect_;
    std::optional<PushGesture> push_;
    std::optional<MoveGesture> move_;
    std::optional<MoveAxis> axisLock_;
    bool moveCopy_ = false;
    PointSnapper snapper_; // after snap_, which it refers to
    std::optional<SnapResult> indicator_;
    MeasureTool measure_;
};

} // namespace pf
