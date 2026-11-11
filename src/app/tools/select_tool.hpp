#pragma once

#include "app/tools/tool.hpp"

#include <optional>

namespace pf {

class PushPullTool;

/// Select: a click on an object selects it (with the face under the pointer),
/// a click on empty space clears the selection. Shift adds or removes the
/// clicked object, Ctrl (Cmd on macOS) adds it. Dragging a box selects several:
/// left to right is a window (objects entirely inside), right to left a
/// crossing (objects it touches); Shift and Ctrl apply to the box the same way.
/// The typed value sets the selected object's total height.
class SelectTool final : public ToolBase {
public:
    SelectTool(ToolContext& ctx, PushPullTool& heights) : ToolBase(ctx), heights_(heights) {}

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override { return "Total height"; }
    /// True while a selection box is being dragged.
    [[nodiscard]] bool active() const override { return press_ && press_->boxing; }
    /// Escape during a box drag: back to the selection before the press.
    void cancel() override;
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] bool showsSnapInHint() const override { return false; }

private:
    struct Press {
        Vec2 screen{0.0};
        SelectModifier modifier = SelectModifier::Replace;
        SelectionSet before; ///< selection before the press (a box applies to it)
        bool boxing = false; ///< travelled past the drag threshold
    };

    /// The selection the box would make for the pointer at `input`.
    [[nodiscard]] SelectionSet boxSelection(const PointerInput& input, RegionMode mode) const;

    PushPullTool& heights_;
    std::optional<Press> press_;
};

/// The click modifier for the keys held: Shift toggles, else Ctrl adds.
SelectModifier selectModifier(const PointerInput& input);

} // namespace pf
