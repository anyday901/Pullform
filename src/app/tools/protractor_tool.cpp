#include "app/tools/protractor_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

bool isPointSnap(SnapKind kind) {
    return kind == SnapKind::Endpoint || kind == SnapKind::Midpoint || kind == SnapKind::GuidePoint ||
           kind == SnapKind::GuideCrossing;
}

} // namespace

Vec3 protractorDirection(const Vec3& reference, const Vec3& normal, double angle) {
    // Rodrigues' rotation; the reference lies in the plane, so the axial term drops.
    const Vec3 r = glm::normalize(reference - normal * glm::dot(reference, normal));
    return glm::normalize(r * std::cos(angle) + glm::cross(normal, r) * std::sin(angle));
}

bool ProtractorTool::key(ToolKey key) {
    const auto axis = arrowAxis(key);
    if (!axis) return false;
    toggleAxis(*axis);
    return true;
}

void ProtractorTool::toggleAxis(MoveAxis axis) {
    axis_ = axis_ == axis ? std::nullopt : std::optional<MoveAxis>(axis);
    if (!gesture_) return;
    // Keep the centre; the old baseline is not in the new plane.
    gesture_->normal = axis_ ? axisVector(*axis_) : Vec3{0.0, 0.0, 1.0};
    gesture_->reference.reset();
    gesture_->angle = 0.0;
    publish(std::nullopt);
}

Vec3 ProtractorTool::planeNormal(const PointerInput& input) const {
    if (axis_) return axisVector(*axis_);
    // Flat on the face under the pointer, else on the ground.
    if (input.pick) {
        if (const auto* o = editor().document().find(input.pick->objectId)) {
            for (const auto& f : buildFaces(*o)) {
                if (f.id == input.pick->faceId && glm::length(f.normal) > 0.5) return glm::normalize(f.normal);
            }
        }
    }
    return {0.0, 0.0, 1.0};
}

std::optional<Vec3> ProtractorTool::planePoint(const PointerInput& input, bool& exact) {
    exact = false;
    const Vec3 c = gesture_->center;
    const Vec3 n = gesture_->normal;
    const SnapResult snap = ctx_.snapPointer(input);
    if (isPointSnap(snap.kind)) {
        const Vec3 p = snap.point - n * glm::dot(snap.point - c, n);
        if (glm::length(p - c) > tol::kMinDimension) {
            ctx_.indicator = snap;
            exact = true;
            return p;
        }
    }
    ctx_.indicator.reset();
    return intersectPlane(input.ray, c, n);
}

void ProtractorTool::publish(const std::optional<Vec3>& current) {
    if (!gesture_) return;
    ProtractorPreview p;
    p.center = gesture_->center;
    p.normal = gesture_->normal;
    p.reference = gesture_->reference;
    p.current = current;
    p.angle = gesture_->angle;
    if (gesture_->reference) {
        p.guide = Guide::line(gesture_->center, protractorDirection(*gesture_->reference, gesture_->normal, gesture_->angle));
    }
    editor().setPreview(p);
}

void ProtractorTool::track(const PointerInput& input) {
    bool exact = false;
    const auto p = planePoint(input, exact);
    if (!p) return;
    const Vec3 current = *p - gesture_->center;
    if (gesture_->reference && glm::length(current) > 1e-9) {
        const Vec3& a = *gesture_->reference;
        double angle = std::atan2(glm::dot(glm::cross(a, current), gesture_->normal), glm::dot(a, current));
        if (ctx_.snap.enabled && !exact) angle = snapValue(angle, kSnapRadians);
        gesture_->angle = angle;
    }
    publish(current);
}

void ProtractorTool::pointerMove(const PointerInput& input) {
    if (gesture_) {
        track(input);
        return;
    }
    // Before the first click: show where the protractor would lie.
    const SnapResult snap = ctx_.snapPointer(input);
    ctx_.indicator = snap.kind == SnapKind::None ? std::nullopt : std::optional<SnapResult>(snap);
    ProtractorPreview p;
    p.center = snap.point;
    p.normal = planeNormal(input);
    if (snap.kind != SnapKind::None || input.pick) editor().setPreview(p);
    else editor().cancelPreview();
}

void ProtractorTool::pointerDown(const PointerInput& input) {
    if (!gesture_) {
        // First click: the centre.
        const SnapResult snap = ctx_.snapPointer(input);
        if (snap.kind == SnapKind::None && !input.pick && !intersectHorizontalPlane(input.ray, 0.0)) return;
        gesture_ = Gesture{snap.point, planeNormal(input), std::nullopt, 0.0};
        editor().notify(std::nullopt);
        publish(std::nullopt);
        return;
    }
    if (!gesture_->reference) {
        // Second click: the baseline.
        bool exact = false;
        const auto p = planePoint(input, exact);
        if (!p || glm::length(*p - gesture_->center) < tol::kMinDimension) {
            editor().notify(Message{Message::Kind::Error, "Click a point away from the centre to set the baseline."});
            return;
        }
        gesture_->reference = *p - gesture_->center;
        gesture_->angle = 0.0;
        publish(gesture_->reference);
        return;
    }
    // Third click: the guide at the current angle.
    track(input);
    commit(gesture_->angle);
}

bool ProtractorTool::commit(double angle) {
    if (!gesture_ || !gesture_->reference) return false;
    const Guide guide = Guide::line(gesture_->center, protractorDirection(*gesture_->reference, gesture_->normal, angle));
    const bool ok = editor().commit("Add guide at " + formatAngle(angle),
                                   [&guide](const Document& doc) { return addGuide(doc, guide); });
    if (!ok) return false;
    gesture_.reset();
    ctx_.indicator.reset();
    editor().cancelPreview();
    editor().notify(Message{Message::Kind::Info, "Guide added at " + formatAngle(angle) + " from the baseline."});
    return true;
}

std::optional<std::string> ProtractorTool::submit(std::string_view text) {
    if (!gesture_ || !gesture_->reference) {
        return std::string("Click the centre and a point on the baseline first, then type the angle.");
    }
    const auto parsed = parseAngle(text);
    if (!parsed) return parsed.error();
    double angle = *parsed;
    // A typed angle turns the way the pointer is turning.
    if (gesture_->angle < 0.0) angle = -angle;
    if (!commit(angle)) return ctx_.lastError("Could not add the guide.");
    return std::nullopt;
}

std::string ProtractorTool::hint() const {
    const std::string lock = axis_ ? " Plane locked square to " + std::string(axisName(*axis_)) + " (arrow key again to release)."
                                   : std::string(" Arrow keys lock the plane (Right X, Left Y, Up Z).");
    if (!gesture_) return "Protractor: click the centre, on a face or on the ground." + lock;
    if (!gesture_->reference) return "Protractor: click a point on the baseline the angle is measured from. Esc cancels." + lock;
    return "Protractor: " + formatAngle(gesture_->angle) +
           ". Move to set the angle (15° steps with grid snap) and click, or type degrees. Esc cancels.";
}

} // namespace pf
