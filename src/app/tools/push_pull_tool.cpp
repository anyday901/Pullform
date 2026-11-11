#include "app/tools/push_pull_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/geometry/pick.hpp"

#include <cmath>

namespace pf {

std::optional<std::string> PushPullTool::pushableReason(const FaceId& face) {
    using enum FaceKind;
    switch (face.kind) {
    case Profile:
    case Cap: return std::nullopt;
    case Base:
        return std::string("Push/pull of the base face is not supported yet. Pick the top cap to change the height.");
    case Side:
        return "Push/pull of side faces (" + faceLabel(face) + ") is not supported yet. Pick the top cap to change the height.";
    case Mesh: return std::string("Push/pull of drawn faces is not supported yet.");
    }
    return std::nullopt;
}

void PushPullTool::pointerDown(const PointerInput& input) {
    if (gesture_) {
        finish(); // second click of click-move-click
        return;
    }
    if (!input.pick) {
        editor().select(std::nullopt);
        return;
    }
    const auto& pick = *input.pick;
    editor().select(Selection{pick.objectId, pick.faceId});
    if (auto reason = pushableReason(pick.faceId)) {
        editor().notify(Message{Message::Kind::Error, *reason});
        return;
    }
    const auto* o = editor().document().find(pick.objectId);
    if (!o) return;
    gesture_ = Gesture{pick.objectId, pick.faceId.kind, pick.point, o->extrusion ? o->extrusion->height : 0.0,
                       profileToWorld(*o, Vec2{0.0}, 0.0).z, input.screen, true};
    editor().notify(std::nullopt);
    setPreview(0.0);
}

void PushPullTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    if (isDrag(gesture_->pressScreen, input.screen)) finish();
}

void PushPullTool::setPreview(double delta) {
    if (!gesture_) return;
    const double height = gesture_->startHeight + delta;
    // A planar face may be pushed backwards (into a wall); a cap stays above its base.
    const auto error = gesture_->face == FaceKind::Profile ? checkProfileHeight(height) : checkHeight(height);
    editor().setPreview(PushPullPreview{gesture_->objectId, gesture_->face, gesture_->startHeight, delta, height, error});
}

void PushPullTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        if (const auto& pick = input.pick; pick && !pushableReason(pick->faceId)) editor().setHover(Selection{pick->objectId, pick->faceId});
        else editor().setHover(std::nullopt);
        return;
    }
    const auto* o = editor().document().find(gesture_->objectId);
    if (!o) return;
    // Along the profile normal, which a rotated object carries with it. The
    // pointer ray may be parallel to it (looking straight down); a corner under
    // the pointer still gives a height then.
    const Vec3 normal = profileNormalWorld(*o);
    const auto t = closestParameterOnAxis(gesture_->anchor, normal, input.ray);
    const auto raw = t.has_value() ? std::optional(gesture_->startHeight + *t) : std::nullopt;
    if (normal != Vec3{0.0, 0.0, 1.0}) {
        // Point and height snaps compare world heights, so a tipped object
        // snaps its height to the grid only.
        ctx_.indicator.reset();
        if (raw.has_value()) setPreview(ctx_.snap.toGrid(*raw) - gesture_->startHeight);
        return;
    }
    const auto total = snapHeight(input, raw);
    if (total.has_value()) setPreview(*total - gesture_->startHeight);
}

