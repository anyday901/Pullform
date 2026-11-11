#include "app/tools/offset_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/offset.hpp"
#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <cmath>

namespace pf {

namespace {

Vec3 toLocal(const ProfileObject& o, const Vec3& world) {
    return glm::transpose(rotationMatrix(o.transform)) * (world - o.transform.translation);
}

/// Where the pointer ray meets the plane of face `face` (world space).
std::optional<Vec3> onFacePlane(const ProfileObject& o, const mesh::Mesh& m, std::uint32_t face, const Ray& ray) {
    const auto* f = m.face(mesh::FaceId{face});
    if (!f) return std::nullopt;
    const Vec3 point = localToWorld(o, m.loopPositions(f->loops[0]).front());
    const Vec3 normal = rotationMatrix(o.transform) * f->normal;
    return intersectPlane(ray, point, normal);
}

/// Projects a world point onto the plane of face `face`.
Vec3 projectToFace(const ProfileObject& o, const mesh::Mesh& m, std::uint32_t face, const Vec3& world) {
    const auto* f = m.face(mesh::FaceId{face});
    if (!f) return world;
    const Vec3 point = localToWorld(o, m.loopPositions(f->loops[0]).front());
    const Vec3 normal = rotationMatrix(o.transform) * f->normal;
    return world - normal * glm::dot(world - point, normal);
}

} // namespace

std::optional<std::pair<ObjectId, std::uint32_t>> OffsetTool::faceUnder(const PointerInput& input) const {
    if (!input.pick) return std::nullopt;
    const auto* o = editor().document().find(input.pick->objectId);
    if (!o) return std::nullopt;
    const mesh::FaceId face = mesh::faceIdFor(input.pick->faceId);
    if (!mesh::objectMesh(*o)->face(face)) return std::nullopt;
    return std::pair{o->id, face.value};
}

void OffsetTool::pointerDown(const PointerInput& input) {
    if (gesture_ && input.clicks == 2 && last_) {
        // The second press of a double-click: its first press started a
        // gesture that has not moved yet. Drop it and repeat the last offset.
        const auto* preview = std::get_if<OffsetPreview>(&editor().preview());
        const auto under = faceUnder(input);
        if (under && (!preview || std::abs(preview->distance) < tol::kMinDimension)) {
            abandon();
            commit(under->first, under->second, *last_);
            return;
        }
    }
    if (gesture_) {
        finish(); // second click of click-move-click
        return;
    }
    const auto under = faceUnder(input);
    if (!under) {
        editor().select(std::nullopt);
        return;
    }
    const auto [id, face] = *under;
    if (input.clicks == 2 && last_) {
        // Double-click: the last offset again, on this face.
        commit(id, face, *last_);
        return;
    }
    editor().select(Selection{id, input.pick->faceId});
    const auto* o = editor().document().find(id);
    const auto m = mesh::objectMesh(*o);
    const Vec3 anchor = projectToFace(*o, *m, face, input.pick->point);
    gesture_ = Gesture{id, face, anchor, mesh::signedBoundaryDistance(*m, mesh::FaceId{face}, toLocal(*o, anchor)), input.screen, true};
    editor().notify(std::nullopt);
    setPreview(0.0);
}

void OffsetTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    if (isDrag(gesture_->pressScreen, input.screen)) finish();
}

void OffsetTool::pointerMove(const PointerInput& input) {
    if (!gesture_) {
        if (const auto under = faceUnder(input)) editor().setHover(Selection{under->first, input.pick->faceId});
        else editor().setHover(std::nullopt);
        return;
    }
    const auto* o = editor().document().find(gesture_->objectId);
    if (!o) return;
    const auto m = mesh::objectMesh(*o);
    // A corner or midpoint under the pointer sets the distance exactly
    // (projected onto the face's plane); otherwise the distance snaps to the grid.
    std::optional<Vec3> at;
    if (auto hit = ctx_.snapper.point(input)) {
        at = projectToFace(*o, *m, gesture_->face, hit->point);
        hit->point = *at;
        ctx_.indicator = *hit;
    } else {
        ctx_.indicator.reset();
        at = onFacePlane(*o, *m, gesture_->face, input.ray);
    }
    if (!at) return;
    double d = mesh::signedBoundaryDistance(*m, mesh::FaceId{gesture_->face}, toLocal(*o, *at)) - gesture_->startDistance;
    if (!ctx_.indicator) d = ctx_.snap.toGrid(d);
    setPreview(d);
}

