#include "app/tools/tool_controller.hpp"

#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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
    case Tool::Orbit: return "Orbit";
    case Tool::TapeMeasure: return "Tape Measure";
    case Tool::Dimension: return "Dimension";
    }
    return "Select";
}

void ToolController::setTool(Tool tool) {
    if (tool == tool_) return;
    cancel();
    tool_ = tool;
    indicator_.reset();
    references_.clear();
    if (tool == Tool::TapeMeasure) measure_.start(MeasureTool::Mode::Tape);
    if (tool == Tool::Dimension) measure_.start(MeasureTool::Mode::Dimension);
    editor_.setHover(std::nullopt);
    editor_.notify(std::nullopt);
}

bool ToolController::cancel() {
    const bool had = active() || !std::holds_alternative<std::monostate>(editor_.preview());
    rect_.reset();
    push_.reset();
    indicator_.reset();
    measure_.cancel();
    editor_.cancelPreview();
    return had;
}

Vec3 ToolController::snapPoint(const Vec3& p) const {
    if (!snap_.enabled || snap_.step <= 0.0) return p;
    return {snapValue(p.x, snap_.step), snapValue(p.y, snap_.step), p.z};
}

double ToolController::tolerance(const PointerInput& input, const Vec3& at) const {
    return input.pixelAngle * snap_.radiusPx * glm::length(at - input.ray.origin);
}

void ToolController::remember(const Vec3& p) {
    // Most recent first, no duplicates, at most kMaxReferences.
    const Vec3 point = p;
    std::erase(references_, point);
    if (references_.size() >= SnapSettings::kMaxReferences) references_.resize(SnapSettings::kMaxReferences - 1);
    references_.push_back(point);
    std::rotate(references_.rbegin(), references_.rbegin() + 1, references_.rend());
}

std::optional<SnapResult> ToolController::pointSnap(const PointerInput& input, std::optional<ObjectId> exclude) {
    if (!snap_.points || !(input.pixelAngle > 0.0)) return std::nullopt;
    snapIndex_.sync(editor_.document());
    // The face under the pointer hides points behind it, unless it belongs to
    // the excluded object (the one being edited, drawn as a preview).
    std::optional<double> occluder;
    if (input.pick && !(exclude && input.pick->objectId == *exclude)) occluder = input.pick->distance;
    auto hit = snapIndex_.nearest(input.ray, input.pixelAngle * snap_.radiusPx, occluder, exclude);
    if (hit) remember(hit->featurePoint);
    return hit;
}

std::optional<SnapResult> ToolController::snapGround(const PointerInput& input) {
    if (auto hit = pointSnap(input)) {
        // Rectangles are drawn on the ground: use the point straight below.
        hit->point.z = 0.0;
        if (std::abs(hit->featurePoint.z) > tol::kLengthEpsilon) hit->guides.push_back({hit->featurePoint, hit->point, 2});
        return hit;
    }
    const auto raw = intersectHorizontalPlane(input.ray, 0.0);
    if (!raw) return std::nullopt;
    SnapResult res;
    res.point = snapPoint(*raw);
    res.kind = snap_.enabled && snap_.step > 0.0 ? SnapKind::Grid : SnapKind::None;
    res.featurePoint = res.point;
    if (!snap_.points || !(input.pixelAngle > 0.0)) return res;

    // Inference: line up with a remembered point along X or Y. The first
    // corner of the rectangle is no use as a reference (it would give a zero
    // width or depth), so it is skipped.
    const double tolerance = this->tolerance(input, *raw);
    const Vec3* refX = nullptr; // same x as the pointer: guide runs along Y
    const Vec3* refY = nullptr; // same y: guide runs along X
    double bestX = tolerance, bestY = tolerance;
    for (const auto& r : references_) {
        if (rect_ && std::abs(r.x - rect_->start.x) <= tol::kLengthEpsilon && std::abs(r.y - rect_->start.y) <= tol::kLengthEpsilon) {
            continue;
        }
        if (const double d = std::abs(raw->x - r.x); d < bestX) { bestX = d; refX = &r; }
        if (const double d = std::abs(raw->y - r.y); d < bestY) { bestY = d; refY = &r; }
    }
    if (!refX && !refY) return res;
    if (refX) res.point.x = refX->x;
    if (refY) res.point.y = refY->y;
    res.kind = SnapKind::Inference;
    res.featurePoint = res.point;
    if (refX) res.guides.push_back({{refX->x, refX->y, 0.0}, res.point, 1});
    if (refY) res.guides.push_back({{refY->x, refY->y, 0.0}, res.point, 0});
    return res;
}

