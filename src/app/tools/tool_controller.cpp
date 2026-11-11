#include "app/tools/tool_controller.hpp"

#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pf {

namespace {


/// The axis a move runs along when exactly one component is non-zero.
std::optional<MoveAxis> singleAxis(const Vec3& offset) {
    const bool x = offset.x != 0.0;
    const bool y = offset.y != 0.0;
    const bool z = offset.z != 0.0;
    if (x && !y && !z) return MoveAxis::X;
    if (y && !x && !z) return MoveAxis::Y;
    if (z && !x && !y) return MoveAxis::Z;
    return std::nullopt;
}

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
    case Tool::Move: return "Move";
    case Tool::Orbit: return "Orbit";
    }
    return "Select";
}

void ToolController::setTool(Tool tool) {
    if (tool == tool_) return;
    cancel();
    axisLock_.reset();
    moveCopy_ = false;
    tool_ = tool;
    indicator_.reset();
    snapper_.clearReferences();
    editor_.setHover(std::nullopt);
    editor_.notify(std::nullopt);
}

bool ToolController::cancel() {
    const bool had = active() || !std::holds_alternative<std::monostate>(editor_.preview());
    rect_.reset();
    push_.reset();
    move_.reset();
    indicator_.reset();
    editor_.cancelPreview();
    return had;
}

std::optional<SnapResult> ToolController::snapGround(const PointerInput& input) {
    return snapper_.ground(input, rect_ ? std::optional(rect_->start) : std::nullopt);
}

SnapResult ToolController::snapPointer(const PointerInput& input) {
    if (auto hit = snapper_.point(input)) return *hit;
    if (input.pick) {
        const Vec3 p = input.pick->point;
        return SnapResult{p, SnapKind::OnFace, input.pick->objectId, -1, p, {}};
    }
    return snapGround(input).value_or(SnapResult{input.ray.origin, SnapKind::None, std::nullopt, -1, input.ray.origin, {}});
}

std::optional<Vec3> ToolController::groundPoint(const PointerInput& input) {
    auto res = snapGround(input);
    indicator_ = res;
    if (!res) return std::nullopt;
    return res->point;
}

void ToolController::pointerDown(const PointerInput& input) {
    switch (tool_) {
    case Tool::Select: selectDown(input); break;
    case Tool::Rectangle: rectDown(input); break;
    case Tool::PushPull: pushDown(input); break;
    case Tool::Move: moveDown(input); break;
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
    case Tool::Move: moveMove(input); break;
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
    } else if (move_ && move_->pressed) {
        move_->pressed = false;
        if (moved(move_->pressScreen)) {
            updateMovePreview(input.ray); // release position, not the last move event
            moveFinish();
        }
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
    const auto p = groundPoint(input); // also shows the snap before the first corner
    if (!p || !rect_) return;
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
        indicator_.reset();
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
                        o->transform.translation.z + o->profile.plane.origin.z, input.screen, true};
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
    // The pointer ray may be parallel to the push axis (looking straight down);
    // a corner under the pointer still gives a height then.
    const auto t = closestParameterOnAxis(push_->anchor, {0.0, 0.0, 1.0}, input.ray);
    const auto total = snapPushHeight(input, t.has_value() ? std::optional(push_->startHeight + *t) : std::nullopt);
    if (total.has_value()) setPushPreview(*total - push_->startHeight);
}