void OffsetTool::setPreview(double distance) {
    if (!gesture_) return;
    OffsetPreview p{gesture_->objectId, gesture_->face, distance, {}, std::nullopt};
    const auto* o = editor().document().find(gesture_->objectId);
    if (!o) return;
    if (std::abs(distance) >= tol::kMinDimension) {
        const auto loops = mesh::offsetLoops(*mesh::objectMesh(*o), mesh::FaceId{gesture_->face}, distance);
        if (loops) {
            for (const auto& loop : *loops) {
                std::vector<Vec3> world;
                for (const Vec3& q : loop.points) world.push_back(localToWorld(*o, q));
                p.loops.push_back(std::move(world));
            }
        } else {
            p.error = loops.error();
        }
    }
    editor().setPreview(std::move(p));
}

void OffsetTool::finish() {
    const auto* preview = std::get_if<OffsetPreview>(&editor().preview());
    if (!gesture_ || !preview) return;
    if (std::abs(preview->distance) < tol::kMinDimension) {
        // A zero-distance gesture is a no-op, not an edit.
        abandon();
        editor().notify(Message{Message::Kind::Info, "Offset cancelled (zero distance)."});
        return;
    }
    if (preview->error) {
        // Keep the gesture alive so the user can move back to a distance that fits.
        editor().notify(Message{Message::Kind::Error, *preview->error});
        return;
    }
    commit(gesture_->objectId, gesture_->face, preview->distance);
}

bool OffsetTool::commit(ObjectId id, std::uint32_t face, double distance) {
    const LengthUnit unit = editor().document().displayUnit;
    mesh::FaceId ring{face};
    const bool ok = editor().commit(
        "Offset " + formatLength(distance, unit),
        [id, face, distance, &ring](const Document& doc) { return offsetFace(doc, id, mesh::FaceId{face}, distance, &ring); },
        [id, &ring](const OpResult&) -> std::optional<Selection> { return Selection{id, FaceId::meshFace(ring.value)}; });
    if (ok) {
        last_ = distance;
        gesture_.reset();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> OffsetTool::submit(std::string_view text) {
    const auto parsed = parseLength(text, editor().document().displayUnit);
    if (!parsed) return parsed.error();
    if (std::abs(*parsed) < tol::kMinDimension) return std::string("Offset distance must be at least 0.1 mm.");
    if (gesture_) {
        // The typed length goes the way the pointer shows; negative turns it round.
        const auto* preview = std::get_if<OffsetPreview>(&editor().preview());
        const double sign = preview && preview->distance < 0.0 ? -1.0 : 1.0;
        if (!commit(gesture_->objectId, gesture_->face, *parsed * sign)) return ctx_.lastError("Offset failed.");
        return std::nullopt;
    }
    const auto& sel = editor().selection();
    const auto* o = sel ? editor().document().find(sel->objectId) : nullptr;
    if (!o || !sel->face) return std::string("Click a face to offset first.");
    const mesh::FaceId face = mesh::faceIdFor(*sel->face);
    if (!mesh::objectMesh(*o)->face(face)) return std::string("Click a face to offset first.");
    if (!commit(o->id, face.value, *parsed)) return ctx_.lastError("Offset failed.");
    return std::nullopt;
}

std::string OffsetTool::hint() const {
    if (gesture_) {
        return "Offset: move into the face (inward) or out of it and click, or type a distance and press Enter. Esc cancels.";
    }
    return "Offset: click a face, then move to set the distance. Double-click a face to repeat the last offset.";
}

} // namespace pf
