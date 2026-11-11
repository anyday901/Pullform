#include "core/commands/selection.hpp"

#include "core/geometry/solid.hpp"

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
    primary_ = entry;
}

bool SelectionSet::remove(ObjectId id) {
    const auto it = std::ranges::find(objects_, id);
    if (it == objects_.end()) return false;
    objects_.erase(it);
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
    primary_.reset();
}

SelectionSet sanitizeSelection(const Document& doc, const SelectionSet& sel) {
    const auto primary = sanitizeSelection(doc, sel.primary());
    SelectionSet out;
    for (const ObjectId id : sel.objects()) {
        if (primary && id == primary->objectId) out.add(*primary);
        else if (doc.find(id)) out.add(Selection{id, std::nullopt});
    }
    // Adding an object already in the set keeps the order and only makes it primary.
    if (primary) out.add(*primary);
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

SelectionSet applyRegion(const SelectionSet& current, const std::vector<ObjectId>& ids, SelectModifier modifier) {
    SelectionSet next = modifier == SelectModifier::Replace ? SelectionSet{} : current;
    for (const ObjectId id : ids) {
        if (modifier == SelectModifier::Toggle) next.toggle(Selection{id, std::nullopt});
        else if (!next.contains(id)) next.add(Selection{id, std::nullopt});
    }
    return next;
}

} // namespace pf
