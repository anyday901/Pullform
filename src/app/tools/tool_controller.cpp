#include "app/tools/tool_controller.hpp"

#include "core/commands/operations.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

double snapValue(double v, double step) { return std::round(v / step) * step; }

std::string trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return std::string(s);
}

} // namespace

std::string_view toolName(Tool tool) {
    switch (tool) {
    case Tool::Select: return "Select";
    case Tool::Rectangle: return "Rectangle";
    case Tool::PushPull: return "Push/Pull";
    case Tool::Rotate: return "Rotate";
    case Tool::Scale: return "Scale";
    case Tool::Orbit: return "Orbit";
    }
    return "Select";
}

Vec3 axisVector(RotateAxis axis) {
    switch (axis) {
    case RotateAxis::X: return {1.0, 0.0, 0.0};
    case RotateAxis::Y: return {0.0, 1.0, 0.0};
    case RotateAxis::Z: return {0.0, 0.0, 1.0};
    }
    return {0.0, 0.0, 1.0};
}

std::string_view axisName(RotateAxis axis) {
    switch (axis) {
    case RotateAxis::X: return "X (red)";
    case RotateAxis::Y: return "Y (green)";
    case RotateAxis::Z: return "Z (blue)";
    }
    return "Z (blue)";
}

namespace {

RotatePreview rotatePreview(ObjectId id, const Vec3& pivot, const Vec3& axis, std::optional<Vec3> reference = std::nullopt,
                            std::optional<Vec3> current = std::nullopt) {
    RotatePreview p;
    p.objectId = id;
    p.pivot = pivot;
    p.axis = axis;
    p.reference = reference;
    p.current = current;
    return p;
}

ScalePreview scalePreview(ObjectId id, const Vec3& pivot, const Vec3& start, const Vec3& current) {
    ScalePreview p;
    p.objectId = id;
    p.pivot = pivot;
    p.start = start;
    p.current = current;
    return p;
}

} // namespace

Vec3 baseCenter(const ProfileObject& o) {
    Vec2 sum{0.0};
    for (const auto& p : o.profile.boundary) sum += p;
    const Vec2 c = o.profile.boundary.empty() ? Vec2{0.0} : sum / static_cast<double>(o.profile.boundary.size());
    return profileToWorld(o, c, 0.0);
}

void ToolController::setTool(Tool tool) {
    if (tool == tool_) return;
    cancel();
    tool_ = tool;
    editor_.setHover(std::nullopt);
    editor_.notify(std::nullopt);
}

bool ToolController::cancel() {
    const bool had = active() || !std::holds_alternative<std::monostate>(editor_.preview());
    rect_.reset();
    push_.reset();
    rotate_.reset();
    scale_.reset();
    editor_.cancelPreview();
    return had;
}

Vec3 ToolController::snapPoint(const Vec3& p) const {
    if (!snap_.enabled || snap_.step <= 0.0) return p;
    return {snapValue(p.x, snap_.step), snapValue(p.y, snap_.step), p.z};
}

std::optional<Vec3> ToolController::groundPoint(const PointerInput& input) const {
    const auto p = intersectHorizontalPlane(input.ray, 0.0);
    if (!p) return std::nullopt;
    return snapPoint(*p);
}

void ToolController::pointerDown(const PointerInput& input) {
    switch (tool_) {
    case Tool::Select: selectDown(input); break;
    case Tool::Rectangle: rectDown(input); break;
    case Tool::PushPull: pushDown(input); break;
    case Tool::Rotate: rotateDown(input); break;
    case Tool::Scale: scaleDown(input); break;
    case Tool::Orbit: break; // navigation owns the drag
    }
}

void ToolController::pointerMove(const PointerInput& input) {
    switch (tool_) {
    case Tool::Select:
        editor_.setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, input.pick->faceId})
                                    : std::nullopt);
        break;
    case Tool::Rectangle: rectMove(input); break;
    case Tool::PushPull: pushMove(input); break;
    case Tool::Rotate: rotateMove(input); break;
    case Tool::Scale: scaleMove(input); break;
    case Tool::Orbit: break;
    }
}

