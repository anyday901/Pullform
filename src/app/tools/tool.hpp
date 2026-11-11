#pragma once

#include "app/tools/snapper.hpp"
#include "core/commands/editor.hpp"
#include "core/geometry/snap.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace pf {

/// Every tool the controller can switch to. Adding a tool: a value here, a
/// name in toolName(), and one registration line in ToolController's constructor.
enum class Tool {
    Select, Rectangle, RotatedRectangle, Line, Circle, Polygon, PushPull, Move, Rotate, Scale, Orbit, TapeMeasure, Dimension,
    Arc, ThreePointArc, Pie
};
inline constexpr int kToolCount = static_cast<int>(Tool::Pie) + 1;

std::string_view toolName(Tool tool);

/// Pointer travel (screen pixels) between press and release that makes a drag
/// rather than a click.
inline constexpr double kDragThresholdPx = 5.0;

/// Keys a tool may react to. Arrow keys work while a value is being typed;
/// the letters only when no value is being typed (the app decides).
enum class ToolKey { ArrowRight, ArrowLeft, ArrowUp, ArrowDown, X, Y, Z };

/// State and services every tool shares: the editor, the snap settings, the
/// point snapper and the snap indicator shown in the viewport.
class ToolContext {
public:
    ToolContext(Editor& e, SnapSettings& s, PointSnapper& p) : editor(e), snap(s), snapper(p) {}

    Editor& editor;
    SnapSettings& snap;
    PointSnapper& snapper;
    /// The snap shown for the last pointer event (marker and inference guides).
    std::optional<SnapResult> indicator;

    /// Ground snap (see ToolController::snapGround). `skip` is a point not to
    /// infer from (the rectangle's own first corner).
    std::optional<SnapResult> snapGround(const PointerInput& input, const std::optional<Vec3>& skip = std::nullopt);
    /// Point snap for any tool (see ToolController::snapPointer).
    SnapResult snapPointer(const PointerInput& input, const std::optional<Vec3>& skip = std::nullopt);
    /// The full inference engine (PointSnapper::infer): pass `from` while a
    /// gesture has a start point (a line's first point) for axis, parallel and
    /// perpendicular inference from it, and `plane` to draw on a face. A miss
    /// is SnapKind::None at the ray origin. Does not set the indicator.
    SnapResult inferPointer(const PointerInput& input, const InferOptions& options);
    /// Ground snap that also shows the marker; nothing when the ray misses the ground.
    std::optional<Vec3> groundPoint(const PointerInput& input, const std::optional<Vec3>& skip = std::nullopt);
    /// Snaps for the tape measure and dimension tools and shows the marker;
    /// `from` is the first point once placed. Empty when the pointer reaches
    /// neither the model nor the ground (an unsnapped ground point, with grid
    /// snap off, is still a point).
    std::optional<SnapResult> measureSnap(const PointerInput& input, const std::optional<Vec3>& from = std::nullopt);

    /// The editor's last error message, or `fallback`.
    [[nodiscard]] std::string lastError(const char* fallback) const;

    /// Selected object for Rotate/Scale; a click on an object selects it first
    /// (and returns nothing, so the next click starts the gesture).
    const ProfileObject* transformTarget(const PointerInput& input);
};

/// One modeling tool's interaction state machine.
///
/// Pointer movement only updates the editor's transient preview; a completed
/// gesture commits exactly one operation through Editor::commit. cancel()
/// discards the gesture without touching the document or history (the
/// controller clears the preview itself).
class ToolBase {
public:
    explicit ToolBase(ToolContext& ctx) : ctx_(ctx) {}
    virtual ~ToolBase() = default;
    ToolBase(const ToolBase&) = delete;
    ToolBase& operator=(const ToolBase&) = delete;

    /// The tool became the current tool.
    virtual void activate() { /* most tools keep no state across activations */ }
    /// The current tool changed (called on every tool, so per-tool modifiers
    /// such as the move axis lock reset whichever tool was current).
    virtual void toolChanged() { /* only tools with per-tool modifiers reset anything */ }

    virtual void pointerDown(const PointerInput& /*input*/) { /* ignored by default */ }
    virtual void pointerMove(const PointerInput& /*input*/) { /* ignored by default */ }
    virtual void pointerUp(const PointerInput& /*input*/) { /* ignored by default */ }
    /// Returns whether the tool used the key.
    virtual bool key(ToolKey /*key*/) { return false; }

    /// Typed value from the measurements box. Returns an error message, or
    /// nothing on success.
    virtual std::optional<std::string> submit(std::string_view text) = 0;
    /// Label of the measurements box while this tool is current.
    [[nodiscard]] virtual std::string_view measurementLabel() const { return "Measurements"; }

    /// True while a gesture is in progress.
    [[nodiscard]] virtual bool active() const { return false; }
    /// Drops the gesture (Escape, or a tool change).
    virtual void cancel() { /* a tool without gestures has nothing to drop */ }

    /// Status bar hint for the current gesture state.
    [[nodiscard]] virtual std::string hint() const = 0;
    /// Whether the hint is prefixed with the snap kind ("[Endpoint] ...").
    [[nodiscard]] virtual bool showsSnapInHint() const { return true; }
    /// A point the ground snap should not infer from (see ToolContext::snapGround).
    [[nodiscard]] virtual std::optional<Vec3> groundSkip() const { return std::nullopt; }

protected:
    ToolContext& ctx_;
    [[nodiscard]] Editor& editor() const { return ctx_.editor; }
    /// Ends the gesture with nothing committed: drops it, the snap marker and the preview.
    void abandon() {
        cancel();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
};

} // namespace pf
