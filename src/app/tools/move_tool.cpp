#include "app/tools/move_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <algorithm>

namespace pf {

bool MoveTool::key(ToolKey key) {
    auto axis = arrowAxis(key);
    if (!axis) axis = letterAxis(key);
    if (!axis) return false;
    toggleAxisLock(*axis);
    return true;
}

void MoveTool::toggleAxisLock(MoveAxis axis) {
    pf::toggleAxisLock(axisLock_, axis);
    if (gesture_ && gesture_->lastRay) updatePreview(*gesture_->lastRay);
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
    if (const auto* p = std::get_if<MovePreview>(&editor().preview())) {
        MovePreview next = *p;
        next.copy = copy_;
        editor().setPreview(next);
    }
}

void MoveTool::pointerDown(const PointerInput& input) {
    if (gesture_) {
        // Second click of click-move-click. The pointer may have moved in the
        // same frame before this click, so evaluate the clicked position first.
        updatePreview(input.ray);
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
    // Clicking one of several selected objects moves them all; any other
    // object becomes the selection on its own.
    std::vector<ObjectId> ids = transformGroup(editor(), pick.objectId);
    if (ids.size() > 1) {
        SelectionSet keep = editor().selectionSet();
        keep.add(Selection{pick.objectId, pick.faceId}); // primary, others kept
        editor().setSelection(keep);
    } else {
        editor().select(Selection{pick.objectId, pick.faceId});
    }
    gesture_ = Gesture{pick.objectId, pick.point, o->transform.translation, input.screen, true, std::nullopt, std::move(ids)};
    editor().notify(std::nullopt);
    editor().setPreview(preview(Vec3{0.0}, std::nullopt));
}

MovePreview MoveTool::preview(const Vec3& offset, std::optional<std::string> error) const {
    MovePreview p{gesture_->objectId, gesture_->anchor, offset, axisLock_, std::move(error), copy_, {}};
    if (gesture_->ids.size() > 1) p.objectIds = gesture_->ids;
    return p;
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
        updatePreview(input.ray); // release position, not the last move event
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

void MoveTool::updatePreview(const Ray& ray) {
    if (!gesture_) return;
    gesture_->lastRay = ray;
    if (!editor().document().find(gesture_->objectId)) return;
    auto offset = offsetFor(ray);
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
    editor().setPreview(preview(*offset, checkGroup(gesture_->ids, *offset)));
}

void MoveTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        const auto& pick = input.pick;
        editor().setHover(pick ? std::optional<Selection>(Selection{pick->objectId, pick->faceId}) : std::nullopt);
        return;
    }
    updatePreview(input.ray);
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
    commit(gesture_->ids, gesture_->objectId, offset);
}

bool MoveTool::commit(const std::vector<ObjectId>& ids, ObjectId primary, const Vec3& offset) {
    const LengthUnit unit = editor().document().displayUnit;
    const bool copy = copy_;
    std::string label = (copy ? "Copy " : "Move ") + formatLength(glm::length(offset), unit);
    if (const auto axis = singleAxis(offset)) label += std::string(" along ") + axisName(*axis);
    label += groupSuffix(ids.size());
    bool ok = false;
    if (copy) {
        // The copies take over the selection (the picked object's copy with the
        // same face) so they can be moved or copied again right away.
        const auto face = editor().selection() && editor().selection()->objectId == primary ? editor().selection()->face
                                                                                           : std::optional<FaceId>{};
        const auto it = std::find(ids.begin(), ids.end(), primary);
        const auto primaryIndex = static_cast<std::size_t>(it == ids.end() ? ids.size() - 1 : it - ids.begin());
        ok = editor().commit(
            label, [&ids, &offset](const Document& doc) { return duplicateObjects(doc, ids, offset); },
            [face, primaryIndex](const OpResult& r) {
                SelectionSet copies = SelectionSet::ofObjects(r.objectIds);
                if (primaryIndex < r.objectIds.size()) copies.add(Selection{r.objectIds[primaryIndex], face});
                return copies;
            });
    } else {
        ok = editor().commit(label, [&ids, &offset](const Document& doc) { return moveObjects(doc, ids, offset); });
    }
    if (ok) {
        gesture_.reset();
        axisLock_.reset();
        copy_ = false;
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> MoveTool::submitDistance(std::string_view text) {
    const LengthUnit unit = editor().document().displayUnit;
    ObjectId id;
    std::vector<ObjectId> ids;
    if (gesture_) {
        id = gesture_->objectId;
        ids = gesture_->ids;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to move first.");
        id = o->id;
        ids = transformGroup(editor(), id);
    }
    Vec3 offset{0.0};
    if (text.find_first_of(",;") != std::string_view::npos) {
        const auto list = parseLengthList(text, 3, unit);
        if (!list) return "Enter one distance, or x, y, z separated by commas (for example 500, 0, 0).";
        offset = Vec3{(*list)[0], (*list)[1], (*list)[2]};
    } else {
        const auto d = parseLength(text, unit);
        if (!d) return d.error();
        Vec3 direction{0.0};
        if (axisLock_) {
            direction = axisVector(*axisLock_);
        } else if (const auto* p = std::get_if<MovePreview>(&editor().preview()); gesture_ && p && glm::length(p->offset) > 0.0) {
            direction = glm::normalize(p->offset);
        } else {
            return std::string("Lock an axis first (X, Y or Z, or the arrow keys), move the pointer to set a direction, "
                               "or type x, y, z.");
        }
        offset = direction * *d;
    }
    if (glm::length(offset) < tol::kLengthEpsilon) return std::string("Distance must not be zero.");
    if (auto e = checkGroup(ids, offset)) return e;
    if (!commit(ids, id, offset)) return ctx_.lastError("Move failed.");
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
    if (gesture_) {
        return "Move: click the destination, or type a distance and press Enter. X, Y, Z or the arrow keys lock an "
               "axis. Esc cancels." + lock;
    }
    return "Move: click an object and move it, or select one and type x, y, z. X, Y, Z or the arrow keys lock an "
           "axis." + lock;
}

} // namespace pf