void ToolController::pointerUp(const PointerInput& input) {
    const auto moved = [&](const Vec2& press) { return glm::length(input.screen - press) > kDragThresholdPx; };
    if (rect_ && rect_->pressed) {
        rect_->pressed = false;
        // Press-drag-release draws in one gesture; a plain click waits for the second corner.
        if (moved(rect_->pressScreen)) rectFinish(input);
    } else if (push_ && push_->pressed) {
        push_->pressed = false;
        if (moved(push_->pressScreen)) pushFinish();
    }
}

// ---- Select -----------------------------------------------------------------

void ToolController::selectDown(const PointerInput& input) {
    if (input.pick) editor_.select(Selection{input.pick->objectId, input.pick->faceId});
    else editor_.select(std::nullopt); // clicking empty space clears the selection
}

// ---- Rectangle --------------------------------------------------------------

void ToolController::rectDown(const PointerInput& input) {
    const auto p = groundPoint(input);
    if (!p) return;
    if (!rect_) {
        // A first corner near the horizon lands far outside the model range;
        // refuse it now rather than start a rectangle that can never finish.
        if (auto e = checkRectangleCorner(*p)) {
            editor_.notify(Message{Message::Kind::Error, *e + " Pick a first corner closer to the model."});
            return;
        }
        rect_ = RectGesture{*p, input.screen, true};
        editor_.setPreview(RectanglePreview{*p, *p, std::nullopt});
        editor_.notify(std::nullopt);
    } else {
        rectFinish(input);
    }
}

void ToolController::rectMove(const PointerInput& input) {
    if (!rect_) return;
    const auto p = groundPoint(input);
    if (!p) return;
    const auto r = rectangleFromCorners(rect_->start, *p);
    editor_.setPreview(RectanglePreview{rect_->start, *p, r ? std::optional(*r) : std::nullopt});
}

void ToolController::rectFinish(const PointerInput& input) {
    if (!rect_) return;
    const auto p = groundPoint(input);
    if (!p) return;
    const auto r = rectangleFromCorners(rect_->start, *p);
    if (!r) {
        // Keep the first corner so the user can pick a valid second corner.
        editor_.notify(Message{Message::Kind::Error, r.error()});
        return;
    }
    commitRectangle(*r);
}

bool ToolController::commitRectangle(const RectanglePlacement& placement) {
    const bool ok = editor_.commit(
        "Draw rectangle", [&](const Document& doc) { return createRectangle(doc, placement); },
        [](const OpResult& res) -> std::optional<Selection> {
            if (!res.objectId) return std::nullopt;
            return Selection{*res.objectId, FaceId::profile()};
        });
    if (ok) {
        rect_.reset();
        editor_.cancelPreview();
    }
    return ok;
}

std::string ToolController::lastError(const char* fallback) const {
    const auto& m = editor_.message();
    return m && m->kind == Message::Kind::Error ? m->text : std::string(fallback);
}

std::optional<std::string> ToolController::submitRectangleSize(std::string_view widthText, std::string_view depthText) {
    const LengthUnit unit = editor_.document().displayUnit;
    double w = 0.0, d = 0.0;
    if (trimmed(depthText).empty()) {
        const auto list = parseLengthList(widthText, 2, unit);
        if (!list) return list.error();
        w = (*list)[0];
        d = (*list)[1];
    } else {
        const auto pw = parseLength(widthText, unit);
        if (!pw) return "Width: " + pw.error();
        const auto pd = parseLength(depthText, unit);
        if (!pd) return "Depth: " + pd.error();
        w = *pw;
        d = *pd;
    }
    if (w < 0 || d < 0) return std::string("Width and depth must be positive; the drawing direction follows the pointer.");
    Vec3 start{0.0};
    double sx = 1.0, sy = 1.0;
    if (rect_) {
        start = rect_->start;
        if (const auto* preview = std::get_if<RectanglePreview>(&editor_.preview())) {
            if (preview->end.x < preview->start.x) sx = -1.0;
            if (preview->end.y < preview->start.y) sy = -1.0;
        }
    }
    const auto r = rectangleFromSignedSize(start, sx * w, sy * d);
    if (!r) return r.error();
    if (!commitRectangle(*r)) return lastError("Could not create the rectangle.");
    return std::nullopt;
}

