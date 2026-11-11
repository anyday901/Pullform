#include "app/tools/select_tool.hpp"

#include "app/tools/push_pull_tool.hpp"
#include "app/tools/tool_support.hpp"

namespace pf {

SelectModifier selectModifier(const PointerInput& input) {
    if (input.shift) return SelectModifier::Toggle;
    if (input.ctrl) return SelectModifier::Add;
    return SelectModifier::Replace;
}

void SelectTool::pointerDown(const PointerInput& input) {
    const SelectModifier modifier = selectModifier(input);
    press_ = Press{input.screen, modifier, editor().selectionSet(), false};
    // The click applies right away; if the press becomes a box drag, the box
    // replaces it (starting again from the selection before the press).
    std::optional<Selection> hit;
    if (input.pick) hit = Selection{input.pick->objectId, input.pick->faceId};
    editor().setSelection(applyClick(editor().selectionSet(), hit, modifier));
}

SelectionSet SelectTool::boxSelection(const PointerInput& input, RegionMode mode) const {
    std::vector<ObjectId> found;
    if (input.project) found = objectsInRegion(editor().document(), ScreenRect{press_->screen, input.screen}, mode, input.project);
    return applyRegion(press_->before, found, press_->modifier);
}

void SelectTool::pointerMove(const PointerInput& input) {
    if (press_ && (press_->boxing || isDrag(press_->screen, input.screen))) {
        if (!press_->boxing) {
            press_->boxing = true;
            editor().setSelection(press_->before);
            editor().setHover(std::nullopt);
        }
        const RegionMode mode = regionModeForDrag(press_->screen, input.screen);
        editor().setPreview(SelectBoxPreview{press_->screen, input.screen, mode, boxSelection(input, mode).objects()});
        return;
    }
    editor().setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, input.pick->faceId}) : std::nullopt);
}

void SelectTool::pointerUp(const PointerInput& input) {
    if (!press_) return;
    if (!press_->boxing && !isDrag(press_->screen, input.screen)) { // a click: already applied
        press_.reset();
        return;
    }
    const SelectionSet next = boxSelection(input, regionModeForDrag(press_->screen, input.screen));
    press_.reset();
    editor().cancelPreview();
    editor().setSelection(next);
    const std::size_t n = editor().selectedObjects().size();
    editor().notify(Message{Message::Kind::Info, n == 1 ? std::string("1 object selected.")
                                                        : std::to_string(n) + " objects selected."});
}

void SelectTool::cancel() {
    if (press_ && press_->boxing) editor().setSelection(press_->before);
    press_.reset();
}

std::optional<std::string> SelectTool::submit(std::string_view text) {
    if (!editor().selection()) return std::string("Select an object first, or pick the Rectangle or Push/Pull tool.");
    return heights_.setTotalHeight(editor().selection()->objectId, text);
}

std::string SelectTool::hint() const {
    if (active()) {
        return "Select: release to select. Left to right selects objects entirely inside; right to left also "
               "selects objects the box touches. Esc cancels.";
    }
    return "Select: click an object, Shift-click to add or remove, Ctrl-click to add, or drag a box. Click empty "
           "space to clear. Delete removes the selection.";
}

} // namespace pf
