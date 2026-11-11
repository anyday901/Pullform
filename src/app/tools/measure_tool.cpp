#include "app/tools/measure_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

namespace {

std::string deltaText(const Vec3& d, LengthUnit u) {
    return "X " + formatLength(d.x, u, false) + ", Y " + formatLength(d.y, u, false) + ", Z " + formatLength(d.z, u, false);
}

#ifdef __APPLE__
constexpr const char* kToggleKey = "Option";
#else
constexpr const char* kToggleKey = "Ctrl";
#endif

/// Snaps that are a point already: a guide point there would add nothing.
bool onPoint(SnapKind kind) {
    return kind == SnapKind::Endpoint || kind == SnapKind::Midpoint || kind == SnapKind::GuidePoint ||
           kind == SnapKind::Intersection || kind == SnapKind::Center;
}

/// Closest point to `p` on the line through `point` along unit `direction`.
Vec3 footOn(const Vec3& point, const Vec3& direction, const Vec3& p) {
    return point + direction * glm::dot(p - point, direction);
}

} // namespace

void MeasureTool::start(Mode mode) {
    reset();
    mode_ = mode;
}

void MeasureTool::toggleGuides() {
    guides_ = !guides_;
    // A line start only means something while adding guides.
    if (!guides_) line_.reset();
    if (start_ && !fixed_) publish(cursor_);
}

bool MeasureTool::reset() {
    const bool had = active();
    start_.reset();
    line_.reset();
    end_.reset();
    fixed_ = false;
    offset_.reset();
    pressed_ = false;
    return had;
}

MeasurePoint MeasureTool::pointAt(const SnapResult& snap) const {
    const Document& doc = editor().document();
    // Only corners and edge midpoints attach; a point on a face or on the
    // ground stays where it was placed even if the object later changes.
    if (snap.kind == SnapKind::Endpoint || snap.kind == SnapKind::Midpoint) {
        return MeasurePoint{snap.point, anchorAt(doc, snap.point, snap.objectId)};
    }
    return MeasurePoint{snap.point, DimensionAnchor{snap.point, std::nullopt, {}}};
}

std::optional<MeasureTool::Line> MeasureTool::lineAt(const PointerInput& input, const SnapResult& snap) const {
    if (mode_ != Mode::Tape || !guides_ || onPoint(snap.kind)) return std::nullopt;
    const Document& doc = editor().document();
    // On a guide line or an edge, as the inference engine found it.
    if (snap.kind == SnapKind::OnGuide && snap.constraint && snap.constraint->type == SnapConstraint::Type::Line) {
        return Line{snap.point, snap.constraint->direction};
    }
    if (snap.kind == SnapKind::OnEdge && snap.edge && snap.edge->direction() != Vec3{0.0}) {
        return Line{snap.point, snap.edge->direction()};
    }
    // Else the edge the element picker reports (an edge the snap radius misses).
    if (!input.element || input.element->element.kind != ElementKind::Edge) return std::nullopt;
    const auto* o = doc.find(input.element->element.object);
    if (!o) return std::nullopt;
    const ElementTopology topo = buildTopology(*o);
    const auto index = topo.indexOf(ElementKind::Edge, input.element->element.id);
    if (!index) return std::nullopt;
    const auto& e = topo.edges[*index];
    const Vec3 a = topo.vertices[e.a].position;
    const Vec3 b = topo.vertices[e.b].position;
    if (glm::length(b - a) < tol::kLengthEpsilon) return std::nullopt;
    const Vec3 dir = glm::normalize(b - a);
    // The point on the edge under the pointer (the pick may sit just off it).
    return Line{footOn(a, dir, input.element->point), dir};
}

std::optional<Guide> MeasureTool::guideAt(const MeasurePoint& cursor, SnapKind kind) const {
    if (mode_ != Mode::Tape || !guides_ || !start_ || fixed_) return std::nullopt;
    if (line_) {
        // Parallel to the start line, through the pointer; none on the line itself.
        const Vec3 foot = footOn(line_->point, line_->direction, cursor.point);
        if (glm::length(cursor.point - foot) < tol::kMinDimension) return std::nullopt;
        return Guide::line(cursor.point, line_->direction);
    }
    if (onPoint(kind) || glm::length(cursor.point - start_->point) < tol::kMinDimension) return std::nullopt;
    return Guide::at(cursor.point);
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
    preview.guide = guideAt(cursor, cursorKind_);
    // A parallel guide measures its distance square to the start line.
    if (preview.guide && line_) preview.start = footOn(line_->point, line_->direction, cursor.point);
    editor().setPreview(preview);
}

