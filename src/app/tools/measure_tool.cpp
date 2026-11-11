#include "app/tools/measure_tool.hpp"

#include "app/tools/tool_controller.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/measure.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

std::string deltaText(const Vec3& d, LengthUnit u) {
    return "X " + formatLength(d.x, u, false) + ", Y " + formatLength(d.y, u, false) + ", Z " + formatLength(d.z, u, false);
}

} // namespace

void MeasureTool::start(Mode mode) {
    cancel();
    mode_ = mode;
}

bool MeasureTool::cancel() {
    const bool had = active();
    start_.reset();
    end_.reset();
    fixed_ = false;
    offset_.reset();
    pressed_ = false;
    return had;
}

MeasurePoint MeasureTool::pointAt(const SnapResult& snap) const {
    const Document& doc = editor_.document();
    // Only corners and edge midpoints attach; a point on a face or on the
    // ground stays where it was placed even if the object later changes.
    if (snap.kind == SnapKind::Endpoint || snap.kind == SnapKind::Midpoint) {
        return MeasurePoint{snap.point, anchorAt(doc, snap.point, snap.objectId)};
    }
    return MeasurePoint{snap.point, DimensionAnchor{snap.point, std::nullopt, {}}};
}

void MeasureTool::publish(const MeasurePoint& cursor) {
    cursor_ = cursor;
    MeasurePreview preview;
    preview.mode = mode_;
    preview.cursor = cursor.point;
    if (start_) preview.start = start_->point;
    if (end_) preview.end = end_->point;
    else if (start_) preview.end = cursor.point; // live second point
    preview.fixed = fixed_;
    preview.offset = offset_;
    editor_.setPreview(preview);
}

void MeasureTool::pointerMove(const PointerInput& input, const std::optional<SnapResult>& snap) {
    // Placing the dimension line needs only the ray, so it keeps working when
    // the pointer is above the horizon and hits neither the model nor the ground.
    if (placingLine()) {
        if (const auto o = offsetFromRay(start_->point, end_->point, input.ray)) offset_ = *o;
    }
    publish(snap ? pointAt(*snap) : cursor_);
}

void MeasureTool::pointerDown(const PointerInput& input, const std::optional<SnapResult>& snap) {
    pressScreen_ = input.screen;
    pressed_ = true;
    click(input, snap);
}

void MeasureTool::pointerUp(const PointerInput& input, const std::optional<SnapResult>& snap) {
    if (!pressed_) return;
    pressed_ = false;
    // Press-drag-release places the next point at the release, like two clicks.
    // Only between the first and second point: the line offset is always a click.
    if (start_ && !fixed_ && glm::length(input.screen - pressScreen_) > ToolController::kDragThresholdPx) click(input, snap);
}

void MeasureTool::click(const PointerInput& input, const std::optional<SnapResult>& snap) {
    if (placingLine()) {
        // Third click places the line; it needs only the ray, not a hit.
        if (const auto o = offsetFromRay(start_->point, end_->point, input.ray)) offset_ = *o;
        commitDimension(offset_.value_or(Vec3{0.0}));
        return;
    }
    if (!snap) return; // nothing under the pointer to measure from
    const MeasurePoint p = pointAt(*snap);
    const LengthUnit u = editor_.document().displayUnit;
    if (!start_ || (mode_ == Mode::Tape && fixed_)) {
        // First point (a click after a finished tape measurement starts over).
        cancel();
        pressed_ = true;
        start_ = p;
        editor_.notify(std::nullopt);
        publish(p);
        return;
    }
    if (!fixed_) {
        const Measurement m = measureBetween(start_->point, p.point);
        if (m.length < tol::kMinDimension) {
            editor_.notify(Message{Message::Kind::Error, "Pick a second point away from the first."});
            return;
        }
        end_ = p;
        fixed_ = true;
        if (mode_ == Mode::Tape) {
            editor_.notify(Message{Message::Kind::Info, "Distance " + formatLength(m.length, u) + "  (" + deltaText(m.delta, u) + ")"});
        } else {
            offset_ = offsetFromRay(start_->point, end_->point, input.ray);
            editor_.notify(std::nullopt);
        }
        publish(p);
    }
}

bool MeasureTool::commitDimension(const Vec3& offset) {
    if (!start_ || !end_) return false;
    const DimensionAnchor a = start_->anchor;
    const DimensionAnchor b = end_->anchor;
    const LengthUnit u = editor_.document().displayUnit;
    const double length = measureBetween(start_->point, end_->point).length;
    const bool ok = editor_.commit("Add dimension " + formatLength(length, u),
                                   [&a, &b, &offset](const Document& doc) { return addDimension(doc, a, b, offset); });
    if (ok) {
        const MeasurePoint keep = cursor_;
        cancel();
        publish(keep);
    }
    return ok;
}

std::optional<std::string> MeasureTool::submit(std::string_view text) {
    if (mode_ == Mode::Tape) {
        return std::string("The tape measure takes no typed value. Click two points to measure between them.");
    }
    if (!fixed_ || !start_ || !end_) return std::string("Click the two points to dimension first, then type the offset.");
    const auto parsed = parseLength(text, editor_.document().displayUnit);
    if (!parsed) return parsed.error();
    if (!std::isfinite(*parsed) || std::abs(*parsed) > tol::kMaxCoordinate) return std::string("The offset is out of range.");
    // The typed distance keeps the side the pointer chose; a negative value flips it.
    Vec3 dir = defaultOffsetDirection(start_->point, end_->point);
    if (offset_ && glm::length(*offset_) > tol::kLengthEpsilon) dir = glm::normalize(*offset_);
    if (!commitDimension(dir * *parsed)) {
        const auto& m = editor_.message();
        return m && m->kind == Message::Kind::Error ? m->text : std::string("Could not add the dimension.");
    }
    return std::nullopt;
}

std::string MeasureTool::hint() const {
    if (mode_ == Mode::Tape) {
        if (start_ && fixed_) return "Tape measure: the distance is shown below. Click to start a new measurement, Esc clears.";
        if (start_) return "Tape measure: click the second point. Corners and edge midpoints snap. Esc cancels.";
        return "Tape measure: click the first point to measure from. Corners and edge midpoints snap.";
    }
    if (start_ && fixed_) return "Dimension: move to place the dimension line and click, or type the offset and press Enter. Esc cancels.";
    if (start_) return "Dimension: click the second point. Esc cancels.";
    return "Dimension: click the first point. Ends on corners or edge midpoints follow the object.";
}

} // namespace pf
