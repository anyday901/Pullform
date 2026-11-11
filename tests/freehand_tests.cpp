#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/freehand.hpp"

#include <cmath>
#include <numbers>
#include <vector>

using namespace pf;
using namespace pf::test;

namespace {

/// Points round a circle of radius `r` about (cx, cy) at height z, starting at
/// angle 0 and stopping `gap` radians short of a full turn.
std::vector<Vec3> circle(double cx, double cy, double z, double r, int count, double gap = 0.0) {
    std::vector<Vec3> pts;
    for (int i = 0; i < count; ++i) {
        const double a = (2.0 * std::numbers::pi - gap) * i / (count - 1);
        pts.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a), z);
    }
    return pts;
}

/// Drags the current tool through `pts` (each a ray straight down), pressing at
/// the first and releasing at the last. Screen positions step one pixel per point.
void drag(const Editor& editor, ToolController& tools, const std::vector<Vec3>& pts) {
    tools.pointerDown(downAt(editor, pts.front().x, pts.front().y, {0, 0}));
    for (std::size_t i = 1; i < pts.size(); ++i) {
        tools.pointerMove(downAt(editor, pts[i].x, pts[i].y, {static_cast<double>(i), 0}));
    }
    tools.pointerUp(downAt(editor, pts.back().x, pts.back().y, {static_cast<double>(pts.size()), 0}));
}

} // namespace

TEST_CASE("freehand: a noisy straight stroke simplifies to one segment with exact ends") {
    std::vector<Vec3> raw;
    for (int i = 0; i <= 200; ++i) raw.push_back({i * 0.01, (i % 2 ? 0.0005 : -0.0005), 0.0});
    const auto out = freehand::smoothStroke(raw, 0.005, false);
    REQUIRE(out.size() == 2);
    CHECK(out.front() == raw.front());
    CHECK(out.back() == raw.back());
}

TEST_CASE("freehand: a closed circle keeps its shape with far fewer points") {
    const auto raw = circle(0, 0, 0, 1.0, 400, 0.01);
    const double tolerance = 0.005;
    const auto out = freehand::smoothStroke(raw, tolerance, true);
    CHECK(out.size() >= 8);
    CHECK(out.size() < 100);
    CHECK(out.front() == raw.front()); // the start stays put (it may be snapped)
    CHECK(glm::length(out.back() - out.front()) > tolerance); // not repeated at the end
    for (const Vec3& p : out) CHECK(std::abs(glm::length(p) - 1.0) < 3.0 * tolerance);
}

TEST_CASE("freehand: sparse samples keep their corners within the tolerance") {
    const std::vector<Vec3> raw{{0, 0, 0}, {1, 1, 0}, {2, 0, 0}};
    const double tolerance = 0.005;
    const auto out = freehand::smoothStroke(raw, tolerance, false);
    REQUIRE(out.size() == 3);
    CHECK(out.front() == raw.front());
    CHECK(out.back() == raw.back());
    CHECK(glm::length(out[1] - raw[1]) <= tolerance);
}

TEST_CASE("freehand: an open stroke that comes back near its start ends at the release point") {
    const std::vector<Vec3> raw{{0, 0, 0}, {0.03, 0, 0}, {0.001, 0, 0}};
    const auto out = freehand::smoothStroke(raw, 0.005, false);
    REQUIRE(out.size() == 3);
    CHECK(out.back() == raw.back());
}

TEST_CASE("freehand: degenerate strokes give nothing") {
    CHECK(freehand::smoothStroke({}, 0.01, false).empty());
    CHECK(freehand::smoothStroke({{0, 0, 0}, {0.001, 0, 0}}, 0.01, false).empty());
    CHECK(freehand::smoothStroke({{0, 0, 0}, {1, 0, 0}}, 0.0, false).empty());
    CHECK(freehand::smoothStroke({{0, 0, 0}, {1, 0, 0}, {0, 0, 0}}, 0.01, true).empty());
    // A long thin loop simplifies to a line, but a loop needs three points.
    const auto thin = freehand::smoothStroke({{0, 0, 0}, {1, 0.001, 0}, {2, 0, 0}, {1, -0.001, 0}}, 0.01, true);
    CHECK(thin.size() == 3);
}

TEST_CASE("freehand: only a stroke that went somewhere and came back closes") {
    CHECK(freehand::closesLoop(circle(0, 0, 0, 1.0, 100, 0.01), 0.05));
    CHECK_FALSE(freehand::closesLoop(circle(0, 0, 0, 1.0, 100, 1.0), 0.05)); // stopped short
    // A wiggle near the start never left it.
    CHECK_FALSE(freehand::closesLoop({{0, 0, 0}, {0.02, 0, 0}, {0.03, 0.01, 0}, {0.01, 0, 0}}, 0.05));
}

