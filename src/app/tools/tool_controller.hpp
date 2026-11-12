#pragma once

#include "app/tools/snapper.hpp"
#include "app/tools/tool.hpp"
#include "app/tools/tool_support.hpp"
#include "core/commands/editor.hpp"
#include "core/commands/solid_tools.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/snap.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

class MoveTool;
class PushPullTool;
class RectangleTool;
class RotateTool;
class ScaleTool;
class MeasureTool;

/// Owns the modeling tools (one class per tool, see tool.hpp) and routes
/// pointer events, keys and typed values to the current one.
///
/// Pointer movement only updates the editor's transient preview; a completed
/// gesture commits exactly one operation through Editor::commit. Click-click
/// and press-drag-release both work. cancel() (Escape, or a tool change)
/// discards the preview without touching the document or history. Numeric
/// entry calls the same operations as the mouse.
class ToolController {
public:
    explicit ToolController(Editor& editor);
    ~ToolController();
    ToolController(const ToolController&) = delete;
    ToolController& operator=(const ToolController&) = delete;

    [[nodiscard]] Tool tool() const { return tool_; }
    void setTool(Tool tool);

    [[nodiscard]] SnapSettings& snap() { return snap_; }
    [[nodiscard]] const SnapSettings& snap() const { return snap_; }

    /// True while a gesture is in progress (first corner placed, push/pull, move,
    /// rotate, scale or a measurement started).
    [[nodiscard]] bool active() const;

    /// Escape: cancels the active gesture. Returns whether anything was cancelled.
    bool cancel();

    void pointerDown(const PointerInput& input);
    void pointerMove(const PointerInput& input);
    void pointerUp(const PointerInput& input);

    /// A key for the current tool (axis locks and the Rotate axis). Returns
    /// whether the tool used it.
    bool key(ToolKey key);

    /// Typed value for the current tool. Returns an error message, or nothing on success.
    std::optional<std::string> submit(std::string_view text);
    /// Label of the measurements box for the current tool.
    [[nodiscard]] std::string_view measurementLabel() const;

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
    [[nodiscard]] const std::optional<MoveAxis>& axisLock() const;

    /// Move copy mode (tap Ctrl, or Option on macOS). While on, committing a
    /// move places a duplicate at the destination and leaves the original where
    /// it is: one undo entry, and the copy becomes the selection. Like the axis
    /// lock, it can be set before the first click and lasts until the move is
    /// committed or the tool changes.
    void toggleMoveCopy();
    [[nodiscard]] bool moveCopy() const;

    /// Exact move. A single length moves along the locked axis or, while a move
    /// is in progress without a lock, along the current pointer direction.
    /// "x, y, z" moves by that vector. Applies to the active gesture, or else to
    /// the selected object.
    std::optional<std::string> submitMoveDistance(std::string_view distance);

    /// Sets the total extrusion height of an object (properties panel).
    std::optional<std::string> setTotalHeight(ObjectId id, std::string_view height);

    /// Rotation axis for the Rotate tool. Changing it during a gesture keeps the
    /// centre and asks for a new reference direction.
    void setRotateAxis(MoveAxis axis);
    [[nodiscard]] MoveAxis rotateAxis() const;

    /// Exact rotation in degrees. Turns the active gesture's object about its
    /// centre, or else the selected object about the centre of its base. Positive
    /// angles turn counter-clockwise looking down the axis; once a reference
    /// direction is set, the typed angle follows the pointer's direction instead.
    std::optional<std::string> submitRotateAngle(std::string_view angle);

    /// Exact scale: one uniform factor or "width, depth, height" factors, about
    /// the centre of the object's base. Applies to the active gesture's object or
    /// the selected object.
    std::optional<std::string> submitScaleFactors(std::string_view factors);
    /// Duplicates the selected object (Edit > Duplicate). The copy is placed
    /// beside the original along +X, one object width over, and becomes the
    /// selection, so repeating it builds a row. One undo entry.
    std::optional<std::string> duplicateSelection();

    /// Push/pull new starting face mode (tap Ctrl, or Option on macOS, with
    /// Push/Pull): no wall stretches and the old outline stays as edges. It
    /// lasts until it is tapped again or the tool changes.
    void togglePushNewFace();
    [[nodiscard]] bool pushNewFace() const;

    /// Tape measure guide creation (tap Ctrl, or Option on macOS, with the
    /// tape measure). On by default; it persists across tool changes.
    void toggleTapeGuides();
    [[nodiscard]] bool tapeGuides() const;

    /// Deletes every construction guide (Edit > Delete Guides): one undo entry.
    std::optional<std::string> deleteGuides();
    /// Runs a solid tool (Union, Subtract, ...) on the selected objects, in
    /// the order they were selected, and selects the result. Returns the
    /// error to show when it cannot (wrong count, not solids, no overlap).
    std::optional<std::string> applySolidTool(SolidTool tool);

    /// Intersect Faces (Edit > Intersect Faces): adds edges where the selected
    /// faces, or every face of the selected objects, cross the faces `mode`
    /// names (see pf::intersectFaces). One undo entry; the objects stay selected.
    std::optional<std::string> intersectSelection(IntersectMode mode);

    /// Typed value for the tape measure or dimension tool (the dimension line offset).
    std::optional<std::string> submitMeasure(std::string_view text);

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
    [[nodiscard]] const std::optional<SnapResult>& snapIndicator() const { return ctx_.indicator; }

    /// Points recently snapped to; inference guides align with these.
    [[nodiscard]] const std::vector<Vec3>& inferenceReferences() const { return snapper_.references(); }

    /// The name of the inference shown ("On Edge", "Perpendicular to Edge"),
    /// for the tooltip beside the pointer; empty when there is none to show.
    [[nodiscard]] std::string snapTooltip() const;

    /// Status bar hint for the current tool and gesture state.
    [[nodiscard]] std::string hint() const;

private:
    /// Registers `T` as the tool for `id`. Returns it for wiring.
    template <class T, class... Args>
    T& add(Tool id, Args&&... args);
    [[nodiscard]] ToolBase& current() const { return *tools_[static_cast<std::size_t>(tool_)]; }

    Editor& editor_;
    Tool tool_ = Tool::Select;
    Tool lastMeasure_ = Tool::TapeMeasure; ///< target of submitMeasure()
    SnapSettings snap_;
    PointSnapper snapper_; // after snap_, which it refers to
    ToolContext ctx_;      // after snapper_
    std::array<std::unique_ptr<ToolBase>, kToolCount> tools_;
    // Typed views of registered tools, for their tool-specific entry points.
    RectangleTool* rectangle_ = nullptr;
    PushPullTool* pushPull_ = nullptr;
    MoveTool* move_ = nullptr;
    RotateTool* rotate_ = nullptr;
    ScaleTool* scale_ = nullptr;
    MeasureTool* tape_ = nullptr;
};

} // namespace pf
