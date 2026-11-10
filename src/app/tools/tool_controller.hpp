#pragma once

#include "core/commands/editor.hpp"
#include "core/geometry/pick.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace pf {

enum class Tool { Select, Rectangle, PushPull, Orbit };

std::string_view toolName(Tool tool);

/// What the viewport reports for each pointer event. The tool layer never sees
/// windowing or GPU types, so tests can drive it with synthetic events.
struct PointerInput {
    Ray ray;                     ///< world-space pick ray through the cursor
    std::optional<PickHit> pick; ///< logical face under the cursor, if any
    Vec2 screen{0.0};            ///< cursor position in window pixels (tells clicks from drags)
};

struct SnapSettings {
    bool enabled = true;
    double step = 0.1; ///< meters (100 mm)
};

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

    explicit ToolController(Editor& editor) : editor_(editor) {}

    [[nodiscard]] Tool tool() const { return tool_; }
    void setTool(Tool tool);

    [[nodiscard]] SnapSettings& snap() { return snap_; }
    [[nodiscard]] const SnapSettings& snap() const { return snap_; }

    /// True while a gesture is in progress (first corner placed, push/pull started).
    [[nodiscard]] bool active() const { return rect_.has_value() || push_.has_value(); }

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

    /// Sets the total extrusion height of an object (properties panel).
    std::optional<std::string> setTotalHeight(ObjectId id, std::string_view height);

    /// Explanation when a face cannot be push/pulled in this milestone.
    static std::optional<std::string> pushableReason(const FaceId& face);

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
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };

    [[nodiscard]] Vec3 snapPoint(const Vec3& p) const;
    [[nodiscard]] std::optional<Vec3> groundPoint(const PointerInput& input) const;

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
    [[nodiscard]] std::string lastError(const char* fallback) const;

    Editor& editor_;
    Tool tool_ = Tool::Select;
    SnapSettings snap_;
    std::optional<RectGesture> rect_;
    std::optional<PushGesture> push_;
};

} // namespace pf