std::optional<double> ToolController::snapPushHeight(const PointerInput& input, std::optional<double> raw) {
    const auto at = [this](double h) { return Vec3{push_->anchor.x, push_->anchor.y, push_->baseZ + h}; };
    // 1. The pointer is on a corner or edge midpoint of another object: take
    //    its height. 2. Otherwise the height lines up with another object's
    //    corner (for example its cap). Both draw a guide from that corner.
    auto hit = snapper_.point(input, push_->objectId);
    if (!hit && raw.has_value()) hit = snapper_.height(input, at(*raw), push_->objectId);
    if (hit) {
        const double h = hit->featurePoint.z - push_->baseZ;
        hit->point = at(h);
        hit->guides.emplace_back(hit->featurePoint, hit->point, -1);
        indicator_ = *hit;
        return h;
    }
    if (!raw.has_value()) return std::nullopt;
    // 3. Grid: snap the total height to the increment.
    const double total = snap_.toGrid(*raw);
    indicator_ = SnapResult{at(total), snap_.gridOn() ? SnapKind::Grid : SnapKind::None, std::nullopt, -1, at(total), {}};
    return total;
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
        indicator_.reset();
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

// ---- Move -------------------------------------------------------------------

void ToolController::toggleAxisLock(MoveAxis axis) {
    if (axisLock_ == axis) axisLock_.reset();
    else axisLock_ = axis;
    if (move_ && move_->lastRay) updateMovePreview(*move_->lastRay);
    else if (move_) {
        if (const auto* p = std::get_if<MovePreview>(&editor_.preview())) {
            MovePreview next = *p;
            next.axis = axisLock_;
            editor_.setPreview(next);
        }
    }
}

void ToolController::toggleMoveCopy() {
    moveCopy_ = !moveCopy_;
    if (const auto* p = std::get_if<MovePreview>(&editor_.preview())) {
        MovePreview next = *p;
        next.copy = moveCopy_;
        editor_.setPreview(next);
    }
}

void ToolController::moveDown(const PointerInput& input) {
    if (move_) {
        // Second click of click-move-click. The pointer may have moved in the
        // same frame before this click, so evaluate the clicked position first.
        updateMovePreview(input.ray);
        moveFinish();
        return;
    }
    if (!input.pick) {
        editor_.select(std::nullopt);
        return;
    }
    const auto& pick = *input.pick;
    const auto* o = editor_.document().find(pick.objectId);
    if (!o) return;
    editor_.select(Selection{pick.objectId, pick.faceId});
    move_ = MoveGesture{pick.objectId, pick.point, o->transform.translation, input.screen, true, std::nullopt};
    editor_.notify(std::nullopt);
    editor_.setPreview(MovePreview{pick.objectId, pick.point, Vec3{0.0}, axisLock_, std::nullopt, moveCopy_});
}

std::optional<Vec3> ToolController::moveOffset(const Ray& ray) const {
    if (!move_) return std::nullopt;
    const bool snapping = snap_.enabled && snap_.step > 0.0;
    // Snapping places the object's corner (its translation) on the grid, the
    // same points rectangle corners snap to.
    const double step = snap_.step;
    const auto snapped = [snapping, step](double start, double delta) {
        return snapping ? snapValue(start + delta, step) - start : delta;
    };
    const Vec3& start = move_->startTranslation;
    if (axisLock_) {
        const Vec3 axis = axisVector(*axisLock_);
        const auto t = closestParameterOnAxis(move_->anchor, axis, ray);
        if (!t.has_value()) return std::nullopt;
        // `axis` is a unit world axis, so the dot product is exactly that component.
        return axis * snapped(glm::dot(start, axis), *t);
    }
    // Free move: slide in the horizontal plane through the picked point.
    const auto p = intersectHorizontalPlane(ray, move_->anchor.z);
    if (!p) return std::nullopt;
    const Vec3 d = *p - move_->anchor;
    return Vec3{snapped(start.x, d.x), snapped(start.y, d.y), 0.0};
}

void ToolController::updateMovePreview(const Ray& ray) {
    if (!move_) return;
    move_->lastRay = ray;
    const auto* o = editor_.document().find(move_->objectId);
    if (!o) return;
    auto offset = moveOffset(ray);
    if (!offset) {
        // The ray cannot be resolved under the current constraint (parallel to
        // the locked axis, or no forward hit on the horizontal plane). Never keep
        // an offset from the previous constraint: project it onto this one.
        const auto* previous = std::get_if<MovePreview>(&editor_.preview());
        const Vec3 last = previous ? previous->offset : Vec3{0.0};
        if (axisLock_) {
            const Vec3 axis = axisVector(*axisLock_);
            offset = axis * glm::dot(last, axis);
        } else {
            offset = Vec3{last.x, last.y, 0.0};
        }
    }
    editor_.setPreview(MovePreview{move_->objectId, move_->anchor, *offset, axisLock_, checkMove(*o, *offset), moveCopy_});
}

void ToolController::moveMove(const PointerInput& input) {
    if (!move_) {
        const auto& pick = input.pick;
        editor_.setHover(pick ? std::optional<Selection>(Selection{pick->objectId, pick->faceId}) : std::nullopt);
        return;
    }
    updateMovePreview(input.ray);
}

void ToolController::moveFinish() {
    const auto* preview = std::get_if<MovePreview>(&editor_.preview());
    if (!move_ || !preview) return;
    const Vec3 offset = preview->offset;
    if (glm::length(offset) < tol::kLengthEpsilon) {
        // A zero-distance gesture is a no-op, not an edit.
        cancel();
        editor_.notify(Message{Message::Kind::Info, "Move cancelled (zero distance)."});
        return;
    }
    if (preview->error) {
        // Keep the gesture alive so the user can move back into range.
        editor_.notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commitMove(move_->objectId, offset);
}

bool ToolController::commitMove(ObjectId id, const Vec3& offset) {
    const LengthUnit unit = editor_.document().displayUnit;
    const bool copy = moveCopy_;
    std::string label = (copy ? "Copy " : "Move ") + formatLength(glm::length(offset), unit);
    if (const auto axis = singleAxis(offset)) label += std::string(" along ") + axisName(*axis);
    bool ok = false;
    if (copy) {
        // The copy takes over the selection (with the same face) so it can be
        // moved or copied again right away.
        const auto face = editor_.selection() && editor_.selection()->objectId == id ? editor_.selection()->face
                                                                                    : std::optional<FaceId>{};
        ok = editor_.commit(
            label, [id, &offset](const Document& doc) { return duplicateObject(doc, id, offset); },
            [face](const OpResult& r) -> std::optional<Selection> {
                if (!r.objectId) return std::nullopt;
                return Selection{*r.objectId, face};
            });
    } else {
        ok = editor_.commit(label, [id, &offset](const Document& doc) { return moveObject(doc, id, offset); });
    }
    if (ok) {
        move_.reset();
        axisLock_.reset();
        moveCopy_ = false;
        editor_.cancelPreview();
    }
    return ok;
}

std::optional<std::string> ToolController::submitMoveDistance(std::string_view text) {
    const LengthUnit unit = editor_.document().displayUnit;
    ObjectId id;
    if (move_) {
        id = move_->objectId;
    } else {
        const auto& sel = editor_.selection();
        const auto* o = sel ? editor_.document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select an object to move first.");
        id = o->id;
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
        } else if (const auto* p = std::get_if<MovePreview>(&editor_.preview()); move_ && p && glm::length(p->offset) > 0.0) {
            direction = glm::normalize(p->offset);
        } else {
            return std::string("Lock an axis first (X, Y or Z, or the arrow keys), move the pointer to set a direction, "
                               "or type x, y, z.");
        }
        offset = direction * *d;
    }
    if (glm::length(offset) < tol::kLengthEpsilon) return std::string("Distance must not be zero.");
    const auto* o = editor_.document().find(id);
    if (!o) return std::string("The object no longer exists.");
    if (auto e = checkMove(*o, offset)) return e;
    if (!commitMove(id, offset)) return lastError("Move failed.");
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

std::string ToolController::hint() const {
    std::string text = toolHint();
    if (indicator_ && indicator_->kind >= SnapKind::OnFace && (tool_ == Tool::Rectangle || tool_ == Tool::PushPull)) {
        text = std::string("[") + snapKindName(indicator_->kind) + "] " + text;
    }
    return text;
}

std::string ToolController::toolHint() const {
    switch (tool_) {
    case Tool::Select:
        return "Select: click an object to select it, click empty space to clear. Delete removes the selected object.";
    case Tool::Rectangle:
        if (rect_) return "Rectangle: click the opposite corner, or type width, depth (e.g. 2000, 1000) and press Enter. Esc cancels.";
        return "Rectangle: click the first corner on the ground, or type width, depth to draw from the origin.";
    case Tool::PushPull:
        if (push_) return "Push/Pull: move to set the height and click, or type a distance and press Enter. Esc cancels.";
        return "Push/Pull: click a rectangle or an extrusion's top cap, then move up. Side and base faces are not supported yet.";
    case Tool::Move: {
        std::string lock = axisLock_ ? std::string(" Locked to ") + axisName(*axisLock_) + "." : std::string();
#ifdef __APPLE__
        constexpr const char* copyKey = "Option";
#else
        constexpr const char* copyKey = "Ctrl";
#endif
        lock += moveCopy_ ? std::string(" Copying (") + copyKey + " to stop)." : std::string(" ") + copyKey + " makes a copy.";
        if (move_) {
            return "Move: click the destination, or type a distance and press Enter. X, Y, Z or the arrow keys lock an "
                   "axis. Esc cancels." + lock;
        }
        return "Move: click an object and move it, or select one and type x, y, z. X, Y, Z or the arrow keys lock an "
               "axis." + lock;
    }
    case Tool::Orbit:
        return "Orbit: drag with the left button to orbit. Right-drag pans, the wheel zooms.";
    }
    return {};
}

std::optional<std::string> ToolController::duplicateSelection() {
    const auto selection = editor_.selection();
    if (!selection) return std::string{"Select an object to duplicate."};
    const auto* o = editor_.document().find(selection->objectId);
    if (!o) return std::string{"The selected object no longer exists."};
    cancel();
    const Vec3 offset{objectBounds(*o).size().x, 0.0, 0.0};
    const auto face = selection->face;
    const ObjectId id = selection->objectId;
    if (const bool ok = editor_.commit(
            "Duplicate " + idString(id), [id, offset](const Document& d) { return duplicateObject(d, id, offset); },
            [face](const OpResult& r) -> std::optional<Selection> {
                if (!r.objectId) return std::nullopt;
                return Selection{*r.objectId, face};
            });
        !ok) {
        return lastError("Could not duplicate the object.");
    }
    if (const auto* copy = editor_.selection() ? editor_.document().find(editor_.selection()->objectId) : nullptr)
        editor_.notify(Message{Message::Kind::Info, "Duplicated as " + copy->name + "."});
    return std::nullopt;
}

} // namespace pf