std::optional<SnapResult> ToolController::measureSnap(const PointerInput& input) {
    SnapResult res = snapPointer(input);
    // snapPointer reports a miss as SnapKind::None, but so is a plain ground
    // point with grid snap off: only a ray that misses the ground is a miss.
    if (res.kind == SnapKind::None && !intersectHorizontalPlane(input.ray, 0.0)) {
        indicator_.reset();
        return std::nullopt;
    }
    if (res.kind == SnapKind::None) indicator_.reset();
    else indicator_ = res;
    return res;
}

SnapResult ToolController::snapPointer(const PointerInput& input) {
    if (auto hit = pointSnap(input)) return *hit;
    if (input.pick) {
        SnapResult res;
        res.point = res.featurePoint = input.pick->point;
        res.kind = SnapKind::OnFace;
        res.objectId = input.pick->objectId;
        return res;
    }
    if (auto ground = snapGround(input)) return *ground;
    return SnapResult{input.ray.origin, SnapKind::None, std::nullopt, -1, input.ray.origin, {}};
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
    case Tool::Orbit: break; // navigation owns the drag
    case Tool::TapeMeasure:
    case Tool::Dimension: measure_.pointerDown(input, measureSnap(input)); break;
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
    case Tool::Orbit: break;
    case Tool::TapeMeasure:
    case Tool::Dimension: measure_.pointerMove(input, measureSnap(input)); break;
    }
}

void ToolController::pointerUp(const PointerInput& input) {
    const auto moved = [&](const Vec2& press) { return glm::length(input.screen - press) > kDragThresholdPx; };
    if (tool_ == Tool::TapeMeasure || tool_ == Tool::Dimension) {
        measure_.pointerUp(input, measureSnap(input));
    } else if (rect_ && rect_->pressed) {
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
    const auto total = snapPushHeight(input, t ? std::optional(push_->startHeight + *t) : std::nullopt);
    if (total) setPushPreview(*total - push_->startHeight);
}

std::optional<double> ToolController::snapPushHeight(const PointerInput& input, std::optional<double> raw) {
    const auto at = [&](double h) { return Vec3{push_->anchor.x, push_->anchor.y, push_->baseZ + h}; };

    // 1. The pointer is on a corner or edge midpoint of another object: take its height.
    if (auto hit = pointSnap(input, push_->objectId)) {
        const double h = hit->featurePoint.z - push_->baseZ;
        hit->point = at(h);
        hit->guides.push_back({hit->featurePoint, hit->point, -1});
        indicator_ = *hit;
        return h;
    }
    if (!raw) return std::nullopt;
    double total = *raw;
    const Vec3 axisPoint = at(total);
    // 2. The height lines up with another object's corner (for example its cap).
    if (snap_.points && input.pixelAngle > 0.0) {
        const double tolerance = this->tolerance(input, axisPoint);
        const SnapFeatures* bestF = nullptr;
        ObjectId bestId;
        int bestV = -1;
        double bestDz = tolerance, bestXY = std::numeric_limits<double>::infinity();
        for (const auto& [id, entry] : snapIndex_.entries()) {
            if (id == push_->objectId) continue;
            const auto& f = entry.features;
            for (std::size_t i = 0; i < f.vertices.size(); ++i) {
                const Vec3& v = f.vertices[i];
                const double dz = std::abs(v.z - axisPoint.z);
                const double dxy = glm::length(Vec2{v.x - axisPoint.x, v.y - axisPoint.y});
                // Nearest height first; among equal heights, the nearest corner draws the guide.
                if (dz < bestDz - tol::kLengthEpsilon || (bestF && std::abs(dz - bestDz) <= tol::kLengthEpsilon && dxy < bestXY)) {
                    bestDz = dz;
                    bestXY = dxy;
                    bestF = &f;
                    bestId = id;
                    bestV = static_cast<int>(i);
                }
            }
        }
        if (bestF) {
            const Vec3& v = bestF->vertices[static_cast<std::size_t>(bestV)];
            const double h = v.z - push_->baseZ;
            SnapResult res{at(h), SnapKind::Inference, bestId, bestV, v, {}};
            res.guides.push_back({v, res.point, -1});
            indicator_ = res;
            return h;
        }
    }
    // 3. Grid: snap the total height to the increment.
    if (snap_.enabled && snap_.step > 0.0) total = snapValue(total, snap_.step);
    indicator_ = SnapResult{at(total), snap_.enabled ? SnapKind::Grid : SnapKind::None, std::nullopt, -1, at(total), {}};
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
    if (indicator_ && indicator_->kind >= SnapKind::OnFace && tool_ != Tool::Select && tool_ != Tool::Orbit) {
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
    case Tool::Orbit:
        return "Orbit: drag with the left button to orbit. Right-drag pans, the wheel zooms.";
    case Tool::TapeMeasure:
    case Tool::Dimension: return measure_.hint();
    }
    return {};
}

} // namespace pf
