#include "app/tools/move_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <format>

namespace pf {

namespace {

constexpr const char* kNoElementCopy =
    "Copies of vertices, edges or faces are not supported yet: turn copy mode off to move them.";

} // namespace

bool MoveTool::key(ToolKey key) {
    auto axis = arrowAxis(key);
    if (!axis) axis = letterAxis(key);
    if (!axis) return false;
    toggleAxisLock(*axis);
    return true;
}

void MoveTool::toggleAxisLock(MoveAxis axis) {
    pf::toggleAxisLock(axisLock_, axis);
    if (gesture_ && gesture_->lastRay) {
        ctx_.indicator.reset(); // the lock replaces any inferred point
        updatePreview(*gesture_->lastRay, axisLock_ ? std::nullopt : gesture_->lastSnap);
    }
    else if (gesture_) {
        if (const auto* p = std::get_if<MovePreview>(&editor().preview())) {
            MovePreview next = *p;
            next.axis = axisLock_;
            editor().setPreview(next);
        }
    }
}

void MoveTool::toggleCopy() {
    copy_ = !copy_;
    lastCopy_.reset(); // a new copy starts; typed values no longer edit the last one
    if (const auto* p = std::get_if<MovePreview>(&editor().preview())) {
        MovePreview next = *p;
        next.copy = copy_;
        editor().setPreview(next);
    }
}

std::optional<Vec3> MoveTool::snappedOffset(const PointerInput& input) {
    if (!gesture_ || axisLock_ || !ctx_.snapper.active(input)) {
        ctx_.indicator.reset();
        return std::nullopt;
    }
    // Moving elements may snap to the rest of their own object; a whole object skips itself.
    const std::optional<ObjectId> exclude = gesture_->elements.empty() ? std::optional(gesture_->objectId) : std::nullopt;
    const auto res = ctx_.snapper.infer(input, InferOptions{gesture_->anchor, std::nullopt, exclude, std::nullopt, false});
    // A face point or the ground is the free move's job (it keeps the height).
    if (!res || res->kind < SnapKind::Inference) {
        ctx_.indicator.reset();
        return std::nullopt;
    }
    ctx_.indicator = res;
    Vec3 offset = res->point - gesture_->anchor;
    if (res->kind == SnapKind::Axis && res->constraint && res->constraint->axis >= 0 && ctx_.snap.gridOn()) {
        // Along an axis the object's corner still lands on the grid, as in a free move.
        const Vec3 axis = res->constraint->direction;
        const double start = glm::dot(gesture_->startTranslation, axis);
        offset = axis * (snapValue(start + glm::dot(offset, axis), ctx_.snap.step) - start);
    }
    return offset;
}

void MoveTool::pointerDown(const PointerInput& input) {
    if (gesture_) {
        // Second click of click-move-click. The pointer may have moved in the
        // same frame before this click, so evaluate the clicked position first.
        updatePreview(input.ray, snappedOffset(input));
        finish();
        return;
    }
    if (!input.pick) {
        editor().select(std::nullopt);
        return;
    }
    const auto& pick = *input.pick;
    const auto* o = editor().document().find(pick.objectId);
    if (!o) return;
    // Selected vertices, edges or faces move on their own when one of their
    // objects is clicked; the selection stays as it is.
    const SelectionSet& set = editor().selectionSet();
    std::vector<ElementRef> elements;
    std::vector<ObjectId> ids;
    if (set.hasElements(pick.objectId)) {
        elements = set.elements();
        ids = elementOwners(set);
    } else {
        // Clicking one of several selected objects moves them all; any other
        // object becomes the selection on its own.
        ids = transformGroup(editor(), pick.objectId);
    }
    if (!elements.empty()) {
        // Nothing to change in the selection.
    } else if (ids.size() > 1) {
        SelectionSet keep = editor().selectionSet();
        keep.add(Selection{pick.objectId, pick.faceId}); // primary, others kept
        editor().setSelection(keep);
    } else {
        editor().select(Selection{pick.objectId, pick.faceId});
    }
    // Start from a corner, midpoint or edge point under the pointer when there
    // is one, so the move can put exactly that point somewhere.
    Vec3 anchor = pick.point;
    if (ctx_.snapper.active(input)) {
        if (const auto res = ctx_.snapper.infer(input); res && res->kind >= SnapKind::OnEdge) anchor = res->point;
    }
    lastCopy_.reset(); // a new move ends editing the last copy
    gesture_ = Gesture{pick.objectId, anchor, o->transform.translation, input.screen, true, std::nullopt, std::nullopt,
                       std::move(ids), std::move(elements)};
    editor().notify(std::nullopt);
    editor().setPreview(checkedPreview(Vec3{0.0}));
}

MovePreview MoveTool::preview(const Vec3& offset, std::optional<std::string> error) const {
    MovePreview p{gesture_->objectId, gesture_->anchor, offset, axisLock_, std::move(error), copy_, {}, {}, false};
    if (gesture_->ids.size() > 1) p.objectIds = gesture_->ids;
    if (gesture_->elements.empty()) return p;
    // Elements: draw their owners stretched (or unchanged while refused).
    p.elements = true;
    p.objectIds = gesture_->ids;
    const Document& doc = editor().document();
    if (!p.error && copy_) p.error = std::string(kNoElementCopy);
    if (!p.error && glm::length(offset) >= tol::kLengthEpsilon) {
        if (auto moved = moveElements(doc, gesture_->elements, offset)) {
            for (const ObjectId id : gesture_->ids) p.results.push_back(*moved->document.find(id));
            return p;
        } else {
            p.error = moved.error();
        }
    }
    for (const ObjectId id : gesture_->ids) {
        if (const auto* o = doc.find(id)) p.results.push_back(*o);
    }
    return p;
}

MovePreview MoveTool::checkedPreview(const Vec3& offset) const {
    return preview(offset, gesture_->elements.empty() ? checkGroup(gesture_->ids, offset) : std::nullopt);
}

std::optional<std::string> MoveTool::checkGroup(const std::vector<ObjectId>& ids, const Vec3& offset) const {
    for (const ObjectId id : ids) {
        const auto* o = editor().document().find(id);
        if (!o) return std::string("The object no longer exists.");
        if (auto e = checkMove(*o, offset)) return e;
    }
    return std::nullopt;
}

void MoveTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    if (isDrag(gesture_->pressScreen, input.screen)) {
        updatePreview(input.ray, snappedOffset(input)); // release position, not the last move event
        finish();
    }
}

