#pragma once

#include "app/tools/snapper.hpp"
#include "core/commands/editor.hpp"
#include "core/document/element_ref.hpp"

#include <optional>
#include <vector>

namespace pf {

/// The vertex, edge or face under the pointer for the Select tool: the
/// viewport's element pick, or else the face of the plain face pick (as
/// headless tests and callers without element picking provide).
std::optional<ElementRef> elementUnder(const Document& document, const PointerInput& input);

/// What a click on `element` selects: the element itself (1 click), the
/// element with its bounding edges or bordering faces (2, see expandBounded),
/// or everything connected to it (3, see expandConnected).
std::vector<ElementRef> expandClick(const Document& document, const ElementRef& element, int clicks);

/// The logical face (push/pull, properties panel) an element click leaves as
/// the primary's face: the clicked face, else the face under the pointer.
std::optional<FaceId> primaryFace(const Document& document, const ElementRef& element, const PointerInput& input);

/// The Select tool's click: selects the element under the pointer, expanded by
/// the click count, combined with the selection by `modifier`. Clicking empty
/// space clears the selection (Replace) or keeps it (Toggle, Add).
void selectElementAt(Editor& editor, const PointerInput& input, SelectModifier modifier);

/// The Select tool's hover: the element under the pointer.
void hoverElementAt(Editor& editor, const PointerInput& input);

} // namespace pf
