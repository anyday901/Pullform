#include "app/tools/freehand_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/freehand.hpp"
#include "core/geometry/pick.hpp"

#include <cmath>
#include <format>

namespace pf {
namespace {

bool inRange(const Vec3& p) {
    return std::abs(p.x) <= tol::kMaxCoordinate && std::abs(p.y) <= tol::kMaxCoordinate &&
           std::abs(p.z) <= tol::kMaxCoordinate;
}

} // namespace

void FreehandTool::pointerDown(const PointerInput& input) {
    if (stroke_) return; // a second button press mid-stroke changes nothing
    using enum SnapKind;
    const SnapResult start = ctx_.snapPointer(input);
    if (start.kind == None && !intersectHorizontalPlane(input.ray, 0.0)) return; // nothing under the pointer
    if (!inRange(start.point)) {
        editor().notify(Message{Message::Kind::Error, "That point is outside the +/-10 km model range. Start closer to the model."});
        return;
    }
    Stroke s;
    s.points.push_back(start.point);
    // Starting on an object draws into it, so the stroke sticks to that geometry.
    if (start.kind == Endpoint || start.kind == Midpoint || start.kind == OnFace) {
        s.target = start.objectId;
    }
    if (input.pixelAngle > 0.0) {
        const double perPixel = input.pixelAngle * glm::length(start.point - input.ray.origin);
        if (perPixel > 0.0) {
            s.tolerance = perPixel * kSmoothingPx;
            s.closeRadius = ctx_.snapper.tolerance(input, start.point);
        }
    }
    if (!(s.closeRadius > 0.0)) s.closeRadius = s.tolerance * 4.0;
    s.pressScreen = input.screen;
    stroke_ = std::move(s);
    ctx_.indicator = start.kind == None ? std::nullopt : std::optional(start);
    editor().notify(std::nullopt);
    showPreview();
}

void FreehandTool::pointerMove(const PointerInput& input) {
    if (!stroke_) {
        // Show where a stroke would start (and what it would stick to).
        if (const SnapResult res = ctx_.snapPointer(input); res.kind == SnapKind::None) ctx_.indicator.reset();
        else ctx_.indicator = res;
        return;
    }
    if (const auto p = planePoint(input)) addPoint(*p);
    // Mark the start while the release would close the loop.
    if (closes()) ctx_.indicator = SnapResult{stroke_->points.front(), SnapKind::Endpoint, std::nullopt, -1, stroke_->points.front(), {}};
    else ctx_.indicator.reset();
    showPreview();
}

void FreehandTool::pointerUp(const PointerInput& input) {
    if (!stroke_) return;
    if (!isDrag(stroke_->pressScreen, input.screen) && stroke_->points.size() < 3) {
        // A click draws nothing; wait for a drag.
        abandon();
        editor().notify(Message{Message::Kind::Info, "Freehand: press and drag to draw a curve."});
        return;
    }
    if (const auto p = planePoint(input)) addPoint(*p);
    const bool closed = closes();
    std::vector<Vec3> raw = stroke_->points;
    if (closed) raw.back() = raw.front();
    const std::vector<Vec3> points = freehand::smoothStroke(raw, stroke_->tolerance, closed);
    const std::optional<ObjectId> target = stroke_->target;
    if (points.size() < 2) {
        abandon();
        editor().notify(Message{Message::Kind::Error, "The stroke is too short to draw. Drag further before releasing."});
        return;
    }
    const bool ok = editor().commit("Freehand", [&target, &points, closed](const Document& doc) { return drawPolyline(doc, target, points, closed); });
    abandon(); // the stroke is done either way; a failed commit has already set the error message
    if (ok) {
        const std::size_t edges = closed ? points.size() : points.size() - 1;
        editor().notify(Message{Message::Kind::Info, std::format("Freehand: {} edges{}", edges, closed ? " in a closed loop." : ".")});
    }
}

std::optional<std::string> FreehandTool::submit(std::string_view /*text*/) {
    return std::string("Freehand takes no typed values. Press and drag to draw.");
}

std::string FreehandTool::hint() const {
    if (stroke_) {
        return closes() ? "Freehand: release to close the loop and make a face. Esc cancels."
                        : "Freehand: drag the curve and release. End on the start point to close it. Esc cancels.";
    }
    return "Freehand: press and drag to draw a smooth curve on the ground or on top of the geometry you start on.";
}

std::optional<Vec3> FreehandTool::planePoint(const PointerInput& input) const {
    const auto p = intersectHorizontalPlane(input.ray, stroke_->points.front().z);
    if (!p || !inRange(*p)) return std::nullopt;
    return p;
}

void FreehandTool::addPoint(const Vec3& p) {
    if (glm::length(p - stroke_->points.back()) > tol::kWeldDistance) stroke_->points.push_back(p);
}

bool FreehandTool::closes() const { return stroke_ && freehand::closesLoop(stroke_->points, stroke_->closeRadius); }

void FreehandTool::showPreview() { editor().setPreview(FreehandPreview{stroke_->points, closes()}); }

} // namespace pf