std::optional<Vec3> MeasureTool::inferFrom() const {
    // While the second point is being picked, infer along axes and edges from the first.
    if (start_ && !fixed_) return start_->point;
    return std::nullopt;
}

bool MeasureTool::commitGuide(const Guide& guide) {
    const LengthUnit u = editor().document().displayUnit;
    std::string label;
    std::string info;
    if (guide.kind == Guide::Kind::Line && line_) {
        const double d = glm::length(guide.point - footOn(line_->point, line_->direction, guide.point));
        label = "Add guide " + formatLength(d, u);
        info = "Guide added " + formatLength(d, u) + " from the line.";
    } else {
        const double d = start_ ? glm::length(guide.point - start_->point) : 0.0;
        label = "Add guide point";
        info = "Guide point added " + formatLength(d, u) + " from the start.";
    }
    const bool ok = editor().commit(label, [&guide](const Document& doc) { return addGuide(doc, guide); });
    if (!ok) return false;
    const MeasurePoint keep = cursor_;
    reset();
    editor().notify(Message{Message::Kind::Info, info});
    publish(keep);
    return true;
}

std::optional<SnapResult> MeasureTool::snapAt(const PointerInput& input) { return ctx_.measureSnap(input, inferFrom()); }

void MeasureTool::pointerMove(const PointerInput& input) {
    const auto snap = snapAt(input);
    // Placing the dimension line needs only the ray, so it keeps working when
    // the pointer is above the horizon and hits neither the model nor the ground.
    if (placingLine()) {
        if (const auto o = offsetFromRay(start_->point, end_->point, input.ray)) offset_ = *o;
    }
    if (snap) cursorKind_ = snap->kind;
    publish(snap ? pointAt(*snap) : cursor_);
}

void MeasureTool::pointerDown(const PointerInput& input) {
    const auto snap = snapAt(input);
    pressScreen_ = input.screen;
    pressed_ = true;
    click(input, snap);
}

void MeasureTool::pointerUp(const PointerInput& input) {
    const auto snap = snapAt(input);
    if (!pressed_) return;
    pressed_ = false;
    // Press-drag-release places the next point at the release, like two clicks.
    // Only between the first and second point: the line offset is always a click.
    if (start_ && !fixed_ && isDrag(pressScreen_, input.screen)) click(input, snap);
}

void MeasureTool::click(const PointerInput& input, const std::optional<SnapResult>& snap) {
    if (placingLine()) {
        // Third click places the line; it needs only the ray, not a hit.
        if (const auto o = offsetFromRay(start_->point, end_->point, input.ray)) offset_ = *o;
        commitDimension(offset_.value_or(Vec3{0.0}));
        return;
    }
    if (!snap) return; // nothing under the pointer to measure from
    MeasurePoint p = pointAt(*snap);
    cursorKind_ = snap->kind;
    const LengthUnit u = editor().document().displayUnit;
    if (!start_ || (mode_ == Mode::Tape && fixed_)) {
        // First point (a click after a finished tape measurement starts over).
        reset();
        pressed_ = true;
        line_ = lineAt(input, *snap);
        if (line_) p = MeasurePoint{line_->point, DimensionAnchor{line_->point, std::nullopt, {}}};
        start_ = p;
        editor().notify(std::nullopt);
        publish(p);
        return;
    }
    if (!fixed_) {
        cursor_ = p;
        if (const auto guide = guideAt(p, snap->kind)) {
            commitGuide(*guide);
            return;
        }
        const Measurement m = measureBetween(start_->point, p.point);
        if (m.length < tol::kMinDimension) {
            editor().notify(Message{Message::Kind::Error, "Pick a second point away from the first."});
            return;
        }
        end_ = p;
        fixed_ = true;
        if (mode_ == Mode::Tape) {
            editor().notify(Message{Message::Kind::Info, "Distance " + formatLength(m.length, u) + "  (" + deltaText(m.delta, u) + ")"});
        } else {
            offset_ = offsetFromRay(start_->point, end_->point, input.ray);
            editor().notify(std::nullopt);
        }
        publish(p);
    }
}

