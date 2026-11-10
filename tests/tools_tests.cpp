// Scenario 10: the main workflow driven through the real tool state machine
// with synthetic pointer events (the same PointerInput the viewport produces).
#include "helpers.hpp"

#include "app/viewport/camera.hpp"
#include "core/commands/operations.hpp"
#include "core/geometry/solid.hpp"

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;

TEST_CASE("draw by click-click, push/pull by drag, edit the cap, delete, undo") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    tools.snap().step = 0.1;

    // Rectangle: click the first corner, move, click the second corner (any direction).
    tools.setTool(Tool::Rectangle);
    tools.pointerDown(downAt(editor, 2.03, 1.02, {300, 300}));
    tools.pointerUp(downAt(editor, 2.03, 1.02, {300, 300}));
    CHECK(tools.active());
    tools.pointerMove(downAt(editor, 0.5, 0.5, {200, 320}));
    tools.pointerMove(downAt(editor, 0.01, -0.02, {150, 350}));
    CHECK(editor.undoDepth() == 0); // moving only previews
    tools.pointerDown(downAt(editor, 0.01, -0.02, {150, 350}));
    tools.pointerUp(downAt(editor, 0.01, -0.02, {150, 350}));
    CHECK_FALSE(tools.active());
    REQUIRE(editor.document().objects.size() == 1);
    CHECK(editor.undoDepth() == 1);
    const auto& rect = *editor.document().objects[0];
    const ObjectId id = rect.id;
    // Snapped to the 100 mm grid: (0,0)..(2,1).
    CHECK(rect.transform.translation.x == doctest::Approx(0.0));
    CHECK(rect.transform.translation.y == doctest::Approx(0.0));
    CHECK(objectBounds(rect).size().x == doctest::Approx(2.0));
    CHECK(objectBounds(rect).size().y == doctest::Approx(1.0));

    // Push/pull: press on the face, drag up, release (one gesture, one entry).
    tools.setTool(Tool::PushPull);
    tools.pointerDown(downAt(editor, 1.0, 0.5, {200, 300}));
    for (int i = 1; i <= 15; ++i) tools.pointerMove(levelAt(1.0, 0.5, 0.05 * i, {200, 300.0 - 4 * i}));
    tools.pointerMove(levelAt(1.0, 0.5, 0.74, {200, 200}));
    tools.pointerUp(levelAt(1.0, 0.5, 0.74, {200, 200}));
    CHECK(editor.undoDepth() == 2);
    REQUIRE(editor.document().find(id)->extrusion);
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.7)); // snapped total height
    CHECK(editor.selection()->face == FaceId::cap());

    // Pick the cap again and change its height by click-move-click.
    tools.pointerDown(downAt(editor, 1.5, 0.8, {220, 260}));
    tools.pointerUp(downAt(editor, 1.5, 0.8, {220, 260}));
    tools.pointerMove(levelAt(1.5, 0.8, 1.21, {220, 240}));
    const auto* preview = std::get_if<PushPullPreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->startHeight == doctest::Approx(0.7));
    CHECK(preview->height == doctest::Approx(1.2));
    CHECK(preview->delta == doctest::Approx(0.5)); // incremental distance vs total height
    tools.pointerDown(levelAt(1.5, 0.8, 1.21, {220, 240}));
    tools.pointerUp(levelAt(1.5, 0.8, 1.21, {220, 240}));
    CHECK(editor.document().objects.size() == 1);
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(1.2));
    CHECK(editor.undoDepth() == 3);

    // Select with the select tool and delete the whole object.
    tools.setTool(Tool::Select);
    tools.pointerDown(downAt(editor, 1.0, 0.5));
    REQUIRE(editor.selection());
    REQUIRE(editor.commit("Delete", [&](const Document& d) { return deleteObject(d, editor.selection()->objectId); }));
    CHECK(editor.document().objects.empty());
    editor.undo();
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(1.2));

    // Clicking empty space clears the selection.
    tools.pointerDown(downAt(editor, 8.0, 8.0));
    CHECK_FALSE(editor.selection().has_value());
}

TEST_CASE("rectangle by press-drag-release and numeric entry after the first corner") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    tools.setTool(Tool::Rectangle);

    tools.pointerDown(downAt(editor, 1.0, 1.0, {100, 100}));
    tools.pointerMove(downAt(editor, 1.5, 1.7, {150, 160}));
    tools.pointerUp(downAt(editor, 1.5, 1.7, {150, 160}));
    REQUIRE(editor.document().objects.size() == 1);
    CHECK(objectBounds(*editor.document().objects[0]).size().x == doctest::Approx(0.5));

    // First corner, pointer toward -X/-Y, then exact dimensions follow the pointer direction.
    tools.pointerDown(downAt(editor, 5.0, 5.0, {300, 300}));
    tools.pointerUp(downAt(editor, 5.0, 5.0, {300, 300}));
    tools.pointerMove(downAt(editor, 4.0, 4.0, {250, 350}));
    REQUIRE_FALSE(tools.submitRectangleSize("2m", "100cm").has_value());
    const auto b = objectBounds(*editor.document().objects[1]);
    CHECK(b.min.x == doctest::Approx(3.0));
    CHECK(b.min.y == doctest::Approx(4.0));
    CHECK(b.max.x == doctest::Approx(5.0));
    CHECK(editor.undoDepth() == 2);

    // Mouse and numeric entry produce identical objects.
    tools.pointerDown(downAt(editor, 0.0, 0.0, {0, 0}));
    tools.pointerUp(downAt(editor, 0.0, 0.0, {0, 0}));
    tools.pointerDown(downAt(editor, 2.0, 1.0, {50, 50}));
    tools.pointerUp(downAt(editor, 2.0, 1.0, {50, 50}));
    REQUIRE_FALSE(tools.submitRectangleSize("2000, 1000").has_value());
    const auto& byMouse = *editor.document().objects[2];
    const auto& byNumber = *editor.document().objects[3];
    CHECK(byMouse.profile == byNumber.profile);
    CHECK(byMouse.transform == byNumber.transform);
}

