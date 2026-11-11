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

void RotateTool::pointerDown(const PointerInput& input) {
    if (!gesture_) {
        const auto* o = ctx_.transformTarget(input);
        if (!o) return;
        // First click: the centre of rotation, on a face or on the ground.
        std::optional<Vec3> pivot;
        if (input.pick) pivot = input.pick->point;
        else pivot = ctx_.groundPoint(input);
        if (!pivot) return;
        gesture_ = Gesture{o->id, *pivot, axisVector(axis_), std::nullopt, transformGroup(editor(), o->id)};
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
    commit(gesture_->ids, gesture_->pivot, gesture_->axis, preview->angle);
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
        if (auto result = rotateObjects(editor().document(), gesture_->ids, gesture_->pivot, gesture_->axis, angle); result) {
            if (const auto* o = result->document.find(gesture_->objectId)) preview.result = *o;
            if (gesture_->ids.size() > 1) {
                for (const ObjectId id : gesture_->ids) preview.group.push_back(*result->document.find(id));
            }
        } else {
            preview.error = result.error();
        }
    }
    editor().setPreview(std::move(preview));
}

bool RotateTool::commit(const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& axis, double angle) {
    // One object keeps the selection without a face (a turned face is no
    // longer the one that was clicked); a group keeps the whole selection.
    const SelectionSet after = ids.size() == 1 ? SelectionSet{Selection{ids.front(), std::nullopt}} : editor().selectionSet();
    const bool ok = editor().commit(
        "Rotate " + formatAngle(angle) + groupSuffix(ids.size()),
        [&ids, &pivot, &axis, angle](const Document& doc) { return rotateObjects(doc, ids, pivot, axis, angle); },
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
    Vec3 pivot{0.0};
    Vec3 axis = axisVector(axis_);
    if (gesture_) {
        ids = gesture_->ids;
        pivot = gesture_->pivot;
        axis = gesture_->axis;
        // Typed values follow the direction the pointer is turning.
        if (const auto* p = std::get_if<RotatePreview>(&editor().preview()); p && p->angle < 0.0) angle = -angle;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to rotate first.");
        ids = transformGroup(editor(), o->id);
        pivot = groupBaseCenter(editor().document(), ids);
    }
    if (std::abs(angle) < 1e-12) return std::string("Angle must not be zero.");
    if (!commit(ids, pivot, axis, angle)) return ctx_.lastError("Could not rotate the object.");
    return std::nullopt;
}

std::string RotateTool::hint() const {
    const std::string axis = " Axis " + std::string(axisName(axis_)) + " (" + axisColorName(axis_) +
                             "); arrow keys change it (Right X, Left Y, Up Z).";
    if (!gesture_) {
        if (!editor().selection()) return "Rotate: click the object to rotate." + axis;
        return "Rotate: click the centre of rotation, or type an angle in degrees to turn about the base centre." + axis;
    }
    if (!gesture_->reference) return "Rotate: click to set the reference direction, or type an angle. Esc cancels." + axis;
    return "Rotate: move to turn (15° steps with grid snap) and click, or type an angle. Esc cancels.";
}

} // namespace pf
