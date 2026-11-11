#include "app/tools/arc_tool.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <cctype>
#include <charconv>

namespace pf {

namespace {

Vec3 inPlane(const Vec3& v, const Vec3& n) { return v - n * glm::dot(v, n); }

/// "12s" -> 12; nothing when `text` is not a segment count.
std::optional<int> segmentCount(std::string_view text) {
    if (text.size() < 2 || std::tolower(static_cast<unsigned char>(text.back())) != 's') return std::nullopt;
    const std::string_view digits = text.substr(0, text.size() - 1);
    int n = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
    if (ec != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    return n;
}

/// Normal of the picked face when `p` lies on its plane.
std::optional<Vec3> faceNormalAt(const Document& doc, const PointerInput& input, const Vec3& p) {
    if (!input.pick) return std::nullopt;
    const auto* o = doc.find(input.pick->objectId);
    if (!o) return std::nullopt;
    for (const auto& f : buildFaces(*o)) {
        if (f.id != input.pick->faceId) continue;
        const double len = glm::length(f.normal);
        if (!(len > 0.0)) return std::nullopt;
        const Vec3 n = f.normal / len;
        if (std::abs(glm::dot(p - input.pick->point, n)) > tol::kPlanarity) return std::nullopt;
        return n;
    }
    return std::nullopt;
}

/// Direction arriving at `p` along an open edge end of a drawn group, in world
/// space and in the plane with normal `n`.
std::optional<Vec3> tangentAt(const Document& doc, const Vec3& p, const Vec3& n) {
    for (const auto& ptr : doc.objects) {
        if (!ptr->isMeshGroup()) continue;
        const Mat3 r = rotationMatrix(ptr->transform);
        const Vec3 local = glm::transpose(r) * (p - ptr->transform.translation);
        const auto v = ptr->mesh->findVertex(local);
        if (!v) continue;
        const auto t = openEndTangent(*ptr->mesh, *v);
        if (!t) return std::nullopt;
        const Vec3 world = r * *t;
        if (std::abs(glm::dot(world, n)) > 1e-6) return std::nullopt; // leaves the drawing plane
        return glm::normalize(inPlane(world, n));
    }
    return std::nullopt;
}

} // namespace

std::string_view ArcTool::measurementLabel() const {
    const std::size_t n = points_.size();
    switch (mode_) {
    case Mode::TwoPoint: return n == 2 ? "Bulge (or 12s)" : "Length (or 12s)";
    case Mode::ThreePoint: return n == 1 ? "Length (or 12s)" : "Segments (12s)";
    case Mode::Pie: return n == 2 ? "Angle (or 12s)" : "Radius (or 12s)";
    }
    return "Measurements";
}

void ArcTool::cancel() { reset(); }

void ArcTool::reset() {
    points_.clear();
    tangent_.reset();
    cursor_.reset();
    sweep_ = 0.0;
}

std::optional<Vec3> ArcTool::planePoint(const PointerInput& input) {
    const Vec3 n = plane_.normal;
    if (n == Vec3{0.0, 0.0, 1.0} && plane_.origin.z == 0.0) return ctx_.groundPoint(input, groundSkip());
    const SnapResult s = ctx_.snapPointer(input, groundSkip());
    if ((s.kind == SnapKind::Endpoint || s.kind == SnapKind::Midpoint) &&
        std::abs(glm::dot(s.point - plane_.origin, n)) <= tol::kPlanarity) {
        ctx_.indicator = s;
        return s.point;
    }
    ctx_.indicator.reset();
    return intersectPlane(input.ray, plane_.origin, n);
}

void ArcTool::start(const PointerInput& input) {
    const auto s = ctx_.measureSnap(input);
    if (!s) return;
    const Vec3 p = s->point;
    if (auto e = checkRectangleCorner(p)) {
        editor().notify(Message{Message::Kind::Error, *e + " Pick a point closer to the model."});
        return;
    }
    plane_ = Plane{p, faceNormalAt(editor().document(), input, p).value_or(Vec3{0.0, 0.0, 1.0})};
    points_ = {p};
    sweep_ = 0.0;
    tangent_.reset();
    if (mode_ == Mode::TwoPoint) tangent_ = tangentAt(editor().document(), p, plane_.normal);
    cursor_ = p;
    editor().notify(std::nullopt);
    updatePreview(p, &input);
}

void ArcTool::pointerDown(const PointerInput& input) {
    if (points_.empty()) {
        start(input);
        return;
    }
    const auto p = planePoint(input);
    if (!p) return;
    cursor_ = *p;
    if (points_.size() == 1) {
        if (glm::length(*p - points_.front()) < tol::kMinDimension) {
            editor().notify(Message{Message::Kind::Error, "Pick a second point away from the first."});
            return;
        }
        points_.push_back(*p);
        sweep_ = 0.0;
        updatePreview(*p, &input);
        return;
    }
    if (mode_ == Mode::TwoPoint && input.clicks >= 2 && tangent_) {
        // Double-click on the end point: the tangent arc right away.
        const auto arc = tangentArc(points_[0], *tangent_, points_[1], plane_.normal);
        if (!arc) {
            editor().notify(Message{Message::Kind::Error, arc.error()});
            return;
        }
        commit(*arc);
        return;
    }
    const auto arc = arcFor(*p, &input, nullptr, nullptr);
    if (!arc) {
        editor().notify(Message{Message::Kind::Error, arc.error()});
        return;
    }
    commit(*arc);
}

void ArcTool::pointerMove(const PointerInput& input) {
    if (points_.empty()) {
        ctx_.measureSnap(input); // shows the snap before the first point
        return;
    }
    const auto p = planePoint(input);
    if (!p) return;
    cursor_ = *p;
    updatePreview(*p, &input);
}

Expected<ArcGeometry> ArcTool::arcFor(const Vec3& cursor, const PointerInput* input, bool* tangent, bool* half) {
    const Vec3 n = plane_.normal;
    const double tolerance = input && ctx_.snapper.active(*input) ? ctx_.snapper.tolerance(*input, cursor) : 0.0;
    switch (mode_) {
    case Mode::TwoPoint: {
        const Vec3 a = points_[0];
        const Vec3 b = points_[1];
        const Vec3 chord = inPlane(b - a, n);
        const Vec3 left = glm::normalize(glm::cross(n, chord));
        double bulge = glm::dot(cursor - 0.5 * (a + b), left);
        if (tangent_) {
            if (auto t = tangentArc(a, *tangent_, b, n); t && std::abs(arcBulge(*t) - bulge) <= tolerance) {
                if (tangent) *tangent = true;
                return t;
            }
        }
        const double halfChord = 0.5 * glm::length(chord);
        if (std::abs(std::abs(bulge) - halfChord) <= tolerance) {
            bulge = bulge < 0.0 ? -halfChord : halfChord;
            if (half) *half = true;
        }
        return arcFromBulge(a, b, bulge, n);
    }
    case Mode::ThreePoint: return arcThroughPoints(points_[0], points_[1], cursor);
    case Mode::Pie: {
        const Vec3 c = points_[0];
        sweep_ = unwrapSweep(sweep_, signedAngle(points_[1] - c, cursor - c, n));
        return arcFromCenter(c, points_[1], sweep_, n);
    }
    }
    return Error{"Unknown arc tool."};
}

int ArcTool::segmentsFor(const ArcGeometry& arc) const {
    return segments_ > 0 ? segments_ : defaultArcSegments(arc.sweep);
}

void ArcTool::updatePreview(const Vec3& cursor, const PointerInput* input) {
    ArcPreview pv;
    pv.segments = segments_;
    if (points_.size() == 1) {
        pv.points = {points_[0], cursor};
        pv.value = mode_ == Mode::Pie ? ArcPreview::Value::Radius : ArcPreview::Value::Length;
        pv.amount = glm::length(cursor - points_[0]);
        editor().setPreview(std::move(pv));
        return;
    }
    bool tangent = false;
    bool half = false;
    const auto arc = arcFor(cursor, input, &tangent, &half);
    if (mode_ == Mode::TwoPoint) pv.guides.emplace_back(points_[0], points_[1]);
    if (!arc) {
        pv.error = arc.error();
        pv.points = mode_ == Mode::Pie ? std::vector<Vec3>{points_[0], points_[1]} : std::vector<Vec3>{points_[0], points_[1], cursor};
        editor().setPreview(std::move(pv));
        return;
    }
    pv.segments = segmentsFor(*arc);
    pv.points = arcPoints(*arc, pv.segments);
    if (mode_ == Mode::Pie) {
        pv.points.insert(pv.points.begin(), arc->center);
        pv.points.push_back(arc->center);
    }
    pv.radius = arc->radius;
    pv.tangent = tangent;
    pv.halfCircle = half;
    switch (mode_) {
    case Mode::TwoPoint:
        pv.value = ArcPreview::Value::Bulge;
        pv.amount = std::abs(arcBulge(*arc));
        break;
    case Mode::ThreePoint: pv.value = ArcPreview::Value::None; break;
    case Mode::Pie:
        pv.value = ArcPreview::Value::Angle;
        pv.amount = arc->sweep;
        break;
    }
    editor().setPreview(std::move(pv));
}

bool ArcTool::commit(const ArcGeometry& arc) {
    const int segments = segmentsFor(arc);
    const bool pie = mode_ == Mode::Pie;
    const bool ok = editor().commit(pie ? "Draw pie" : "Draw arc",
                                    [&arc, segments, pie](const Document& doc) { return drawArc(doc, std::nullopt, arc, segments, pie); });
    if (ok) {
        reset();
        ctx_.indicator.reset();
        editor().cancelPreview();
    }
    return ok;
}

std::optional<std::string> ArcTool::submit(std::string_view raw) {
    const std::string text = trimmed(raw);
    if (const auto count = segmentCount(text)) {
        if (auto e = checkArcSegments(*count)) return e;
        segments_ = *count;
        if (cursor_ && !points_.empty()) updatePreview(*cursor_, nullptr);
        editor().notify(Message{Message::Kind::Info, "Arcs now have " + std::to_string(*count) + " segments."});
        return std::nullopt;
    }
    if (points_.empty()) {
        return std::string(mode_ == Mode::Pie ? "Click the centre first, then type the radius."
                                              : "Click the start point first, then type the length.");
    }
    const LengthUnit unit = editor().document().displayUnit;
    const Vec3 n = plane_.normal;
    if (points_.size() == 1) {
        const auto len = parseLength(text, unit);
        if (!len) return len.error();
        if (*len < tol::kMinDimension) return std::string("The length must be at least 0.1 mm.");
        const Vec3 toward = cursor_ ? inPlane(*cursor_ - points_[0], n) : Vec3{0.0};
        if (!(glm::length(toward) > 0.0)) return std::string("Move the pointer toward the next point first, then type the length.");
        points_.push_back(points_[0] + glm::normalize(toward) * *len);
        sweep_ = 0.0;
        updatePreview(*cursor_, nullptr);
        return std::nullopt;
    }
    switch (mode_) {
    case Mode::TwoPoint: {
        const auto bulge = parseLength(text, unit);
        if (!bulge) return bulge.error();
        // The bulge goes to the side the pointer is on.
        const Vec3 a = points_[0];
        const Vec3 b = points_[1];
        const Vec3 left = glm::normalize(glm::cross(n, inPlane(b - a, n)));
        const double side = cursor_ && glm::dot(*cursor_ - 0.5 * (a + b), left) < 0.0 ? -1.0 : 1.0;
        const auto arc = arcFromBulge(a, b, side * std::abs(*bulge), n);
        if (!arc) return arc.error();
        if (!commit(*arc)) return ctx_.lastError("Could not draw the arc.");
        return std::nullopt;
    }
    case Mode::ThreePoint: return std::string("Click the end point, or type a segment count such as 12s.");
    case Mode::Pie: {
        const auto angle = parseAngle(text);
        if (!angle) return angle.error();
        const double sweep = sweep_ < 0.0 ? -std::abs(*angle) : std::abs(*angle);
        const auto arc = arcFromCenter(points_[0], points_[1], sweep, n);
        if (!arc) return arc.error();
        if (!commit(*arc)) return ctx_.lastError("Could not draw the pie.");
        return std::nullopt;
    }
    }
    return std::nullopt;
}

std::string ArcTool::hint() const {
    const std::size_t n = points_.size();
    switch (mode_) {
    case Mode::TwoPoint:
        if (n == 0) return "Arc: click the start point. Type 12s to set the number of segments.";
        if (n == 1) return "Arc: click the end point, or type the length.";
        if (tangent_) {
            return "Arc: move to set the bulge and click, or type it. Cyan means tangent to the edge; double-click the end for the tangent arc.";
        }
        return "Arc: move to set the bulge and click, or type it. Esc cancels.";
    case Mode::ThreePoint:
        if (n == 0) return "3 Point Arc: click the start point.";
        if (n == 1) return "3 Point Arc: click a point the arc passes through, or type the distance to it.";
        return "3 Point Arc: click the end point. Esc cancels.";
    case Mode::Pie:
        if (n == 0) return "Pie: click the centre.";
        if (n == 1) return "Pie: click the start of the arc, or type the radius.";
        return "Pie: click the end of the arc, or type the angle in degrees. Esc cancels.";
    }
    return {};
}

} // namespace pf
