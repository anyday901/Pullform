#include "app/tools/push_pull_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

std::optional<std::string> PushPullTool::pushableReason(const FaceId& /*face*/) {
    return std::nullopt; // every planar face; the operation says when a push/pull cannot be made
}

namespace {

/// Whether a face is pushed through the mesh (drawn faces, a box's sides and
/// base, and any face with Ctrl) rather than by changing a profile's height.
bool meshPush(const ProfileObject& o, FaceKind face, bool newFace) {
    if (face == FaceKind::Mesh) return true;
    if (face == FaceKind::Side || face == FaceKind::Base) return true;
    return newFace && face == FaceKind::Cap && o.extrusion.has_value();
}

} // namespace

void PushPullTool::toggleNewFace() {
    newFace_ = !newFace_;
    if (!gesture_) return;
    // The gesture in progress follows the mode at once, preview included.
    const auto* p = std::get_if<PushPullPreview>(&editor().preview());
    const double delta = p ? p->delta : 0.0;
    gesture_->newFace = newFace_;
    if (route()) setPreview(delta);
}

bool PushPullTool::route() {
    const auto* o = editor().document().find(gesture_->objectId);
    if (!o) {
        gesture_.reset();
        return false;
    }
    if (meshPush(*o, gesture_->picked.kind, gesture_->newFace)) {
        const mesh::FaceId meshFace = mesh::faceIdFor(gesture_->picked);
        const auto m = mesh::objectMesh(*o);
        const auto* f = m->face(meshFace);
        if (!f) {
            gesture_.reset();
            return false;
        }
        gesture_->face = FaceKind::Mesh;
        gesture_->meshFace = meshFace.value;
        gesture_->normal = glm::normalize(rotationMatrix(o->transform) * f->normal);
        gesture_->startHeight = 0.0;
        gesture_->baseZ = gesture_->anchor.z;
    } else {
        // A rectangle's face or a box's cap: its profile height changes.
        gesture_->face = gesture_->picked.kind;
        gesture_->startHeight = o->extrusion ? o->extrusion->height : 0.0;
        gesture_->baseZ = profileToWorld(*o, Vec2{0.0}, 0.0).z;
    }
    return true;
}

void PushPullTool::pointerDown(const PointerInput& input) {
    if (gesture_) {
        const auto* preview = std::get_if<PushPullPreview>(&editor().preview());
        if (input.clicks == 2 && lastDistance_ && (!preview || std::abs(preview->delta) < 1e-12)) {
            // Double-click: the same distance as the last push/pull.
            repeatLast();
            return;
        }
        finish(); // second click of click-move-click
        return;
    }
    if (!input.pick) {
        editor().select(std::nullopt);
        return;
    }
    const auto& pick = *input.pick;
    editor().select(Selection{pick.objectId, pick.faceId});
    if (!editor().document().find(pick.objectId)) return;
    gesture_ = Gesture{};
    gesture_->objectId = pick.objectId;
    gesture_->picked = pick.faceId;
    gesture_->anchor = pick.point;
    gesture_->pressScreen = input.screen;
    gesture_->pressed = true;
    gesture_->newFace = newFace_ || input.ctrl;
    if (!route()) return;
    editor().notify(std::nullopt);
    setPreview(0.0);
}

void PushPullTool::repeatLast() {
    if (!gesture_ || !lastDistance_) return;
    const double d = *lastDistance_;
    if (gesture_->face == FaceKind::Mesh) {
        commitMeshFace(gesture_->objectId, gesture_->meshFace, d, gesture_->newFace);
        return;
    }
    const double height = gesture_->startHeight + d;
    const auto error = gesture_->face == FaceKind::Profile ? checkProfileHeight(height) : checkHeight(height);
    if (error) {
        editor().notify(Message{Message::Kind::Error, *error});
        return;
    }
    commitHeight(gesture_->objectId, gesture_->face, height);
}

void PushPullTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    if (isDrag(gesture_->pressScreen, input.screen)) finish();
}

void PushPullTool::setPreview(double delta) {
    if (!gesture_) return;
    if (gesture_->face == FaceKind::Mesh) {
        PushPullPreview p{gesture_->objectId, FaceKind::Mesh, 0.0, delta, delta, std::nullopt, std::nullopt};
        const Document& doc = editor().document();
        if (std::abs(delta) < tol::kMinDimension) {
            if (const auto* o = doc.find(gesture_->objectId)) p.result = *o; // nothing pushed yet
        } else if (auto r = pushPullFace(doc, gesture_->objectId, mesh::FaceId{gesture_->meshFace}, delta, nullptr, gesture_->newFace)) {
            p.result = *r->document.find(gesture_->objectId);
        } else {
            p.error = r.error();
        }
        editor().setPreview(std::move(p));
        return;
    }
    const double height = gesture_->startHeight + delta;
    // A planar face may be pushed backwards (into a wall); a cap stays above its base.
    const auto error = gesture_->face == FaceKind::Profile ? checkProfileHeight(height) : checkHeight(height);
    editor().setPreview(PushPullPreview{gesture_->objectId, gesture_->face, gesture_->startHeight, delta, height, error, std::nullopt});
}

void PushPullTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        if (const auto& pick = input.pick; pick && !pushableReason(pick->faceId)) editor().setHover(Selection{pick->objectId, pick->faceId});
        else editor().setHover(std::nullopt);
        return;
    }
    if (gesture_->face == FaceKind::Mesh) {
        moveMeshFace(input);
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

void PushPullTool::moveMeshFace(const PointerInput& input) {
    const Vec3 n = gesture_->normal;
    const auto t = closestParameterOnAxis(gesture_->anchor, n, input.ray);
    if (std::abs(std::abs(n.z) - 1.0) > 1e-9) {
        // A tilted or vertical face: the distance snaps to the grid only.
        ctx_.indicator.reset();
        if (t.has_value()) setPreview(ctx_.snap.toGrid(*t));
        return;
    }
    // A horizontal face: heights snap like a cap (snapHeight works in world z
    // from the picked point, so a face looking down pushes the other way).
    const double sign = n.z > 0.0 ? 1.0 : -1.0;
    const auto rise = snapHeight(input, t.has_value() ? std::optional(*t * sign) : std::nullopt);
    if (rise.has_value()) setPreview(*rise * sign);
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
    if (gesture_->face == FaceKind::Mesh) {
        commitMeshFace(gesture_->objectId, gesture_->meshFace, preview->delta, gesture_->newFace);
        return;
    }
    commitHeight(gesture_->objectId, gesture_->face, preview->height);
}

bool PushPullTool::commitMeshFace(ObjectId id, std::uint32_t face, double distance, bool newFace) {
    const LengthUnit unit = editor().document().displayUnit;
    mesh::FaceId pushed{face};
    const bool ok = editor().commit(
        "Push/pull " + formatLength(distance, unit),
        [id, face, distance, newFace, &pushed](const Document& doc) {
            return pushPullFace(doc, id, mesh::FaceId{face}, distance, &pushed, newFace);
        },
        [id, &pushed](const OpResult&) -> std::optional<Selection> {
            if (pushed.value == 0) return Selection{id, std::nullopt}; // cut through: the face is gone
            return Selection{id, FaceId::meshFace(pushed.value)};
        });
    if (ok) {
        lastDistance_ = distance;
        gesture_.reset();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
    return ok;
}

bool PushPullTool::commitHeight(ObjectId id, FaceKind face, double height) {
    const LengthUnit unit = editor().document().displayUnit;
    const auto* current = editor().document().find(id);
    const double before = current && current->extrusion ? current->extrusion->height : 0.0;
    const std::string label = (face == FaceKind::Profile ? "Push/pull to " : "Set height to ") + formatLength(height, unit);
    const bool ok = editor().commit(
        label,
        [id, face, height](const Document& doc) {
            return face == FaceKind::Profile ? extrudeProfile(doc, id, height) : setExtrusionHeight(doc, id, height);
        },
        [id](const OpResult&) -> std::optional<Selection> { return Selection{id, FaceId::cap()}; });
    if (ok) {
        lastDistance_ = height - before;
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
    if (gesture_ && gesture_->face == FaceKind::Mesh) {
        if (std::abs(*parsed) < tol::kMinDimension) return std::string("Distance must be at least 0.1 mm.");
        if (!commitMeshFace(gesture_->objectId, gesture_->meshFace, *parsed, gesture_->newFace)) return ctx_.lastError("Push/pull failed.");
        return std::nullopt;
    }
    if (const auto& sel = editor().selection(); !gesture_ && sel && sel->face) {
        // A selected face pushed through the mesh: a drawn face, a box's side or base, or Ctrl.
        const auto* o = editor().document().find(sel->objectId);
        if (o && meshPush(*o, sel->face->kind, newFace_)) {
            if (std::abs(*parsed) < tol::kMinDimension) return std::string("Distance must be at least 0.1 mm.");
            if (!commitMeshFace(sel->objectId, mesh::faceIdFor(*sel->face).value, *parsed, newFace_)) {
                return ctx_.lastError("Push/pull failed.");
            }
            return std::nullopt;
        }
    }
    if (gesture_) {
        id = gesture_->objectId;
        face = gesture_->face;
        start = gesture_->startHeight;
    } else {
        const auto& sel = editor().selection();
        const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
        if (!o) return std::string("Select a rectangle, circle or polygon, or an extrusion cap first.");
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
#ifdef __APPLE__
    constexpr const char* key = "Option";
#else
    constexpr const char* key = "Ctrl";
#endif
    const std::string mode = newFace_ ? std::string(" New starting face (") + key + " to stop)."
                                      : std::string(" ") + key + " makes a new starting face.";
    if (gesture_) {
        return "Push/Pull: move to set the distance and click, or type a distance and press Enter. Double-click repeats the "
               "last distance. Esc cancels." + mode;
    }
    return "Push/Pull: click any face (a box's top, side or base, a face drawn on a wall, a rectangle) and move. Push a face "
           "drawn on a wall all the way through to cut an opening." + mode;
}

} // namespace pf