// ---- Push/Pull --------------------------------------------------------------

std::optional<std::string> ToolController::pushableReason(const FaceId& face) {
    switch (face.kind) {
    case FaceKind::Profile:
    case FaceKind::Cap: return std::nullopt;
    case FaceKind::Base:
        return std::string("Push/pull of the base face is not supported yet. Pick the top cap to change the height.");
    case FaceKind::Side:
        return "Push/pull of side faces (" + faceLabel(face) + ") is not supported yet. Pick the top cap to change the height.";
    }
    return std::nullopt;
}

void ToolController::pushDown(const PointerInput& input) {
    if (push_) {
        pushFinish(); // second click of click-move-click
        return;
    }
    if (!input.pick) {
        editor_.select(std::nullopt);
        return;
    }
    const auto& pick = *input.pick;
    editor_.select(Selection{pick.objectId, pick.faceId});
    if (auto reason = pushableReason(pick.faceId)) {
        editor_.notify(Message{Message::Kind::Error, *reason});
        return;
    }
    const auto* o = editor_.document().find(pick.objectId);
    if (!o) return;
    push_ = PushGesture{pick.objectId, pick.faceId.kind, pick.point, o->extrusion ? o->extrusion->height : 0.0,
                        input.screen, true};
    editor_.notify(std::nullopt);
    setPushPreview(0.0);
}

void ToolController::setPushPreview(double delta) {
    if (!push_) return;
    const double height = push_->startHeight + delta;
    editor_.setPreview(PushPullPreview{push_->objectId, push_->face, push_->startHeight, delta, height, checkHeight(height)});
}

void ToolController::pushMove(const PointerInput& input) {
    if (!push_) {
        const auto& pick = input.pick;
        if (pick && !pushableReason(pick->faceId)) editor_.setHover(Selection{pick->objectId, pick->faceId});
        else editor_.setHover(std::nullopt);
        return;
    }
    const auto* o = editor_.document().find(push_->objectId);
    if (!o) return;
    // Along the profile normal, which a rotated object carries with it.
    const auto t = closestParameterOnAxis(push_->anchor, profileNormalWorld(*o), input.ray);
    if (!t) return;
    // Snap the resulting total height to the grid increment.
    double total = push_->startHeight + *t;
    if (snap_.enabled && snap_.step > 0.0) total = snapValue(total, snap_.step);
    setPushPreview(total - push_->startHeight);
}

