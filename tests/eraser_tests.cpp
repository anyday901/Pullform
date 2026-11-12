// Eraser tool (E): click or drag over edges to erase them, Shift hides, Ctrl softens.
#include "helpers.hpp"
#include "app/tools/eraser_tool.hpp"
#include "app/tools/tool_controller.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/element_pick.hpp"
#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace pf;

namespace {

constexpr double kPixel = 1e-3; // radians per pixel

PointerInput lookAt(const Editor& editor, const Vec3& eye, const Vec3& at, Vec2 screen = {0, 0}) {
    PointerInput in = test::lookAtElements(editor, eye, at, kPixel);
    in.screen = screen;
    return in;
}

/// Looking down from above the point (x, y) on the ground.
PointerInput above(const Editor& editor, double x, double y, Vec2 screen = {0, 0}) {
    return lookAt(editor, {x + 0.05, y + 0.05, 10.0}, {x, y, 0.0}, screen);
}

/// Draws a line into the first group (or starts it), as the Line tool will.
void line(Editor& editor, const Vec3& a, const Vec3& b) {
    std::optional<ObjectId> target;
    if (!editor.document().objects.empty()) target = editor.document().objects.front()->id;
    REQUIRE(editor.commit("Line", [&](const Document& d) { return drawEdge(d, target, a, b); }));
}

/// A 2 x 1 m sheet split down the middle at x = 1: two coplanar faces sharing one edge.
ObjectId twoSquares(Editor& editor) {
    line(editor, {0, 0, 0}, {2, 0, 0});
    line(editor, {2, 0, 0}, {2, 1, 0});
    line(editor, {2, 1, 0}, {0, 1, 0});
    line(editor, {0, 1, 0}, {0, 0, 0});
    line(editor, {1, 0, 0}, {1, 1, 0});
    REQUIRE(editor.document().objects.size() == 1);
    const auto& o = *editor.document().objects.front();
    REQUIRE(o.isMeshGroup());
    REQUIRE(o.mesh->faces().size() == 2);
    return o.id;
}

/// A 3 x 1 m strip of three squares: inner edges at x = 1 and x = 2.
ObjectId threeSquares(Editor& editor) {
    line(editor, {0, 0, 0}, {3, 0, 0});
    line(editor, {3, 0, 0}, {3, 1, 0});
    line(editor, {3, 1, 0}, {0, 1, 0});
    line(editor, {0, 1, 0}, {0, 0, 0});
    line(editor, {1, 0, 0}, {1, 1, 0});
    line(editor, {2, 0, 0}, {2, 1, 0});
    return editor.document().objects.front()->id;
}

const mesh::Mesh& meshOf(const Editor& editor, ObjectId id) { return *editor.document().find(id)->mesh; }

std::optional<mesh::EdgeId> edgeAt(const mesh::Mesh& m, const Vec3& a, const Vec3& b) {
    const auto va = m.findVertex(a);
    const auto vb = m.findVertex(b);
    if (!va || !vb) return std::nullopt;
    return m.findEdge(*va, *vb);
}

/// A camera looking straight down from `eye`, projecting to screen pixels.
ScreenProjector downProjector(const Vec3& eye) {
    return [eye](const Vec3& p) -> std::optional<Vec2> {
        const double depth = eye.z - p.z;
        if (depth <= 0.0) return std::nullopt;
        return Vec2{(p.x - eye.x) / depth, (p.y - eye.y) / depth} * 1000.0;
    };
}

/// The pointer of that camera over the world point `at`.
PointerInput seen(const Editor& editor, const Vec3& eye, const Vec3& at) {
    const auto project = downProjector(eye);
    PointerInput in = lookAt(editor, eye, at, *project(at));
    in.project = project;
    return in;
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

} // namespace

TEST_CASE("Eraser: erasing the edge between two coplanar faces leaves one face") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = twoSquares(editor);
    const std::size_t depth = editor.undoDepth();
    tools.setTool(Tool::Eraser);
    CHECK(std::string(toolName(Tool::Eraser)) == "Eraser");

    // Hovering the middle edge highlights it.
    tools.pointerMove(above(editor, 1.0, 0.4));
    REQUIRE(editor.hoverElement());
    CHECK(editor.hoverElement()->kind == ElementKind::Edge);

