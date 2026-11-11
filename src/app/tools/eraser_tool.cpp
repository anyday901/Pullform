#include "app/tools/eraser_tool.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pf {

namespace {

/// Angle (radians, small-angle) between the ray and the closest point of segment ab.
double rayAngleToSegment(const Ray& ray, const Vec3& a, const Vec3& b) {
    double t = closestParameterOnAxis(a, b - a, ray).value_or(0.0);
    t = std::clamp(t, 0.0, 1.0);
    const Vec3 v = a + (b - a) * t - ray.origin;
    const double along = glm::dot(v, ray.direction);
    if (along <= 0.0) return std::numeric_limits<double>::infinity();
    return glm::length(v - ray.direction * along) / along;
}

/// Parameters (s along p0-p1, u along q0-q1) where two screen segments cross, if they do.
std::optional<std::pair<double, double>> crossing(const Vec2& p0, const Vec2& p1, const Vec2& q0, const Vec2& q1) {
    const Vec2 r = p1 - p0;
    const Vec2 d = q1 - q0;
    const double denom = r.x * d.y - r.y * d.x;
    if (std::abs(denom) < 1e-12) return std::nullopt; // parallel: a pass along an edge is caught by the picks
    const Vec2 w = q0 - p0;
    const double s = (w.x * d.y - w.y * d.x) / denom;
    const double u = (w.x * r.y - w.y * r.x) / denom;
    if (s < 0.0 || s > 1.0 || u < 0.0 || u > 1.0) return std::nullopt;
    return std::pair{s, u};
}

std::string countText(std::size_t n, const char* verb) {
    return std::string(verb) + (n == 1 ? " 1 edge" : " " + std::to_string(n) + " edges");
}

} // namespace

EraseMode eraseMode(const PointerInput& input) {
    if (input.ctrl) return EraseMode::Soften;
    if (input.shift) return EraseMode::Hide;
    return EraseMode::Erase;
}

std::optional<ElementRef> edgeUnder(const Document& document, const PointerInput& input) {
    if (!input.element) return std::nullopt;
    const ElementRef& hit = input.element->element;
    const auto* object = document.find(hit.object);
    if (!object) return std::nullopt;
    if (hit.kind == ElementKind::Edge) return hit;
    if (hit.kind != ElementKind::Vertex) return std::nullopt;
    // Near a corner the picker prefers the vertex; the Eraser wants one of its edges.
    const auto topo = buildTopology(*object);
    const auto vertex = topo.indexOf(ElementKind::Vertex, hit.id);
    if (!vertex) return std::nullopt;
    std::optional<ElementRef> best;
    double bestAngle = std::numeric_limits<double>::infinity();
    for (const std::uint32_t e : topo.vertices[*vertex].edges) {
        const auto& edge = topo.edges[e];
        if (edge.soft) continue;
        const double angle = rayAngleToSegment(input.ray, topo.vertices[edge.a].position, topo.vertices[edge.b].position);
        if (angle < bestAngle) {
            bestAngle = angle;
            best = ElementRef{hit.object, ElementKind::Edge, edge.id};
        }
    }
    return best;
}

std::vector<ElementRef> edgesCrossed(const Document& document, const Vec2& from, const PointerInput& input) {
    std::vector<ElementRef> found;
    if (!input.project || glm::length(input.screen - from) < 1.0) return found;
    const Vec3 eye = input.ray.origin;
    for (const auto& object : document.objects) {
        const auto topo = buildTopology(*object);
        std::vector<std::optional<Vec2>> screen;
        screen.reserve(topo.vertices.size());
        for (const auto& v : topo.vertices) screen.push_back(input.project(v.position));
        for (const auto& e : topo.edges) {
            if (e.soft || !screen[e.a] || !screen[e.b]) continue;
            const auto hit = crossing(from, input.screen, *screen[e.a], *screen[e.b]);
            if (!hit) continue;
            // Screen-linear is not world-linear under perspective, but close
            // enough to tell a visible edge from one behind a face.
            const Vec3 a = topo.vertices[e.a].position;
            const Vec3 point = a + (topo.vertices[e.b].position - a) * hit->second;
            const double distance = glm::length(point - eye);
            if (distance <= 0.0) continue;
            const auto blocker = pickDocument(Ray{eye, (point - eye) / distance}, document);
            if (blocker && blocker->distance < distance * (1.0 - 1e-3)) continue;
            found.push_back(ElementRef{object->id, ElementKind::Edge, e.id});
        }
    }
    return found;
}

const std::vector<ElementRef>& EraserTool::stroke() const {
    static const std::vector<ElementRef> none;
    return stroke_ ? *stroke_ : none;
}

void EraserTool::add(const ElementRef& edge) {
    if (std::ranges::find(*stroke_, edge) == stroke_->end()) stroke_->push_back(edge);
}

void EraserTool::collect(const PointerInput& input) {
    if (last_) {
        for (const auto& edge : edgesCrossed(editor().document(), *last_, input)) add(edge);
    }
    if (const auto edge = edgeUnder(editor().document(), input)) add(*edge);
    last_ = input.screen;
    editor().setPreview(ErasePreview{*stroke_});
}

void EraserTool::pointerDown(const PointerInput& input) {
    stroke_.emplace();
    last_.reset();
    editor().setHoverElement(std::nullopt);
    collect(input);
}

void EraserTool::pointerMove(const PointerInput& input) {
    if (stroke_) {
        collect(input);
        return;
    }
    editor().setHoverElement(edgeUnder(editor().document(), input));
}

void EraserTool::pointerUp(const PointerInput& input) {
    if (!stroke_) return;
    collect(input);
    const std::vector<ElementRef> edges = std::move(*stroke_);
    stroke_.reset();
    last_.reset();
    editor().cancelPreview();
    if (edges.empty()) return; // nothing touched: no-op, no history
    const EraseMode mode = eraseMode(input);
    const std::size_t n = edges.size();
    bool ok = false;
    std::string done;
    switch (mode) {
    case EraseMode::Erase:
        ok = editor().commit(countText(n, "Erase"), [&edges](const Document& d) { return eraseEdges(d, edges); });
        done = countText(n, "Erased");
        break;
    case EraseMode::Hide:
        ok = editor().commit(countText(n, "Hide"), [&edges](const Document& d) { return setEdgesSoft(d, edges, true, false); });
        done = countText(n, "Hid");
        break;
    case EraseMode::Soften:
        ok = editor().commit(countText(n, "Soften"), [&edges](const Document& d) { return setEdgesSoft(d, edges, true, true); });
        done = countText(n, "Softened");
        break;
    }
    if (ok) editor().notify(Message{Message::Kind::Info, done + "."});
}

std::optional<std::string> EraserTool::submit(std::string_view /*text*/) {
    return std::string("The Eraser takes no typed value. Click or drag over edges to erase them.");
}

std::string EraserTool::hint() const {
    if (active()) {
        return "Eraser: drag over more edges and release to erase them (Shift: hide, Ctrl: soften). Esc cancels.";
    }
    return "Eraser: click an edge, or drag over several, to erase it. Coplanar faces on both sides merge into one. "
           "Hold Shift to hide edges instead, Ctrl to soften them.";
}

} // namespace pf