TEST_CASE("invalid numeric input and degenerate clicks do not commit") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Rectangle);
    CHECK(tools.submitRectangleSize("2000").has_value());
    CHECK(tools.submitRectangleSize("0, 1000").has_value());
    CHECK(tools.submitRectangleSize("abc, 1000").has_value());
    CHECK(tools.submitRectangleSize("inf, 1000").has_value());
    CHECK(tools.submitRectangleSize("2000", "nan").has_value());
    CHECK(tools.submitRectangleSize("-2000, 1000").has_value());
    // Second click on the same grid point is degenerate: the first corner is kept.
    tools.pointerDown(downAt(editor, 1.0, 1.0, {10, 10}));
    tools.pointerUp(downAt(editor, 1.0, 1.0, {10, 10}));
    tools.pointerDown(downAt(editor, 1.01, 1.0, {11, 10}));
    CHECK(tools.active());
    CHECK(editor.message()->kind == Message::Kind::Error);
    CHECK(editor.document().objects.empty());
    CHECK(editor.undoDepth() == 0);
    CHECK(tools.submitPushDistance("100").has_value()); // nothing selected
}

TEST_CASE("side and base faces explain that push/pull is not supported yet") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "500");
    const auto depth = editor.undoDepth();
    tools.setTool(Tool::PushPull);
    PointerInput side;
    side.ray = Ray{{0.5, -5.0, 0.2}, {0, 1, 0}};
    side.pick = pickDocument(side.ray, editor.document());
    REQUIRE(side.pick);
    CHECK(side.pick->faceId == FaceId::sideAt(0));
    tools.pointerDown(side);
    CHECK_FALSE(tools.active());
    CHECK(editor.message()->text.find("not supported") != std::string::npos);
    CHECK(tools.submitPushDistance("100").has_value()); // selection targets a side face
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("push/pull below the base is rejected during the drag and the gesture survives") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", nullptr);
    tools.setTool(Tool::PushPull);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(levelAt(0.5, 0.5, -0.3, {100, 140}));
    const auto* preview = std::get_if<PushPullPreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->error.has_value());
    tools.pointerUp(levelAt(0.5, 0.5, -0.3, {100, 140}));
    CHECK(tools.active());
    CHECK(editor.undoDepth() == 1);
    tools.pointerMove(levelAt(0.5, 0.5, 0.4, {100, 60}));
    tools.pointerDown(levelAt(0.5, 0.5, 0.4, {100, 60}));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 2);
}

TEST_CASE("a zero-distance push/pull click is a no-op") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "500");
    tools.setTool(Tool::PushPull);
    const auto depth = editor.undoDepth();
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("workflow through camera rays") {
    // Same workflow, with rays generated by the real camera from pixel positions.
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    Camera camera;
    const Vec2 size{1280, 720};
    const auto at = [&](Vec2 px) {
        PointerInput in;
        in.ray = camera.rayThrough(px, size);
        in.pick = pickDocument(in.ray, editor.document());
        in.screen = px;
        return in;
    };
    tools.setTool(Tool::Rectangle);
    tools.pointerDown(at({600, 420}));
    tools.pointerUp(at({600, 420}));
    tools.pointerMove(at({760, 470}));
    tools.pointerDown(at({760, 470}));
    REQUIRE(editor.document().objects.size() == 1);
    const auto b = objectBounds(*editor.document().objects[0]);
    // The center of the screen-space rectangle lies on the drawn face.
    const auto mid = at({680, 445});
    REQUIRE(mid.pick);
    CHECK(mid.pick->faceId == FaceId::profile());
    CHECK(b.size().x > 0.0);
    CHECK(b.size().y > 0.0);

    tools.setTool(Tool::PushPull);
    tools.pointerDown(at({680, 445}));
    tools.pointerMove(at({680, 300}));
    tools.pointerUp(at({680, 300}));
    REQUIRE(editor.document().objects[0]->extrusion);
    CHECK(editor.document().objects[0]->extrusion->height > 0.0);
    CHECK(checkSolid(buildMesh(*editor.document().objects[0])).ok());
}
