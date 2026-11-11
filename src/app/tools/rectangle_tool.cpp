#include "app/tools/rectangle_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

namespace pf {

namespace {

/// The ground preview of an axis-aligned rectangle drawn from `a` to `b`
/// keeps no outline (the viewport draws it from start and end).
bool isGround(const PlaneBasis& plane) { return plane == groundPlane(); }

} // namespace

Vec3 RectangleTool::normal() const { return plane_.plane() ? pf::planeNormal(*plane_.plane()) : Vec3{0.0, 0.0, 1.0}; }

Vec3 RectangleTool::sideDirection(const Vec3& p) const {
    const Vec3 d = p - gesture_->start;
    if (glm::length(d) > 1e-12) return glm::normalize(d);
    return plane_.plane() ? plane_.plane()->xAxis : Vec3{1.0, 0.0, 0.0};
}

double RectangleTool::depthAt(const Vec3& p) const {
    const Vec3 across = glm::normalize(glm::cross(normal(), sideDirection(*gesture_->side)));
    return glm::dot(p - gesture_->start, across);
}

std::string_view RectangleTool::measurementLabel() const {
    if (mode_ == Mode::Rotated && gesture_) return gesture_->side ? "Depth" : "Length (or length, depth)";
    return "Dimensions (width, depth)";
}

bool RectangleTool::key(ToolKey key) {
    // Once a rotated rectangle's first side is placed, the plane must keep
    // containing it: turning the plane could make it parallel to the normal.
    if (mode_ == Mode::Rotated && gesture_ && gesture_->side) return false;
    if (!plane_.key(key)) return false;
    // Show the new plane right away, from the last pointer position.
    if (lastInput_) pointerMove(*lastInput_);
    return true;
}

void RectangleTool::begin(const PointerInput& input) {
    const auto hit = plane_.hover(ctx_, input);
    if (!hit) return;
    // A first corner near the horizon lands far outside the model range;
    // refuse it now rather than start a rectangle that can never finish.
    if (auto e = checkRectangleCorner(hit->point)) {
        editor().notify(Message{Message::Kind::Error, *e + " Pick a first corner closer to the model."});
        return;
    }
    plane_.begin(hit->plane, hit->point, hit->object);
    gesture_ = Gesture{hit->point, input.screen, true, std::nullopt, hit->point};
    update(hit->point);
    editor().notify(std::nullopt);
}

void RectangleTool::pointerDown(const PointerInput& input) {
    lastInput_ = input;
    if (!gesture_) {
        begin(input);
        return;
    }
    gesture_->pressScreen = input.screen;
    gesture_->pressed = true;
    if (mode_ == Mode::Rotated && !gesture_->side) {
        if (const auto p = plane_.point(ctx_, input, gesture_->start)) placeSide(*p);
        return;
    }
    finish(input);
}

void RectangleTool::pointerMove(const PointerInput& input) {
    lastInput_ = input;
    if (!gesture_) {
        plane_.hover(ctx_, input); // shows the snap and the plane before the first corner
        return;
    }
    if (const auto p = plane_.point(ctx_, input, gesture_->start)) update(*p);
}

void RectangleTool::pointerUp(const PointerInput& input) {
    if (!gesture_ || !gesture_->pressed) return;
    gesture_->pressed = false;
    // Press-drag-release draws in one gesture (the first side, for a rotated
    // rectangle); a plain click waits for the next point.
    if (!isDrag(gesture_->pressScreen, input.screen)) return;
    if (mode_ == Mode::Rotated && !gesture_->side) {
        if (const auto p = plane_.point(ctx_, input, gesture_->start)) placeSide(*p);
        return;
    }
    finish(input);
}

void RectangleTool::placeSide(const Vec3& end) {
    if (glm::length(end - gesture_->start) < tol::kMinDimension) {
        editor().notify(Message{Message::Kind::Error, "The first side is too short. Click its other end farther away."});
        return;
    }
    gesture_->side = end;
    editor().notify(std::nullopt);
    update(end);
}

void RectangleTool::update(const Vec3& p) {
    gesture_->pointer = p;
    RectanglePreview preview{gesture_->start, p, std::nullopt, {}, 0.0, 0.0};
    const PlaneBasis plane = plane_.plane().value_or(groundPlane());
    if (mode_ == Mode::Rotated) {
        if (!gesture_->side) {
            preview.outline = {gesture_->start, p};
            preview.width = glm::length(p - gesture_->start);
        } else {
            const double depth = depthAt(p);
            const auto r = rotatedRectangle(gesture_->start, *gesture_->side, normal(), depth);
            if (r) preview.placement = *r;
            preview.width = glm::length(*gesture_->side - gesture_->start);
            preview.depth = std::abs(depth);
            const Vec3 across = glm::normalize(glm::cross(normal(), sideDirection(*gesture_->side))) * depth;
            preview.outline = {gesture_->start, *gesture_->side, *gesture_->side + across, gesture_->start + across};
        }
    } else {
        const Vec2 a = toPlane(plane, gesture_->start);
        const Vec2 b = toPlane(plane, p);
        const auto r = rectangleOnPlane(plane, gesture_->start, p);
        if (r) preview.placement = *r;
        preview.width = std::abs(b.x - a.x);
        preview.depth = std::abs(b.y - a.y);
        if (!isGround(plane)) {
            preview.outline = {fromPlane(plane, a), fromPlane(plane, {b.x, a.y}), fromPlane(plane, b), fromPlane(plane, {a.x, b.y})};
        }
    }
    editor().setPreview(preview);
}

void RectangleTool::finish(const PointerInput& input) {
    if (!gesture_) return;
    const auto p = plane_.point(ctx_, input, gesture_->start);
    if (!p) return;
    const auto r = mode_ == Mode::Rotated
                       ? rotatedRectangle(gesture_->start, *gesture_->side, normal(), depthAt(*p))
                       : rectangleOnPlane(plane_.plane().value_or(groundPlane()), gesture_->start, *p);
    if (!r) {
        // Keep the gesture so the user can pick a valid point.
        editor().notify(Message{Message::Kind::Error, r.error()});
        return;
    }
    commit(*r);
}

bool RectangleTool::commit(const RectanglePlacement& placement) {
    bool ok = false;
    if (const auto target = plane_.target()) {
        // Drawn on a face: the rectangle goes into that object's edges and
        // faces, so push/pull makes a recess or an opening in it.
        mesh::FaceId face{0};
        ok = editor().commit(
            "Draw rectangle", [&](const Document& doc) { return drawRectangleOn(doc, *target, placement, &face); },
            [&](const OpResult& res) -> std::optional<Selection> {
                if (res.objectId && *res.objectId != *target) return Selection{*res.objectId, FaceId::profile()}; // a shape of its own
                if (face.value == 0) return Selection{*target, std::nullopt};
                return Selection{*target, FaceId::meshFace(face.value)};
            });
    } else {
        ok = editor().commit(
            "Draw rectangle", [&placement](const Document& doc) { return createRectangle(doc, placement); },
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

std::optional<std::string> RectangleTool::submitSize(std::string_view widthText, std::string_view depthText) {
    const LengthUnit unit = editor().document().displayUnit;
    // Rotated, after the first click: one value is the first side's length;
    // after the second click it is the depth.
    const bool single = mode_ == Mode::Rotated && gesture_ && trimmed(depthText).empty() &&
                        trimmed(widthText).find(',') == std::string::npos && trimmed(widthText).find(';') == std::string::npos;
    if (single) {
        const auto v = parseLength(widthText, unit);
        if (!v) return v.error();
        if (*v <= 0) return std::string("The length must be positive; the direction follows the pointer.");
        if (!gesture_->side) {
            const Vec3 end = gesture_->start + sideDirection(gesture_->pointer) * *v;
            if (auto e = checkRectangleCorner(end)) return e;
            placeSide(end);
            return std::nullopt;
        }
        const double sign = depthAt(gesture_->pointer) < 0.0 ? -1.0 : 1.0;
        const auto r = rotatedRectangle(gesture_->start, *gesture_->side, normal(), sign * *v);
        if (!r) return r.error();
        if (!commit(*r)) return ctx_.lastError("Could not create the rectangle.");
        return std::nullopt;
    }
    double w = 0.0;
    double d = 0.0;
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
    Expected<RectanglePlacement> r = Error{""};
    if (!gesture_) {
        r = rectangleOnPlaneFromSize(groundPlane(), Vec3{0.0}, w, d);
    } else if (mode_ == Mode::Rotated) {
        // Both at once after the first click: the side toward the pointer, the
        // depth on the pointer's side of it.
        const Vec3 dir = gesture_->side ? sideDirection(*gesture_->side) : sideDirection(gesture_->pointer);
        const Vec3 end = gesture_->start + dir * w;
        const Vec3 across = glm::cross(normal(), dir);
        const double sign = gesture_->side && glm::dot(gesture_->pointer - gesture_->start, across) < 0.0 ? -1.0 : 1.0;
        r = rotatedRectangle(gesture_->start, end, normal(), sign * d);
    } else {
        const PlaneBasis plane = plane_.plane().value_or(groundPlane());
        const Vec2 delta = toPlane(plane, gesture_->pointer) - toPlane(plane, gesture_->start);
        r = rectangleOnPlaneFromSize(plane, gesture_->start, delta.x < 0 ? -w : w, delta.y < 0 ? -d : d);
    }
    if (!r) return r.error();
    if (!commit(*r)) return ctx_.lastError("Could not create the rectangle.");
    return std::nullopt;
}

std::string RectangleTool::hint() const {
    std::string text;
    if (mode_ == Mode::Rotated) {
        if (!gesture_) text = "Rotated Rectangle: click the first corner on a face or the ground. Arrow keys lock a plane.";
        else if (!gesture_->side) text = "Rotated Rectangle: click the end of the first side, or type its length. Esc cancels.";
        else text = "Rotated Rectangle: click to set the depth, or type it. Esc cancels.";
    } else if (gesture_) {
        text = "Rectangle: click the opposite corner, or type width, depth (e.g. 2000, 1000) and press Enter. Esc cancels.";
    } else {
        text = "Rectangle: click the first corner on a face or the ground (arrow keys lock a plane), or type width, depth to "
               "draw from the origin.";
    }
    return text + plane_.lockHint();
}

} // namespace pf