void ToolController::pushFinish() {
    const auto* preview = std::get_if<PushPullPreview>(&editor_.preview());
    if (!push_ || !preview) return;
    if (std::abs(preview->delta) < 1e-12) {
        // A zero-distance gesture is a no-op, not an edit.
        cancel();
        editor_.notify(Message{Message::Kind::Info, "Push/pull cancelled (zero distance)."});
        return;
    }
    if (preview->error) {
        // Keep the gesture alive so the user can move back to a valid height.
        editor_.notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commitHeight(push_->objectId, push_->face, preview->height);
}

bool ToolController::commitHeight(ObjectId id, FaceKind face, double height) {
    const LengthUnit unit = editor_.document().displayUnit;
    const std::string label = (face == FaceKind::Profile ? "Push/pull to " : "Set height to ") + formatLength(height, unit);
    const bool ok = editor_.commit(
        label,
        [&](const Document& doc) {
            return face == FaceKind::Profile ? extrudeProfile(doc, id, height) : setExtrusionHeight(doc, id, height);
        },
        [id](const OpResult&) -> std::optional<Selection> { return Selection{id, FaceId::cap()}; });
    if (ok) {
        push_.reset();
        editor_.cancelPreview();
    }
    return ok;
}

std::optional<std::string> ToolController::submitPushDistance(std::string_view text) {
    const auto parsed = parseLength(text, editor_.document().displayUnit);
    if (!parsed) return parsed.error();
    ObjectId id;
    FaceKind face = FaceKind::Profile;
    double start = 0.0;
    if (push_) {
        id = push_->objectId;
        face = push_->face;
        start = push_->startHeight;
    } else {
        const auto& sel = editor_.selection();
        const auto* o = sel ? editor_.document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select a rectangle or an extrusion cap first.");
        if (sel->face) {
            if (auto reason = pushableReason(*sel->face)) return reason;
        }
        id = o->id;
        face = o->extrusion ? FaceKind::Cap : FaceKind::Profile;
        start = o->extrusion ? o->extrusion->height : 0.0;
    }
    if (*parsed == 0.0) return std::string("Distance must not be zero.");
    if (auto e = checkHeight(start + *parsed)) return e;
    if (!commitHeight(id, face, start + *parsed)) return lastError("Push/pull failed.");
    return std::nullopt;
}

std::optional<std::string> ToolController::setTotalHeight(ObjectId id, std::string_view text) {
    const auto parsed = parseLength(text, editor_.document().displayUnit);
    if (!parsed) return parsed.error();
    const auto* o = editor_.document().find(id);
    if (!o) return std::string("The object no longer exists.");
    if (auto e = checkHeight(*parsed)) return e;
    if (o->extrusion && std::abs(o->extrusion->height - *parsed) < 1e-12) return std::nullopt; // unchanged: no entry
    if (!commitHeight(id, o->extrusion ? FaceKind::Cap : FaceKind::Profile, *parsed)) return lastError("Could not set the height.");
    return std::nullopt;
}

// ---- Rotate and Scale ---------------------------------------------------------

const ProfileObject* ToolController::transformTarget(const PointerInput& input) {
    const auto& sel = editor_.selection();
    if (const auto* o = sel ? editor_.document().find(sel->objectId) : nullptr) return o;
    // Nothing selected yet: the first click picks the object.
    if (input.pick) {
        editor_.select(Selection{input.pick->objectId, std::nullopt});
        editor_.notify(std::nullopt);
    }
    return nullptr;
}

void ToolController::setRotateAxis(RotateAxis axis) {
    rotateAxis_ = axis;
    if (!rotate_) return;
    // Keep the centre; the old reference direction is not in the new plane.
    rotate_->axis = axisVector(axis);
    rotate_->reference.reset();
    editor_.setPreview(rotatePreview(rotate_->objectId, rotate_->pivot, rotate_->axis));
}

void ToolController::rotateDown(const PointerInput& input) {
    if (!rotate_) {
        const auto* o = transformTarget(input);
        if (!o) return;
        // First click: the centre of rotation, on a face or on the ground.
        std::optional<Vec3> pivot;
        if (input.pick) pivot = input.pick->point;
        else pivot = groundPoint(input);
        if (!pivot) return;
        rotate_ = RotateGesture{o->id, *pivot, axisVector(rotateAxis_), std::nullopt};
        editor_.setPreview(rotatePreview(o->id, *pivot, rotate_->axis));
        editor_.notify(std::nullopt);
        return;
    }
    const auto* preview = std::get_if<RotatePreview>(&editor_.preview());
    if (!rotate_->reference) {
        // Second click: the direction the angle is measured from.
        const auto p = intersectPlane(input.ray, rotate_->pivot, rotate_->axis);
        if (!p || glm::length(*p - rotate_->pivot) < 1e-6) {
            editor_.notify(Message{Message::Kind::Error, "Click a point away from the centre to set the reference direction."});
            return;
        }
        rotate_->reference = *p - rotate_->pivot;
        editor_.setPreview(rotatePreview(rotate_->objectId, rotate_->pivot, rotate_->axis, rotate_->reference, rotate_->reference));
        return;
    }
    // Third click: commit the previewed angle.
    if (!preview) return;
    if (std::abs(preview->angle) < 1e-12) {
        cancel();
        editor_.notify(Message{Message::Kind::Info, "Rotate cancelled (zero angle)."});
        return;
    }
    if (preview->error) {
        editor_.notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commitRotation(rotate_->objectId, rotate_->pivot, rotate_->axis, preview->angle);
}

void ToolController::rotateMove(const PointerInput& input) {
    if (!rotate_) {
        editor_.setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, std::nullopt}) : std::nullopt);
        return;
    }
    const auto p = intersectPlane(input.ray, rotate_->pivot, rotate_->axis);
    if (!p) return;
    RotatePreview preview = rotatePreview(rotate_->objectId, rotate_->pivot, rotate_->axis, rotate_->reference, *p - rotate_->pivot);
    if (rotate_->reference && glm::length(*preview.current) > 1e-9) {
        const Vec3 a = *rotate_->reference;
        const Vec3 b = *preview.current;
        double angle = std::atan2(glm::dot(glm::cross(a, b), rotate_->axis), glm::dot(a, b));
        if (snap_.enabled) angle = snapValue(angle, kRotateSnapRadians);
        preview.angle = angle;
        auto result = rotateObject(editor_.document(), rotate_->objectId, rotate_->pivot, rotate_->axis, angle);
        if (result) {
            if (const auto* o = result->document.find(rotate_->objectId)) preview.result = *o;
        } else {
            preview.error = result.error();
        }
    }
    editor_.setPreview(std::move(preview));
}

