#include "app/tools/circle_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"

#include <cmath>

namespace pf {

CircleTool::CircleTool(ToolContext& ctx, ProfileKind kind)
    : ToolBase(ctx), kind_(kind), segments_(kind == ProfileKind::Circle ? kDefaultCircleSegments : kDefaultPolygonSides) {}

bool CircleTool::key(ToolKey key) {
    if (!plane_.key(key)) return false;
    if (lastInput_) pointerMove(*lastInput_); // show the new plane right away
    return true;
}

void CircleTool::begin(const PointerInput& input) {
    const auto hit = plane_.hover(ctx_, input);
    if (!hit) return;
    if (auto e = checkRectangleCorner(hit->point)) {
        editor().notify(Message{Message::Kind::Error, "Center: " + *e + " Pick a center closer to the model."});
        return;
    }
    plane_.begin(hit->plane, hit->point, hit->object);
    gesture_ = Gesture{hit->point, hit->point, input.screen, true};
    publish();
    editor().notify(std::nullopt);
}

void CircleTool::pointerDown(const PointerInput& input) {
    lastInput_ = input;
    if (!gesture_) {
        begin(input);
        return;
    }
    gesture_->pressScreen = input.screen;
    gesture_->pressed = true;
    finish(input);
}

void CircleTool::pointerMove(const PointerInput& input) {
    lastInput_ = input;
    if (!gesture_) {
        plane_.hover(ctx_, input); // shows the snap and the plane before the center
        return;
    }
    if (const auto p = plane_.point(ctx_, input, gesture_->center)) {
        gesture_->edge = *p;
        publish();
    }
}

void CircleTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    // Press-drag-release draws in one gesture; a plain click waits for the radius.
    if (isDrag(gesture_->pressScreen, input.screen)) finish(input);
}

void CircleTool::publish() {
    if (!gesture_) return;
    const auto placement = circleOnPlane(plane(), gesture_->center, gesture_->edge, segments_);
    editor().setPreview(
        CirclePreview{kind_, gesture_->center, gesture_->edge, segments_, placement ? std::optional(*placement) : std::nullopt});
}

void CircleTool::finish(const PointerInput& input) {
    if (!gesture_) return;
    const auto p = plane_.point(ctx_, input, gesture_->center);
    if (!p) return;
    gesture_->edge = *p;
    const auto placement = circleOnPlane(plane(), gesture_->center, *p, segments_);
    if (!placement) {
        // Keep the center so the user can pick a valid radius.
        editor().notify(Message{Message::Kind::Error, placement.error()});
        return;
    }
    commit(*placement);
}

bool CircleTool::commit(const CirclePlacement& placement) {
    const ProfileKind kind = kind_;
    const char* label = kind == ProfileKind::Circle ? "Draw circle" : "Draw polygon";
    bool ok = false;
    if (const auto target = plane_.target()) {
        // Drawn on a face: into that object's edges and faces (see RectangleTool).
        mesh::FaceId face{0};
        ok = editor().commit(
            label, [&target, &placement, kind, &face](const Document& doc) { return drawCircleOn(doc, *target, placement, kind, &face); },
            [&target, &face](const OpResult& res) -> std::optional<Selection> {
                if (res.objectId && *res.objectId != *target) return Selection{*res.objectId, FaceId::profile()}; // a shape of its own
                if (face.value == 0) return Selection{*target, std::nullopt};
                return Selection{*target, FaceId::meshFace(face.value)};
            });
    } else {
        ok = editor().commit(
            label, [&placement, kind](const Document& doc) { return createCircle(doc, placement, kind); },
            [](const OpResult& res) -> std::optional<Selection> {
                if (!res.objectId) return std::nullopt;
                return Selection{*res.objectId, FaceId::profile()};
            });
    }
    if (ok) {
        gesture_.reset();
        plane_.end();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> CircleTool::submit(std::string_view text) {
    if (const auto count = parseSegmentCount(text)) {
        if (!*count) return count->error();
        segments_ = **count;
        publish(); // the preview follows the new count at once
        return std::nullopt;
    }
    const auto radius = parseLength(text, editor().document().displayUnit);
    if (!radius) {
        return "Radius: " + radius.error() + (kind_ == ProfileKind::Circle ? " Type 24s for 24 segments." : " Type 6s for 6 sides.");
    }
    if (*radius < 0) return std::string("The radius must be positive.");
    Vec3 center{0.0};
    double start = 0.0;
    Quat rotation{1.0, 0.0, 0.0, 0.0};
    if (gesture_) {
        const PlaneBasis p = plane();
        center = gesture_->center;
        rotation = planeRotation(p);
        // The first vertex points toward the pointer, as when clicking.
        const Vec2 d = toPlane(p, gesture_->edge) - toPlane(p, center);
        if (std::abs(d.x) > tol::kLengthEpsilon || std::abs(d.y) > tol::kLengthEpsilon) start = std::atan2(d.y, d.x);
    }
    const auto placement = circleFromRadius(center, *radius, start, segments_, rotation);
    if (!placement) return placement.error();
    if (!commit(*placement)) return ctx_.lastError(kind_ == ProfileKind::Circle ? "Could not create the circle." : "Could not create the polygon.");
    return std::nullopt;
}

std::string_view CircleTool::measurementLabel() const {
    return kind_ == ProfileKind::Circle ? "Radius (or segments, e.g. 24s)" : "Radius (or sides, e.g. 6s)";
}

std::string CircleTool::hint() const {
    const std::string count = std::to_string(segments_) + (kind_ == ProfileKind::Circle ? " segments" : " sides");
    const std::string name = kind_ == ProfileKind::Circle ? "Circle" : "Polygon";
    const std::string example = kind_ == ProfileKind::Circle ? "48s" : "8s";
    if (gesture_) {
        return name + " (" + count + "): click a point on the " + noun() + ", or type the radius and press Enter. Type " +
               example + " to change the count. Esc cancels." + plane_.lockHint();
    }
    return name + " (" + count + "): click the center on a face or the ground (arrow keys lock a plane), or type the radius "
           "to draw at the origin. Type " + example + " to change the count." + plane_.lockHint();
}

} // namespace pf