bool MeasureTool::commitDimension(const Vec3& offset) {
    if (!start_ || !end_) return false;
    const DimensionAnchor a = start_->anchor;
    const DimensionAnchor b = end_->anchor;
    const LengthUnit u = editor().document().displayUnit;
    const double length = measureBetween(start_->point, end_->point).length;
    const bool ok = editor().commit("Add dimension " + formatLength(length, u),
                                   [&a, &b, &offset](const Document& doc) { return addDimension(doc, a, b, offset); });
    if (ok) {
        const MeasurePoint keep = cursor_;
        reset();
        publish(keep);
    }
    return ok;
}

std::optional<std::string> MeasureTool::submit(std::string_view text) {
    if (mode_ == Mode::Tape) return submitGuideDistance(text);
    if (!fixed_ || !start_ || !end_) return std::string("Click the two points to dimension first, then type the offset.");
    const auto parsed = parseLength(text, editor().document().displayUnit);
    if (!parsed) return parsed.error();
    if (!std::isfinite(*parsed) || std::abs(*parsed) > tol::kMaxCoordinate) return std::string("The offset is out of range.");
    // The typed distance keeps the side the pointer chose; a negative value flips it.
    Vec3 dir = defaultOffsetDirection(start_->point, end_->point);
    if (offset_ && glm::length(*offset_) > tol::kLengthEpsilon) dir = glm::normalize(*offset_);
    if (!commitDimension(dir * *parsed)) {
        return ctx_.lastError("Could not add the dimension.");
    }
    return std::nullopt;
}

std::optional<std::string> MeasureTool::submitGuideDistance(std::string_view text) {
    if (!guides_) {
        return std::string("The tape measure takes a typed distance only while adding guides. Tap ") + kToggleKey +
               " to turn guides on.";
    }
    if (!start_ || fixed_) return std::string("Click an edge or a point to measure from first, then type the guide distance.");
    const auto parsed = parseLength(text, editor().document().displayUnit);
    if (!parsed) return parsed.error();
    const double d = *parsed;
    if (!std::isfinite(d) || std::abs(d) > tol::kMaxCoordinate) return std::string("The distance is out of range.");
    if (std::abs(d) < tol::kMinDimension) return std::string("Distance must not be zero.");
    // The typed distance goes the way the pointer points; a negative value flips it.
    Guide guide;
    if (line_) {
        const Vec3 off = cursor_.point - footOn(line_->point, line_->direction, cursor_.point);
        if (glm::length(off) < tol::kMinDimension) {
            return std::string("Move the pointer to the side the guide goes on, then type the distance.");
        }
        guide = Guide::line(line_->point + glm::normalize(off) * d, line_->direction);
    } else {
        const Vec3 off = cursor_.point - start_->point;
        if (glm::length(off) < tol::kMinDimension) {
            return std::string("Move the pointer the way the guide point goes, then type the distance.");
        }
        guide = Guide::at(start_->point + glm::normalize(off) * d);
    }
    if (!commitGuide(guide)) return ctx_.lastError("Could not add the guide.");
    return std::nullopt;
}

std::string MeasureTool::hint() const {
    if (mode_ == Mode::Tape) {
        const std::string toggle = guides_ ? std::string(" Adding guides (") + kToggleKey + " to only measure)."
                                           : std::string(" Measuring only (") + kToggleKey + " to add guides).";
        if (start_ && fixed_) return "Tape measure: the distance is shown below. Click to start a new measurement, Esc clears." + toggle;
        if (start_ && line_) {
            return "Tape measure: move off the line and click to add a parallel guide, or type its distance. Esc cancels." + toggle;
        }
        if (start_ && guides_) {
            return "Tape measure: click the second point (a guide point unless it is on a corner), or type a distance. "
                   "Esc cancels." + toggle;
        }
        if (start_) return "Tape measure: click the second point. Corners and edge midpoints snap. Esc cancels." + toggle;
        if (guides_) return "Tape measure: click an edge for a parallel guide, or a point to measure from." + toggle;
        return "Tape measure: click the first point to measure from. Corners and edge midpoints snap." + toggle;
    }
    if (start_ && fixed_) return "Dimension: move to place the dimension line and click, or type the offset and press Enter. Esc cancels.";
    if (start_) return "Dimension: click the second point. Esc cancels.";
    return "Dimension: click the first point. Ends on corners or edge midpoints follow the object.";
}

} // namespace pf
