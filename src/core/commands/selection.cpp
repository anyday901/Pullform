#include "core/commands/selection.hpp"

#include "core/geometry/solid.hpp"
#include "core/geometry/topology.hpp"

#include <algorithm>

namespace pf {

std::optional<Selection> sanitizeSelection(const Document& doc, const std::optional<Selection>& sel) {
    if (!sel) return std::nullopt;
    const auto* o = doc.find(sel->objectId);
    if (!o) return std::nullopt;
    if (!sel->face) return sel;
    const auto faces = buildFaces(*o);
    const bool exists = std::any_of(faces.begin(), faces.end(), [&](const FacePolygon& f) { return f.id == *sel->face; });
    if (exists) return sel;
    if (sel->face->kind == FaceKind::Profile && o->extrusion) return Selection{o->id, FaceId::cap()};
    if (sel->face->kind == FaceKind::Cap && !o->extrusion) return Selection{o->id, FaceId::profile()};
    return Selection{o->id, std::nullopt};
}

SelectionSet::SelectionSet(const std::optional<Selection>& single) {
    if (single) add(*single);
}

SelectionSet SelectionSet::ofObjects(const std::vector<ObjectId>& ids) {
    SelectionSet s;
    for (const ObjectId id : ids) s.add(Selection{id, std::nullopt});
    return s;
}

bool SelectionSet::contains(ObjectId id) const { return std::ranges::find(objects_, id) != objects_.end(); }

void SelectionSet::add(const Selection& entry) {
    if (!contains(entry.objectId)) objects_.push_back(entry.objectId);
    else std::erase_if(elements_, [&](const ElementRef& e) { return e.object == entry.objectId; }); // now whole
    primary_ = entry;
}

bool SelectionSet::containsElement(const ElementRef& element) const { return std::ranges::find(elements_, element) != elements_.end(); }

bool SelectionSet::hasElements(ObjectId id) const {
    return std::ranges::any_of(elements_, [&](const ElementRef& e) { return e.object == id; });
}

void SelectionSet::addElements(const std::vector<ElementRef>& elements, const std::optional<FaceId>& face) {
    if (elements.empty()) return;
    for (const auto& e : elements) {
        if (containsElement(e)) continue;
        // An object already selected whole stays whole: its elements are implied.
        if (contains(e.object) && !hasElements(e.object)) continue;
        if (!contains(e.object)) objects_.push_back(e.object);
        elements_.push_back(e);
    }
    primary_ = Selection{elements.front().object, face};
}

void SelectionSet::removeElements(const std::vector<ElementRef>& elements) {
    std::vector<ObjectId> touched;
    for (const auto& e : elements) {
        const auto it = std::ranges::find(elements_, e);
        if (it == elements_.end()) continue;
        elements_.erase(it);
        if (std::ranges::find(touched, e.object) == touched.end()) touched.push_back(e.object);
    }
    for (const ObjectId id : touched) {
        if (!hasElements(id)) remove(id);
    }
}

void SelectionSet::toggleElements(const std::vector<ElementRef>& elements, const std::optional<FaceId>& face) {
    // Elements of an object selected whole (by a box or Select All) are implied,
    // so toggling them removes that object.
    const auto selectedWhole = [&](const ElementRef& e) { return contains(e.object) && !hasElements(e.object); };
    if (!elements.empty() && std::ranges::all_of(elements, selectedWhole)) {
        for (const auto& e : elements) remove(e.object);
        return;
    }
    const bool all = !elements.empty() && std::ranges::all_of(elements, [&](const ElementRef& e) { return containsElement(e); });
    if (all) removeElements(elements);
    else addElements(elements, face);
}

bool SelectionSet::remove(ObjectId id) {
    const auto it = std::ranges::find(objects_, id);
    if (it == objects_.end()) return false;
    objects_.erase(it);
    std::erase_if(elements_, [&](const ElementRef& e) { return e.object == id; });
    if (primary_ && primary_->objectId == id) {
        primary_.reset();
        if (!objects_.empty()) primary_ = Selection{objects_.back(), std::nullopt};
    }
    return true;
}

void SelectionSet::toggle(const Selection& entry) {
    if (!remove(entry.objectId)) add(entry);
}

void SelectionSet::clear() {
    objects_.clear();
    elements_.clear();
    primary_.reset();
}

SelectionSet sanitizeSelection(const Document& doc, const SelectionSet& sel) {
    const auto primary = sanitizeSelection(doc, sel.primary());
    SelectionSet out;
    for (const ObjectId id : sel.objects()) {
        if (primary && id == primary->objectId) out.add(*primary);
        else if (doc.find(id)) out.add(Selection{id, std::nullopt});
    }
    // Element entries: keep those whose object still has an element with that id.
    std::vector<ElementRef> kept;
    std::optional<ObjectId> builtFor;
    ElementTopology topo;
    for (const auto& e : sel.elements()) {
        const auto* o = doc.find(e.object);
        if (!o) continue;
        if (builtFor != e.object) {
            topo = buildTopology(*o);
            builtFor = e.object;
        }
        if (topo.indexOf(e.kind, e.id)) kept.push_back(e);
    }
    out.elements_.clear();
    for (const auto& e : kept) out.elements_.push_back(e);
    // An object that had only element entries and lost them all is dropped.
    for (const ObjectId id : sel.objects()) {
        if (sel.hasElements(id) && !out.hasElements(id)) out.remove(id);
    }
    // Adding an object already in the set keeps the order and only makes it primary.
    if (primary && out.contains(primary->objectId)) {
        const auto saved = out.elements_;
        out.add(*primary);
        out.elements_ = saved; // add() would make the primary whole
    }
    return out;
}

SelectionSet selectAll(const Document& doc) {
    SelectionSet s;
    for (const auto& o : doc.objects) s.add(Selection{o->id, std::nullopt});
    return s;
}

SelectionSet applyClick(const SelectionSet& current, const std::optional<Selection>& hit, SelectModifier modifier) {
    if (!hit) return modifier == SelectModifier::Replace ? SelectionSet{} : current;
    SelectionSet next = modifier == SelectModifier::Replace ? SelectionSet{} : current;
    if (modifier == SelectModifier::Toggle) next.toggle(*hit);
    else next.add(*hit);
    return next;
}

SelectionSet applyElementClick(const SelectionSet& current, const std::vector<ElementRef>& hit,
                               const std::optional<FaceId>& face, SelectModifier modifier) {
    if (hit.empty()) return modifier == SelectModifier::Replace ? SelectionSet{} : current;
    SelectionSet next = modifier == SelectModifier::Replace ? SelectionSet{} : current;
    if (modifier == SelectModifier::Toggle) next.toggleElements(hit, face);
    else next.addElements(hit, face);
    return next;
}

SelectionSet applyRegion(const SelectionSet& current, const std::vector<ObjectId>& ids, SelectModifier modifier) {
    SelectionSet next = modifier == SelectModifier::Replace ? SelectionSet{} : current;
    for (const ObjectId id : ids) {
        if (modifier == SelectModifier::Toggle) next.toggle(Selection{id, std::nullopt});
        else if (!next.contains(id)) next.add(Selection{id, std::nullopt});
    }
    return next;
}

} // namespace pf