    click(tools, above(editor, 1.0, 0.4));
    const auto& m = meshOf(editor, id);
    CHECK(m.faces().size() == 1);
    CHECK_FALSE(edgeAt(m, {1, 0, 0}, {1, 1, 0}));
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Erase 1 edge");
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    CHECK_FALSE(tools.active());

    // One undo brings both faces back.
    REQUIRE(editor.undo());
    CHECK(meshOf(editor, id).faces().size() == 2);
}

TEST_CASE("Eraser: a drag erases every edge it passes over in one commit") {
    Editor editor;
    ToolController tools(editor);
    // A 3 x 1 strip in three squares: two inner edges at x = 1 and x = 2.
    const ObjectId id = threeSquares(editor);
    REQUIRE(meshOf(editor, id).faces().size() == 3);
    const std::size_t depth = editor.undoDepth();

    tools.setTool(Tool::Eraser);
    tools.pointerDown(above(editor, 1.0, 0.5, {100, 100}));
    CHECK(tools.active());
    tools.pointerMove(above(editor, 1.5, 0.5, {150, 100})); // over a face: nothing added
    tools.pointerMove(above(editor, 2.0, 0.5, {200, 100}));
    // Nothing is erased until release; the preview shows the stroke.
    CHECK(meshOf(editor, id).faces().size() == 3);
    const auto* preview = std::get_if<ErasePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->edges.size() == 2);
    tools.pointerUp(above(editor, 2.0, 0.5, {200, 100}));

    CHECK(meshOf(editor, id).faces().size() == 1);
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Erase 2 edges");
}

TEST_CASE("Eraser: a fast drag keeps the edges passed over between two events") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = threeSquares(editor);
    const Vec3 eye{1.5, 0.5, 10.0};

    tools.setTool(Tool::Eraser);
    // Both events are on faces, far from any edge; the edges at x = 1 and 2 lie between.
    tools.pointerDown(seen(editor, eye, {0.5, 0.5, 0.0}));
    tools.pointerMove(seen(editor, eye, {2.5, 0.5, 0.0}));
    tools.pointerUp(seen(editor, eye, {2.5, 0.5, 0.0}));
    CHECK(meshOf(editor, id).faces().size() == 1);
    CHECK(editor.undoLabel() == "Erase 2 edges");
}

TEST_CASE("Eraser: edges hidden behind a face are not erased by a drag across it") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000,1000", "1000");
    const std::size_t depth = editor.undoDepth();
    const Vec3 eye{0.5, 0.5, 3.0};
    // Across the top face, inside its edges: the base edges at x = 0 and 1 are
    // under the pointer's path on screen but hidden by the top.
    tools.setTool(Tool::Eraser);
    tools.pointerDown(seen(editor, eye, {0.1, 0.5, 1.0}));
    tools.pointerMove(seen(editor, eye, {0.9, 0.5, 1.0}));
    const Vec2 from = *downProjector(eye)({0.1, 0.5, 1.0});
    CHECK(edgesCrossed(editor.document(), from, seen(editor, eye, {0.9, 0.5, 1.0})).empty());
    // Without the top face in the way they would count: the base's x = 0 edge does cross the path.
    CHECK(downProjector(eye)({0.0, 0.5, 0.0})->x > from.x);
    tools.pointerUp(seen(editor, eye, {0.9, 0.5, 1.0}));
    CHECK(editor.undoDepth() == depth);
    CHECK_FALSE(editor.document().find(box)->isMeshGroup());

    // The same drag reaching past the top's edge erases the top edge it crosses.
    tools.pointerDown(seen(editor, eye, {0.5, 0.5, 1.0}));
    tools.pointerMove(seen(editor, eye, {1.5, 0.5, 0.0}));
    tools.pointerUp(seen(editor, eye, {1.5, 0.5, 0.0}));
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Erase 1 edge");
}