std::optional<double> PushPullTool::snapHeight(const PointerInput& input, std::optional<double> raw) {
    const auto at = [this](double h) { return Vec3{gesture_->anchor.x, gesture_->anchor.y, gesture_->baseZ + h}; };
    // 1. The pointer is on a corner or edge midpoint of another object: take
    //    its height. 2. Otherwise the height lines up with another object's
    //    corner (for example its cap). Both draw a guide from that corner.
    auto hit = ctx_.snapper.point(input, gesture_->objectId);
    if (!hit && raw.has_value()) hit = ctx_.snapper.height(input, at(*raw), gesture_->objectId);
    if (hit) {
        const double h = hit->featurePoint.z - gesture_->baseZ;
        hit->point = at(h);
        hit->guides.emplace_back(hit->featurePoint, hit->point, -1);
        ctx_.indicator = *hit;
        return h;
    }
    if (!raw.has_value()) return std::nullopt;
    // 3. Grid: snap the total height to the increment.
    const double total = ctx_.snap.toGrid(*raw);
    ctx_.indicator = SnapResult{at(total), ctx_.snap.gridOn() ? SnapKind::Grid : SnapKind::None, std::nullopt, -1, at(total), {}};
    return total;
}

void PushPullTool::finish() {
    const auto* preview = std::get_if<PushPullPreview>(&editor().preview());
    if (!gesture_ || !preview) return;
    if (std::abs(preview->delta) < 1e-12) {
        // A zero-distance gesture is a no-op, not an edit.
        abandon();
        editor().notify(Message{Message::Kind::Info, "Push/pull cancelled (zero distance)."});
        return;
    }
    if (preview->error) {
        // Keep the gesture alive so the user can move back to a valid height.
        editor().notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commitHeight(gesture_->objectId, gesture_->face, preview->height);
}

bool PushPullTool::commitHeight(ObjectId id, FaceKind face, double height) {
    const LengthUnit unit = editor().document().displayUnit;
    const std::string label = (face == FaceKind::Profile ? "Push/pull to " : "Set height to ") + formatLength(height, unit);
    const bool ok = editor().commit(
        label,
        [id, face, height](const Document& doc) {
            return face == FaceKind::Profile ? extrudeProfile(doc, id, height) : setExtrusionHeight(doc, id, height);
        },
        [id](const OpResult&) -> std::optional<Selection> { return Selection{id, FaceId::cap()}; });
    if (ok) {
        gesture_.reset();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> PushPullTool::submitDistance(std::string_view text) {
    const auto parsed = parseLength(text, editor().document().displayUnit);
    if (!parsed) return parsed.error();
    ObjectId id;
    FaceKind face = FaceKind::Profile;
    double start = 0.0;
    if (gesture_) {
        id = gesture_->objectId;
        face = gesture_->face;
        start = gesture_->startHeight;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select a rectangle or an extrusion cap first.");
        if (sel->face) {
            if (auto reason = pushableReason(*sel->face)) return reason;
        }
        id = o->id;
        face = o->extrusion ? FaceKind::Cap : FaceKind::Profile;
        start = o->extrusion ? o->extrusion->height : 0.0;
    }
    if (*parsed == 0.0) return std::string("Distance must not be zero.");
    if (auto e = face == FaceKind::Profile ? checkProfileHeight(start + *parsed) : checkHeight(start + *parsed)) return e;
    if (!commitHeight(id, face, start + *parsed)) return ctx_.lastError("Push/pull failed.");
    return std::nullopt;
}

std::optional<std::string> PushPullTool::setTotalHeight(ObjectId id, std::string_view text) {
    const auto parsed = parseLength(text, editor().document().displayUnit);
    if (!parsed) return parsed.error();
    const auto* o = editor().document().find(id);
    if (!o) return std::string("The object no longer exists.");
    if (auto e = checkHeight(*parsed)) return e;
    if (o->extrusion && std::abs(o->extrusion->height - *parsed) < 1e-12) return std::nullopt; // unchanged: no entry
    if (!commitHeight(id, o->extrusion ? FaceKind::Cap : FaceKind::Profile, *parsed)) return ctx_.lastError("Could not set the height.");
    return std::nullopt;
}

std::string PushPullTool::hint() const {
    if (gesture_) return "Push/Pull: move to set the height and click, or type a distance and press Enter. Esc cancels.";
    return "Push/Pull: click a rectangle and move either way, or an extrusion's top cap. Side and base faces are not supported yet.";
}

} // namespace pf
