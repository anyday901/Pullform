#include "app/tools/select_tool.hpp"

#include "app/tools/element_select.hpp"
#include "app/tools/push_pull_tool.hpp"

namespace pf {

void SelectTool::pointerDown(const PointerInput& input) {
    // A click selects the vertex, edge or face under the pointer (double click:
    // with its edges or faces; triple click: everything connected). Clicking
    // empty space clears the selection.
    selectElementAt(editor(), input, SelectModifier::Replace);
}

void SelectTool::pointerMove(const PointerInput& input) { hoverElementAt(editor(), input); }

std::optional<std::string> SelectTool::submit(std::string_view text) {
    if (!editor().selection()) return std::string("Select an object first, or pick the Rectangle or Push/Pull tool.");
    return heights_.setTotalHeight(editor().selection()->objectId, text);
}

std::string SelectTool::hint() const {
    return "Select: click a face or edge, double-click for a face and its edges, triple-click for everything connected. "
           "Click empty space to clear; Delete removes the selected object.";
}

} // namespace pf