TEST_CASE("Eraser: Shift hides and Ctrl softens instead, keeping the faces") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = twoSquares(editor);
    tools.setTool(Tool::Eraser);
    const auto middle = *edgeAt(meshOf(editor, id), {1, 0, 0}, {1, 1, 0});

    SUBCASE("Shift: hide") {
        auto in = above(editor, 1.0, 0.4);
        in.shift = true;
        click(tools, in);
        const auto& m = meshOf(editor, id);
        CHECK(m.faces().size() == 2);
        REQUIRE(m.edge(middle));
        CHECK(m.edge(middle)->hidden); // hidden, not softened: Unhide brings it back as it was
        CHECK_FALSE(m.edge(middle)->soft);
        CHECK_FALSE(m.edge(middle)->smooth);
        CHECK(editor.undoLabel() == "Hide 1 edge");
        // A hidden edge is no longer picked, so the Eraser cannot reach it.
        CHECK_FALSE(edgeUnder(editor.document(), above(editor, 1.0, 0.4)));
    }
    SUBCASE("Ctrl: soften") {
        auto in = above(editor, 1.0, 0.4);
        in.ctrl = true;
        click(tools, in);
        const auto& m = meshOf(editor, id);
        CHECK(m.faces().size() == 2);
        REQUIRE(m.edge(middle));
        CHECK(m.edge(middle)->soft);
        CHECK(m.edge(middle)->smooth);
        CHECK(editor.undoLabel() == "Soften 1 edge");
    }
}

TEST_CASE("Eraser: clicking a face or empty space changes nothing; Escape drops a stroke") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = twoSquares(editor);
    const std::size_t depth = editor.undoDepth();
    tools.setTool(Tool::Eraser);

    click(tools, above(editor, 0.5, 0.5)); // a face
    click(tools, above(editor, 5.0, 5.0)); // nothing
    CHECK(editor.undoDepth() == depth);

    tools.pointerDown(above(editor, 1.0, 0.4));
    CHECK(tools.active());
    CHECK(tools.cancel());
    CHECK_FALSE(tools.active());
    tools.pointerUp(above(editor, 1.0, 0.4));
    CHECK(editor.undoDepth() == depth);
    CHECK(meshOf(editor, id).faces().size() == 2);
}

TEST_CASE("Eraser: erasing a box edge removes the two faces that met there") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000,1000", "1000");
    tools.setTool(Tool::Eraser);
    // The top edge along y = 1, seen from above and in front.
    click(tools, lookAt(editor, {0.5, 3.0, 4.0}, {0.5, 1.0, 1.0}));
    const auto* o = editor.document().find(box);
    REQUIRE(o);
    CHECK(o->isMeshGroup());
    CHECK(o->mesh->faces().size() == 4);
    CHECK(o->mesh->edges().size() == 11);
}

TEST_CASE("Eraser: near a corner the pick goes to the corner's edge nearest the pointer") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = twoSquares(editor);
    tools.setTool(Tool::Eraser);
    // Right at the top end of the middle edge, slightly below it along the edge.
    const auto in = lookAt(editor, {1.0, 0.98, 10.0}, {1.0, 0.98, 0.0});
    REQUIRE(in.element);
    REQUIRE(in.element->element.kind == ElementKind::Vertex);
    const auto edge = edgeUnder(editor.document(), in);
    REQUIRE(edge);
    CHECK(edge->id == edgeAt(meshOf(editor, id), {1, 0, 0}, {1, 1, 0})->value);
}

TEST_CASE("eraseEdges and setEdgesSoft: one step for several groups") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "1000,1000", nullptr);
    const ObjectId b = test::makeBox(editor, tools, "1000,1000", nullptr, {5, 0});
    const std::vector<ElementRef> edges{{a, ElementKind::Edge, 1}, {b, ElementKind::Edge, 1}, {b, ElementKind::Edge, 1},
                                        {b, ElementKind::Face, 1}};
    const auto erased = eraseEdges(editor.document(), edges);
    REQUIRE(erased);
    CHECK(erased->objectIds == std::vector<ObjectId>{a, b});
    // A rectangle without one edge has no face left; its three edges stay.
    CHECK(erased->document.find(a)->mesh->faces().empty());
    CHECK(erased->document.find(a)->mesh->edges().size() == 3);

    const auto soft = setEdgesSoft(editor.document(), edges, true, true);
    REQUIRE(soft);
    CHECK(soft->document.find(b)->mesh->edge(mesh::EdgeId{1})->soft);
    CHECK(soft->document.find(b)->mesh->faces().size() == 1);

    CHECK_FALSE(eraseEdges(editor.document(), {{a, ElementKind::Face, 1}}));
    CHECK_FALSE(eraseEdges(editor.document(), {{a, ElementKind::Edge, 99}}));
}
