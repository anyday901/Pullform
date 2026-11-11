#include "app/tools/select_tool.hpp"

#include "app/tools/push_pull_tool.hpp"

namespace pf {

void SelectTool::pointerDown(const PointerInput& input) {
    if (input.pick) editor().select(Selection{input.pick->objectId, input.pick->faceId});
    else editor().select(std::nullopt); // clicking empty space clears the selection
}

void SelectTool::pointerMove(const PointerInput& input) {
    editor().setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, input.pick->faceId}) : std::nullopt);
}

std::optional<std::string> SelectTool::submit(std::string_view text) {
    if (!editor().selection()) return std::string("Select an object first, or pick the Rectangle or Push/Pull tool.");
    return heights_.setTotalHeight(editor().selection()->objectId, text);
}

std::string SelectTool::hint() const {
    return "Select: click an object to select it, click empty space to clear. Delete removes the selected object.";
}

} // namespace pf
