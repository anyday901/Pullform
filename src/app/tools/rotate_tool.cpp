#include "app/tools/rotate_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

RotatePreview rotatePreview(ObjectId id, const Vec3& pivot, const Vec3& axis,
                            const std::optional<Vec3>& reference = std::nullopt,
                            const std::optional<Vec3>& current = std::nullopt) {
    RotatePreview p;
    p.objectId = id;
    p.pivot = pivot;
    p.axis = axis;
    p.reference = reference;
    p.current = current;
    return p;
}

/// The selected objects as they are in `doc`, for a group preview.
std::vector<ProfileObject> groupResult(const Document& doc, const std::vector<ObjectId>& ids) {
    std::vector<ProfileObject> objects;
    for (const ObjectId id : ids) objects.push_back(*doc.find(id));
    return objects;
}

} // namespace

bool RotateTool::key(ToolKey key) {
    const auto axis = arrowAxis(key);
    if (!axis) return false;
    setAxis(*axis);
    return true;
}

void RotateTool::setAxis(MoveAxis axis) {
    axis_ = axis;
    if (!gesture_) return;
    // Keep the centre; the old reference direction is not in the new plane.
    gesture_->axis = axisVector(axis);
    gesture_->reference.reset();
    editor().setPreview(rotatePreview(gesture_->objectId, gesture_->pivot, gesture_->axis));
}

std::optional<Vec3> RotateTool::pointAt(const PointerInput& input) {
    if (ctx_.snapper.active(input)) {
        if (const auto res = ctx_.snapper.infer(input); res && res->kind >= SnapKind::OnEdge) {
            ctx_.indicator = res;
            return res->point;
        }
    }
    ctx_.indicator.reset();
    if (input.pick) return input.pick->point;
    return ctx_.groundPoint(input);
}

void RotateTool::pointerDown(const PointerInput& input) {
    if (!gesture_) {
        // Selected vertices, edges or faces turn on their own; otherwise the
        // selected object (a first click on an object selects it).
        const SelectionSet& set = editor().selectionSet();
        std::vector<ElementRef> elements = set.elements();
        std::vector<ObjectId> ids;
        const ProfileObject* o = nullptr;
        if (!elements.empty()) {
            ids = elementOwners(set);
            const auto& primary = set.primary();
            o = editor().document().find(primary && set.hasElements(primary->objectId) ? primary->objectId : ids.front());
        } else {
            o = ctx_.transformTarget(input);
            if (o) ids = transformGroup(editor(), o->id);
        }
        if (!o) return;
        // First click: the centre of rotation, on the model or on the ground.
        const auto pivot = pointAt(input);
        if (!pivot) return;
        gesture_ = Gesture{o->id, *pivot, axisVector(axis_), std::nullopt, std::move(ids), std::move(elements), input.screen, true};
        editor().setPreview(rotatePreview(o->id, *pivot, gesture_->axis));
        editor().notify(std::nullopt);
        return;
    }
    const auto* preview = std::get_if<RotatePreview>(&editor().preview());
    if (!gesture_->reference) {
        // Second click: the direction the angle is measured from.
        const auto p = intersectPlane(input.ray, gesture_->pivot, gesture_->axis);
        if (!p || glm::length(*p - gesture_->pivot) < 1e-6) {
            editor().notify(Message{Message::Kind::Error, "Click a point away from the centre to set the reference direction."});
            return;
        }
        gesture_->reference = *p - gesture_->pivot;
        editor().setPreview(rotatePreview(gesture_->objectId, gesture_->pivot, gesture_->axis, gesture_->reference, gesture_->reference));
        return;
    }
    // Third click: commit the previewed angle.
    if (!preview) return;
    if (std::abs(preview->angle) < 1e-12) {
        abandon();
        editor().notify(Message{Message::Kind::Info, "Rotate cancelled (zero angle)."});
        return;
    }
    if (preview->error) {
        editor().notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commit(gesture_->ids, gesture_->pivot, gesture_->axis, preview->angle, gesture_->elements);
}

void RotateTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    if (gesture_->reference || !isDrag(gesture_->pressScreen, input.screen)) return;
    // Dragged from the centre: the axis runs to where the drag ended (along
    // an edge, to turn a face about that edge).
    const auto end = pointAt(input);
    if (!end || glm::length(*end - gesture_->pivot) < 1e-6) return;
    gesture_->axis = glm::normalize(*end - gesture_->pivot);
    editor().setPreview(rotatePreview(gesture_->objectId, gesture_->pivot, gesture_->axis));
}

void RotateTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        editor().setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, std::nullopt}) : std::nullopt);
        return;
    }
    const auto p = intersectPlane(input.ray, gesture_->pivot, gesture_->axis);
    if (!p) return;
    RotatePreview preview = rotatePreview(gesture_->objectId, gesture_->pivot, gesture_->axis, gesture_->reference, *p - gesture_->pivot);
    if (gesture_->ids.size() > 1) preview.objectIds = gesture_->ids;
    if (gesture_->reference && glm::length(*preview.current) > 1e-9) {
        const Vec3 a = *gesture_->reference;
        const Vec3 b = *preview.current;
        double angle = std::atan2(glm::dot(glm::cross(a, b), gesture_->axis), glm::dot(a, b));
        if (ctx_.snap.enabled) angle = snapValue(angle, kSnapRadians);
        preview.angle = angle;
        const Document& doc = editor().document();
        auto result = gesture_->elements.empty() ? rotateObjects(doc, gesture_->ids, gesture_->pivot, gesture_->axis, angle)
                                                 : rotateElements(doc, gesture_->elements, gesture_->pivot, gesture_->axis, angle);
        if (std::abs(angle) < 1e-12 && !gesture_->elements.empty()) {
            // Nothing turns yet: draw the owners as they are.
            result = OpResult{doc, gesture_->objectId, {}};
        }
        if (result) {
            if (const auto* o = result->document.find(gesture_->objectId)) preview.result = *o;
            if (gesture_->ids.size() > 1) preview.group = groupResult(result->document, gesture_->ids);
        } else {
            preview.error = result.error();
        }
    }
    editor().setPreview(std::move(preview));
}

bool RotateTool::commit(const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& axis, double angle,
                        const std::vector<ElementRef>& elements) {
    // One object keeps the selection without a face (a turned face is no
    // longer the one that was clicked); a group, or elements, keep the whole selection.
    const SelectionSet after = ids.size() == 1 && elements.empty() ? SelectionSet{Selection{ids.front(), std::nullopt}}
                                                                   : editor().selectionSet();
    const std::string what = elements.empty() ? std::string() : elementsLabel(elements) + " ";
    const bool ok = editor().commit(
        "Rotate " + what + formatAngle(angle) + groupSuffix(ids.size()),
        [&ids, &pivot, &axis, angle, &elements](const Document& doc) {
            return elements.empty() ? rotateObjects(doc, ids, pivot, axis, angle) : rotateElements(doc, elements, pivot, axis, angle);
        },
        [&after](const OpResult&) { return after; });
    if (ok) {
        gesture_.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> RotateTool::submitAngle(std::string_view text) {
    const auto parsed = parseAngle(text);
    if (!parsed) return parsed.error();
    double angle = *parsed;
    std::vector<ObjectId> ids;
    std::vector<ElementRef> elements;
    Vec3 pivot{0.0};
    Vec3 axis = axisVector(axis_);
    if (gesture_) {
        ids = gesture_->ids;
        elements = gesture_->elements;
        pivot = gesture_->pivot;
        axis = gesture_->axis;
        // Typed values follow the direction the pointer is turning.
        if (const auto* p = std::get_if<RotatePreview>(&editor().preview()); p && p->angle < 0.0) angle = -angle;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to rotate first.");
        if (!editor().selectionSet().elements().empty()) {
            elements = editor().selectionSet().elements();
            ids = elementOwners(editor().selectionSet());
            pivot = elementsBaseCenter(editor().document(), elements);
        } else {
            ids = transformGroup(editor(), o->id);
            pivot = groupBaseCenter(editor().document(), ids);
        }
    }
    if (std::abs(angle) < 1e-12) return std::string("Angle must not be zero.");
    if (!commit(ids, pivot, axis, angle, elements)) return ctx_.lastError("Could not rotate the object.");
    return std::nullopt;
}

std::string RotateTool::hint() const {
    const std::string axis = " Axis " + std::string(axisName(axis_)) + " (" + axisColorName(axis_) +
                             "); arrow keys change it (Right X, Left Y, Up Z).";
    if (!gesture_) {
        if (!editor().selection()) return "Rotate: click the object to rotate." + axis;
        return "Rotate: click the centre of rotation (or press there and drag along an edge to turn about it), or type "
               "an angle in degrees to turn about the base centre." + axis;
    }
    if (!gesture_->reference) return "Rotate: click to set the reference direction, or type an angle. Esc cancels." + axis;
    return "Rotate: move to turn (15° steps with grid snap) and click, or type an angle. Esc cancels.";
}

} // namespace pf
