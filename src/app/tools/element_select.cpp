#include "app/tools/element_select.hpp"

#include "core/geometry/topology.hpp"

namespace pf {

std::optional<ElementRef> elementUnder(const Document& document, const PointerInput& input) {
    if (input.element) return document.find(input.element->element.object) ? std::optional(input.element->element) : std::nullopt;
    if (!input.pick) return std::nullopt;
    const auto* object = document.find(input.pick->objectId);
    if (!object) return std::nullopt;
    const auto topo = buildTopology(*object);
    const auto face = topo.faceFor(input.pick->faceId);
    if (!face) return std::nullopt;
    return ElementRef{object->id, ElementKind::Face, topo.faces[*face].id};
}

std::vector<ElementRef> expandClick(const Document& document, const ElementRef& element, int clicks) {
    const auto* object = document.find(element.object);
    if (!object) return {};
    if (clicks <= 1) return {element};
    const auto topo = buildTopology(*object);
    if (clicks == 2) return expandBounded(element.object, topo, element.kind, element.id);
    return expandConnected(element.object, topo, element.kind, element.id);
}

std::optional<FaceId> primaryFace(const Document& document, const ElementRef& element, const PointerInput& input) {
    if (element.kind == ElementKind::Face) {
        if (const auto* object = document.find(element.object)) {
            const auto topo = buildTopology(*object);
            if (const auto index = topo.indexOf(ElementKind::Face, element.id); index && topo.faces[*index].logical) {
                return topo.faces[*index].logical;
            }
        }
    }
    if (input.pick && input.pick->objectId == element.object) return input.pick->faceId;
    return std::nullopt;
}

void selectElementAt(Editor& editor, const PointerInput& input, SelectModifier modifier) {
    const Document& doc = editor.document();
    const auto element = elementUnder(doc, input);
    if (!element) {
        editor.setSelection(applyElementClick(editor.selectionSet(), {}, std::nullopt, modifier));
        return;
    }
    const auto refs = expandClick(doc, *element, input.clicks);
    // A double or triple click repeats the first click of its series, which
    // already selected (or, with Shift, toggled) the clicked element: replace
    // that click's effect rather than toggling it back.
    SelectionSet base = editor.selectionSet();
    if (input.clicks > 1 && modifier == SelectModifier::Toggle) {
        base.removeElements(refs);
        modifier = SelectModifier::Add;
    }
    editor.setSelection(applyElementClick(base, refs, primaryFace(doc, *element, input), modifier));
}

void hoverElementAt(Editor& editor, const PointerInput& input) { editor.setHoverElement(elementUnder(editor.document(), input)); }

} // namespace pf