bool ToolController::commitRotation(ObjectId id, const Vec3& pivot, const Vec3& axis, double angle) {
    const bool ok = editor_.commit(
        "Rotate " + formatAngle(angle), [&](const Document& doc) { return rotateObject(doc, id, pivot, axis, angle); },
        [id](const OpResult&) -> std::optional<Selection> { return Selection{id, std::nullopt}; });
    if (ok) {
        rotate_.reset();
        editor_.cancelPreview();
    }
    return ok;
}

std::optional<std::string> ToolController::submitRotateAngle(std::string_view text) {
    const auto parsed = parseAngle(text);
    if (!parsed) return parsed.error();
    double angle = *parsed;
    ObjectId id;
    Vec3 pivot{0.0};
    Vec3 axis = axisVector(rotateAxis_);
    if (rotate_) {
        id = rotate_->objectId;
        pivot = rotate_->pivot;
        axis = rotate_->axis;
        // Typed values follow the direction the pointer is turning.
        if (const auto* p = std::get_if<RotatePreview>(&editor_.preview()); p && p->angle < 0.0) angle = -angle;
    } else {
        const auto& sel = editor_.selection();
        const auto* o = sel ? editor_.document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to rotate first.");
        id = o->id;
        pivot = baseCenter(*o);
    }
    if (std::abs(angle) < 1e-12) return std::string("Angle must not be zero.");
    if (!commitRotation(id, pivot, axis, angle)) return lastError("Could not rotate the object.");
    return std::nullopt;
}

