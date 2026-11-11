#include "app/tools/eraser_tool.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
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

const std::vector<ElementRef>& EraserTool::stroke() const {
    static const std::vector<ElementRef> none;
    return stroke_ ? *stroke_ : none;
}

void EraserTool::collect(const PointerInput& input) {
    if (const auto edge = edgeUnder(editor().document(), input);
        edge && std::ranges::find(*stroke_, *edge) == stroke_->end()) {
        stroke_->push_back(*edge);
    }
    editor().setPreview(ErasePreview{*stroke_});
}

void EraserTool::pointerDown(const PointerInput& input) {
    stroke_.emplace();
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