TEST_CASE("drawPolyline: a closed loop is one face in one group and one undo entry") {
    Editor editor;
    const std::vector<Vec3> square{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}};
    REQUIRE(editor.commit("Freehand", [&](const Document& doc) { return drawPolyline(doc, std::nullopt, square, true); }));
    CHECK(editor.undoDepth() == 1);
    REQUIRE(editor.document().objects.size() == 1);
    const auto& m = *editor.document().objects[0]->mesh;
    CHECK(m.edges().size() == 4);
    CHECK(m.faces().size() == 1);

    // Open, it is just edges.
    REQUIRE(editor.commit("Freehand", [&](const Document& doc) {
        return drawPolyline(doc, std::nullopt, {{5, 0, 0}, {6, 0, 0}, {6, 1, 0}}, false);
    }));
    REQUIRE(editor.document().objects.size() == 2);
    CHECK(editor.document().objects[1]->mesh->edges().size() == 2);
    CHECK(editor.document().objects[1]->mesh->faces().empty());

    // Nothing to draw is an error and leaves history alone.
    CHECK_FALSE(editor.commit("Freehand", [&](const Document& doc) { return drawPolyline(doc, std::nullopt, {{1, 1, 0}}, false); }));
    CHECK(editor.undoDepth() == 2);
}

TEST_CASE("freehand tool: dragging a loop on the ground makes one face; undo removes it") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Freehand);
    drag(editor, tools, circle(3, 3, 0, 1.0, 120, 0.005));
    CHECK_FALSE(tools.active());
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    REQUIRE(editor.document().objects.size() == 1);
    CHECK(editor.undoDepth() == 1);
    CHECK(editor.undoLabel() == "Freehand");
    const auto& m = *editor.document().objects[0]->mesh;
    CHECK(m.faces().size() == 1);
    CHECK(m.edges().size() >= 8);
    CHECK(m.edges().size() == m.vertices().size()); // a single closed loop
    REQUIRE(editor.undo());
    CHECK(editor.document().objects.empty());
}

TEST_CASE("freehand tool: an open stroke is a chain of edges with no face") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Freehand);
    drag(editor, tools, circle(0, 0, 0, 1.0, 60, std::numbers::pi)); // a half circle
    REQUIRE(editor.document().objects.size() == 1);
    const auto& m = *editor.document().objects[0]->mesh;
    CHECK(m.faces().empty());
    CHECK(m.edges().size() >= 3);
    CHECK(m.vertices().size() == m.edges().size() + 1);
}

TEST_CASE("freehand tool: a stroke started on a box top is drawn on the top, into the box") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = makeBox(editor, tools, "4000, 4000", "1000");
    const std::size_t before = editor.undoDepth();
    tools.setTool(Tool::Freehand);
    drag(editor, tools, circle(2, 2, 1, 0.5, 80, 0.005));
    CHECK(editor.undoDepth() == before + 1);
    REQUIRE(editor.document().objects.size() == 1); // stuck to the box
    const ProfileObject& o = *editor.document().objects[0];
    CHECK(o.id == box);
    REQUIRE(o.isMeshGroup());
    // Box: 8 corners, plus the loop's vertices, all at the top.
    int top = 0;
    for (const auto& [id, v] : o.mesh->vertices()) {
        if (near(v.position.z, 1.0, 1e-9)) ++top;
    }
    CHECK(top > 4 + 8);
    CHECK(o.mesh->faces().size() > 6); // the loop made a face on the top
}

TEST_CASE("freehand tool: a click, or Escape mid-stroke, draws nothing") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Freehand);
    tools.pointerDown(downAt(editor, 1, 1, {0, 0}));
    CHECK(tools.active());
    tools.pointerUp(downAt(editor, 1, 1, {0, 0}));
    CHECK_FALSE(tools.active());
    CHECK(editor.document().objects.empty());

    tools.pointerDown(downAt(editor, 1, 1, {0, 0}));
    tools.pointerMove(downAt(editor, 2, 1, {20, 0}));
    CHECK(std::holds_alternative<FreehandPreview>(editor.preview()));
    CHECK(tools.cancel());
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    tools.pointerUp(downAt(editor, 3, 1, {40, 0}));
    CHECK(editor.document().objects.empty());
    CHECK(editor.undoDepth() == 0);
    CHECK(tools.submit("100").has_value());
}