void ToolController::scaleDown(const PointerInput& input) {
    if (!scale_) {
        const auto* o = transformTarget(input);
        if (!o) return;
        // First click: the grip point. Its distance from the base centre is 100 %.
        const Vec3 pivot = baseCenter(*o);
        const Vec3 normal = profileNormalWorld(*o);
        const auto p = intersectPlane(input.ray, pivot, normal);
        const double size = glm::length(objectBounds(*o).size());
        if (!p || glm::length(*p - pivot) < size * 1e-3) {
            editor_.notify(Message{Message::Kind::Error, "Click a point away from the object's centre to start scaling."});
            return;
        }
        scale_ = ScaleGesture{o->id, pivot, normal, *p};
        editor_.setPreview(scalePreview(o->id, pivot, *p, *p));
        editor_.notify(std::nullopt);
        return;
    }
    // Second click: commit the previewed factor.
    const auto* preview = std::get_if<ScalePreview>(&editor_.preview());
    if (!preview) return;
    if (std::abs(preview->factor - 1.0) < 1e-12) {
        cancel();
        editor_.notify(Message{Message::Kind::Info, "Scale cancelled (factor 1)."});
        return;
    }
    if (preview->error) {
        editor_.notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commitScale(scale_->objectId, Vec3{preview->factor});
}

void ToolController::scaleMove(const PointerInput& input) {
    if (!scale_) {
        editor_.setHover(input.pick ? std::optional<Selection>(Selection{input.pick->objectId, std::nullopt}) : std::nullopt);
        return;
    }
    const auto p = intersectPlane(input.ray, scale_->pivot, scale_->normal);
    if (!p) return;
    ScalePreview preview = scalePreview(scale_->objectId, scale_->pivot, scale_->start, *p);
    double factor = glm::length(*p - scale_->pivot) / glm::length(scale_->start - scale_->pivot);
    if (snap_.enabled) factor = std::max(kScaleSnapStep, snapValue(factor, kScaleSnapStep));
    preview.factor = factor;
    auto result = scaleObject(editor_.document(), scale_->objectId, Vec3{factor});
    if (result) {
        if (const auto* o = result->document.find(scale_->objectId)) preview.result = *o;
    } else {
        preview.error = result.error();
    }
    editor_.setPreview(std::move(preview));
}

bool ToolController::commitScale(ObjectId id, const Vec3& factors) {
    std::string label = "Scale ";
    const auto pct = [](double f) {
        std::string t = formatLength(f * 100.0, LengthUnit::Meter, false);
        return t + "%";
    };
    if (factors.x == factors.y && factors.y == factors.z) label += pct(factors.x);
    else label += pct(factors.x) + ", " + pct(factors.y) + ", " + pct(factors.z);
    const bool ok = editor_.commit(
        label, [&](const Document& doc) { return scaleObject(doc, id, factors); },
        [id](const OpResult&) -> std::optional<Selection> { return Selection{id, std::nullopt}; });
    if (ok) {
        scale_.reset();
        editor_.cancelPreview();
    }
    return ok;
}

std::optional<std::string> ToolController::submitScaleFactors(std::string_view text) {
    const auto factors = parseScaleFactors(text);
    if (!factors) return factors.error();
    ObjectId id;
    if (scale_) {
        id = scale_->objectId;
    } else {
        const auto& sel = editor_.selection();
        const auto* o = sel ? editor_.document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to scale first.");
        id = o->id;
    }
    const Vec3 f{(*factors)[0], (*factors)[1], (*factors)[2]};
    const auto* o = editor_.document().find(id);
    if (o && f.x == 1.0 && f.y == 1.0 && (f.z == 1.0 || !o->extrusion)) return std::string("A factor of 1 changes nothing.");
    if (!commitScale(id, f)) return lastError("Could not scale the object.");
    return std::nullopt;
}

std::string ToolController::hint() const {
    switch (tool_) {
    case Tool::Select:
        return "Select: click an object to select it, click empty space to clear. Delete removes the selected object.";
    case Tool::Rectangle:
        if (rect_) return "Rectangle: click the opposite corner, or type width, depth (e.g. 2000, 1000) and press Enter. Esc cancels.";
        return "Rectangle: click the first corner on the ground, or type width, depth to draw from the origin.";
    case Tool::PushPull:
        if (push_) return "Push/Pull: move to set the height and click, or type a distance and press Enter. Esc cancels.";
        return "Push/Pull: click a rectangle or an extrusion's top cap, then move up. Side and base faces are not supported yet.";
    case Tool::Rotate: {
        const std::string axis = " Axis " + std::string(axisName(rotateAxis_)) + "; arrow keys change it (Right X, Left Y, Up Z).";
        if (!rotate_) {
            if (!editor_.selection()) return "Rotate: click the object to rotate." + axis;
            return "Rotate: click the centre of rotation, or type an angle in degrees to turn about the base centre." + axis;
        }
        if (!rotate_->reference) return "Rotate: click to set the reference direction, or type an angle. Esc cancels." + axis;
        return "Rotate: move to turn (15\xc2\xb0 steps with grid snap) and click, or type an angle. Esc cancels.";
    }
    case Tool::Scale:
        if (!scale_) {
            if (!editor_.selection()) return "Scale: click the object to scale.";
            return "Scale: click a grip point and move away from or toward the centre, or type a factor (2, 50%) or "
                   "width, depth, height factors (2, 1, 1).";
        }
        return "Scale: move to resize about the base centre (0.1 steps with grid snap) and click, or type a factor. Esc cancels.";
    case Tool::Orbit:
        return "Orbit: drag with the left button to orbit. Right-drag pans, the wheel zooms.";
    }
    return {};
}

} // namespace pf