std::optional<Vec3> MoveTool::offsetFor(const Ray& ray) const {
    if (!gesture_) return std::nullopt;
    const SnapSettings& snap = ctx_.snap;
    const bool snapping = snap.enabled && snap.step > 0.0;
    // Snapping places the object's corner (its translation) on the grid, the
    // same points rectangle corners snap to.
    const double step = snap.step;
    const auto snapped = [snapping, step](double start, double delta) {
        return snapping ? snapValue(start + delta, step) - start : delta;
    };
    const Vec3& start = gesture_->startTranslation;
    if (axisLock_) {
        const Vec3 axis = axisVector(*axisLock_);
        const auto t = closestParameterOnAxis(gesture_->anchor, axis, ray);
        if (!t.has_value()) return std::nullopt;
        // `axis` is a unit world axis, so the dot product is exactly that component.
        return axis * snapped(glm::dot(start, axis), *t);
    }
    // Free move: slide in the horizontal plane through the picked point.
    const auto p = intersectHorizontalPlane(ray, gesture_->anchor.z);
    if (!p) return std::nullopt;
    const Vec3 d = *p - gesture_->anchor;
    return Vec3{snapped(start.x, d.x), snapped(start.y, d.y), 0.0};
}

void MoveTool::updatePreview(const Ray& ray, std::optional<Vec3> snapped) {
    if (!gesture_) return;
    gesture_->lastRay = ray;
    gesture_->lastSnap = snapped;
    if (!editor().document().find(gesture_->objectId)) return;
    auto offset = !axisLock_ && snapped ? snapped : offsetFor(ray);
    if (!offset) {
        // The ray cannot be resolved under the current constraint (parallel to
        // the locked axis, or no forward hit on the horizontal plane). Never keep
        // an offset from the previous constraint: project it onto this one.
        const auto* previous = std::get_if<MovePreview>(&editor().preview());
        const Vec3 last = previous ? previous->offset : Vec3{0.0};
        if (axisLock_) {
            const Vec3 axis = axisVector(*axisLock_);
            offset = axis * glm::dot(last, axis);
        } else {
            offset = Vec3{last.x, last.y, 0.0};
        }
    }
    editor().setPreview(checkedPreview(*offset));
}

void MoveTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        const auto& pick = input.pick;
        editor().setHover(pick ? std::optional<Selection>(Selection{pick->objectId, pick->faceId}) : std::nullopt);
        return;
    }
    updatePreview(input.ray, snappedOffset(input));
}

void MoveTool::finish() {
    const auto* preview = std::get_if<MovePreview>(&editor().preview());
    if (!gesture_ || !preview) return;
    const Vec3 offset = preview->offset;
    if (glm::length(offset) < tol::kLengthEpsilon) {
        // A zero-distance gesture is a no-op, not an edit.
        abandon();
        editor().notify(Message{Message::Kind::Info, "Move cancelled (zero distance)."});
        return;
    }
    if (preview->error) {
        // Keep the gesture alive so the user can move back into range.
        editor().notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commit(gesture_->ids, gesture_->objectId, offset, gesture_->elements);
}

bool MoveTool::commit(const std::vector<ObjectId>& ids, ObjectId primary, const Vec3& offset,
                      const std::vector<ElementRef>& elements) {
    const LengthUnit unit = editor().document().displayUnit;
    const bool copy = copy_;
    if (copy && !elements.empty()) {
        editor().notify(Message{Message::Kind::Error, kNoElementCopy});
        return false;
    }
    std::string label = (copy ? "Copy " : "Move ") + (elements.empty() ? std::string() : elementsLabel(elements) + " ") +
                        formatLength(glm::length(offset), unit);
    if (const auto axis = singleAxis(offset)) label += std::string(" along ") + axisName(*axis);
    label += groupSuffix(ids.size());
    bool ok = false;
    if (!elements.empty()) {
        const SelectionSet keep = editor().selectionSet();
        ok = editor().commit(
            label, [&elements, &offset](const Document& doc) { return moveElements(doc, elements, offset); },
            [&keep](const OpResult&) { return keep; });
    } else if (copy) {
        // The copies take over the selection (the picked object's copy with the
        // same face) so they can be moved or copied again right away.
        const auto face = editor().selection() && editor().selection()->objectId == primary ? editor().selection()->face
                                                                                           : std::optional<FaceId>{};
        const auto it = std::find(ids.begin(), ids.end(), primary);
        const auto primaryIndex = static_cast<std::size_t>(it == ids.end() ? ids.size() - 1 : it - ids.begin());
        LastCopy last{ids, primaryIndex, face, {}, 0, {}};
        ok = editor().commit(
            label, [&ids, &offset](const Document& doc) { return duplicateObjects(doc, ids, offset); },
            [&last](const OpResult& r) { return last.lastSet(r); });
        if (ok) {
            last.settle(editor());
            lastCopy_ = MoveCopy{std::move(last), offset};
        }
    } else {
        ok = editor().commit(label, [&ids, &offset](const Document& doc) { return moveObjects(doc, ids, offset); });
    }
    if (ok) {
        gesture_.reset();
        ctx_.indicator.reset();
        axisLock_.reset();
        copy_ = false;
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> MoveTool::editCopy(const Vec3& offset, ArrayCount array) {
    MoveCopy& last = *lastCopy_;
    const std::vector<ObjectId> ids = last.copy.originals;
    const Vec3 step = array.divide ? offset / static_cast<double>(array.count) : offset;
    if (glm::length(step) < tol::kLengthEpsilon) return std::string("Distance must not be zero.");
    const LengthUnit unit = editor().document().displayUnit;
    const std::string spacing = formatLength(glm::length(step), unit);
    std::string label = array.count == 1 ? std::format("Copy {}", spacing) : std::format("Array {} copies {} apart", array.count, spacing);
    if (const auto axis = singleAxis(step)) label += std::format(" along {}", axisName(*axis));
    label += groupSuffix(ids.size());
    const LastCopy& copy = last.copy;
    if (!editor().amendLast(
            label, [&ids, &step, count = array.count](const Document& doc) { return arrayObjects(doc, ids, step, count); },
            [&copy](const OpResult& r) { return copy.lastSet(r); })) {
        return ctx_.lastError("Could not make the copies.");
    }
    last.offset = offset;
    last.copy.array = array;
    last.copy.settle(editor());
    axisLock_.reset();
    return std::nullopt;
}

Expected<Vec3> MoveTool::typedOffset(std::string_view text, const std::optional<Vec3>& direction) const {
    const LengthUnit unit = editor().document().displayUnit;
    Vec3 offset{0.0};
    if (text.find_first_of(",;") != std::string_view::npos) {
        const auto list = parseLengthList(text, 3, unit);
        if (!list) return Error{"Enter one distance, or x, y, z separated by commas (for example 500, 0, 0)."};
        offset = Vec3{(*list)[0], (*list)[1], (*list)[2]};
    } else {
        const auto d = parseLength(text, unit);
        if (!d) return Error{d.error()};
        if (!direction) {
            return Error{"Lock an axis first (X, Y or Z, or the arrow keys), move the pointer to set a direction, "
                         "or type x, y, z."};
        }
        offset = *direction * *d;
    }
    if (glm::length(offset) < tol::kLengthEpsilon) return Error{"Distance must not be zero."};
    return offset;
}

std::optional<std::string> MoveTool::submitArray(const Expected<ArrayCount>& array) {
    if (!array) return array.error();
    if (gesture_) return std::string("Finish the copy first, then type 5x or /5.");
    if (!arrayEditable()) return noCopyToArray("Move");
    return editCopy(lastCopy_->offset, *array);
}

std::optional<std::string> MoveTool::submitDistance(std::string_view text) {
    if (const auto array = parseArrayCount(text)) return submitArray(*array);
    if (!gesture_ && arrayEditable()) {
        // A new distance re-spaces the copies, keeping the array: along the
        // locked axis, else along the copy's direction; or a new vector.
        const Vec3 direction = axisLock_ ? axisVector(*axisLock_) : glm::normalize(lastCopy_->offset);
        const auto offset = typedOffset(text, direction);
        if (!offset) return offset.error();
        return editCopy(*offset, lastCopy_->copy.array);
    }
    ObjectId id;
    std::vector<ObjectId> ids;
    std::vector<ElementRef> elements;
    if (gesture_) {
        id = gesture_->objectId;
        ids = gesture_->ids;
        elements = gesture_->elements;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to move first.");
        id = o->id;
        if (!editor().selectionSet().elements().empty()) {
            elements = editor().selectionSet().elements();
            ids = elementOwners(editor().selectionSet());
        } else {
            ids = transformGroup(editor(), id);
        }
    }
    std::optional<Vec3> direction;
    if (axisLock_) {
        direction = axisVector(*axisLock_);
    } else if (const auto* p = std::get_if<MovePreview>(&editor().preview()); gesture_ && p && glm::length(p->offset) > 0.0) {
        direction = glm::normalize(p->offset);
    }
    const auto offset = typedOffset(text, direction);
    if (!offset) return offset.error();
    if (elements.empty()) {
        if (auto e = checkGroup(ids, *offset)) return e;
    }
    if (!commit(ids, id, *offset, elements)) return ctx_.lastError("Move failed.");
    return std::nullopt;
}

std::string MoveTool::hint() const {
    std::string lock = axisLock_ ? std::string(" Locked to ") + axisName(*axisLock_) + "." : std::string();
#ifdef __APPLE__
    constexpr const char* copyKey = "Option";
#else
    constexpr const char* copyKey = "Ctrl";
#endif
    lock += copy_ ? std::string(" Copying (") + copyKey + " to stop)." : std::string(" ") + copyKey + " makes a copy.";
    if (!gesture_ && arrayEditable()) {
        return "Move: type 5x for five copies at this spacing, /5 for five spread between, or a new distance." + lock;
    }
    if (gesture_) {
        return "Move: click the destination, or type a distance and press Enter. X, Y, Z or the arrow keys lock an "
               "axis. Esc cancels." + lock;
    }
    if (!editor().selectionSet().elements().empty()) {
        return "Move: click the selected geometry's object and move: the selected vertices, edges and faces move and "
               "the rest stretches. Or type x, y, z. X, Y, Z or the arrow keys lock an axis." + lock;
    }
    return "Move: click an object and move it, or select one and type x, y, z. X, Y, Z or the arrow keys lock an "
           "axis." + lock;
}

} // namespace pf
