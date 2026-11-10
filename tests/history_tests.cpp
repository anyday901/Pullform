#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/solid.hpp"

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;

TEST_CASE("scenario 3: changing total height updates the same object and the cap stays selectable") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "750");
    const auto before = editor.document().objects.size();

    // Select the cap by picking it, then set an absolute total height.
    tools.setTool(Tool::Select);
    tools.pointerDown(downAt(editor, 1.0, 0.5));
    REQUIRE(editor.selection());
    CHECK(editor.selection()->objectId == id);
    CHECK(editor.selection()->face == FaceId::cap());
    REQUIRE_FALSE(tools.setTotalHeight(id, "900").has_value());

    CHECK(editor.document().objects.size() == before); // no second solid
    const auto* o = editor.document().find(id);
    REQUIRE(o);
    CHECK(o->extrusion->height == doctest::Approx(0.9));
    CHECK(objectBounds(*o).size().z == doctest::Approx(0.9));

    const auto hit = pickDocument(Ray{{0.4, 0.4, 5}, {0, 0, -1}}, editor.document());
    REQUIRE(hit);
    CHECK(hit->objectId == id);
    CHECK(hit->faceId == FaceId::cap());
    CHECK(hit->point.z == doctest::Approx(0.9));
    CHECK(editor.selection()->face == FaceId::cap());
}

TEST_CASE("scenario 4: undo and redo walk height change and extrusion") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "750");
    REQUIRE_FALSE(tools.setTotalHeight(id, "900").has_value());
    CHECK(editor.undoDepth() == 3); // draw, extrude, height

    REQUIRE(editor.undo());
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.75));
    REQUIRE(editor.undo());
    CHECK_FALSE(editor.document().find(id)->extrusion.has_value()); // planar rectangle again
    CHECK(editor.selection()->face == FaceId::profile());           // cap selection follows to the face
    CHECK(editor.redoDepth() == 2);

    REQUIRE(editor.redo());
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.75));
    REQUIRE(editor.redo());
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.9));
    CHECK_FALSE(editor.canRedo());
    CHECK(editor.document().find(id)->id == id);
}

TEST_CASE("a new edit after undo clears the redo branch") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "1000, 1000", "500");
    REQUIRE(editor.undo());
    CHECK(editor.canRedo());
    REQUIRE_FALSE(tools.submitPushDistance("300").has_value());
    CHECK_FALSE(editor.canRedo());
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(0.3));
}

TEST_CASE("scenario 5: cancelling previews leaves document and history unchanged") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", nullptr);
    const Document snapshot = editor.document();
    const auto depth = editor.undoDepth();

    SUBCASE("rectangle preview") {
        tools.setTool(Tool::Rectangle);
        tools.pointerDown(downAt(editor, 3.0, 3.0, {100, 100}));
        tools.pointerUp(downAt(editor, 3.0, 3.0, {100, 100}));
        for (int i = 0; i < 20; ++i) tools.pointerMove(downAt(editor, 3.0 + i * 0.1, 4.0, {110.0 + i, 120}));
        CHECK(std::holds_alternative<RectanglePreview>(editor.preview()));
        CHECK(tools.cancel());
    }
    SUBCASE("push/pull preview") {
        tools.setTool(Tool::PushPull);
        tools.pointerDown(downAt(editor, 1.0, 0.5, {100, 100}));
        for (int i = 1; i <= 20; ++i) tools.pointerMove(levelAt(1.0, 0.5, i * 0.05, {100, 100.0 - i * 5}));
        const auto* preview = std::get_if<PushPullPreview>(&editor.preview());
        REQUIRE(preview);
        CHECK(preview->height > 0.5);
        CHECK(tools.cancel());
    }
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    CHECK_FALSE(tools.active());
    CHECK(sameContent(editor.document(), snapshot));
    CHECK(editor.document().objects[0] == snapshot.objects[0]); // same immutable object, untouched
    CHECK(editor.undoDepth() == depth);
    CHECK_FALSE(editor.document().find(id)->extrusion.has_value());
}

TEST_CASE("scenario 6: delete one of two objects and undo keeps IDs and selection valid") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "1000, 1000", "500");
    const ObjectId b = test::makeBox(editor, tools, "500, 500", "300", {3.0, 3.0});
    REQUIRE(a != b);

    editor.select(Selection{b, FaceId::cap()});
    REQUIRE(editor.commit("Delete", [&](const Document& d) { return deleteObject(d, b); }));
    CHECK(editor.document().find(b) == nullptr);
    CHECK(editor.document().find(a) != nullptr);
    CHECK_FALSE(editor.selection().has_value()); // selection referenced the deleted object

    REQUIRE(editor.undo());
    REQUIRE(editor.document().find(b));
    CHECK(editor.document().find(b)->id == b);
    REQUIRE(editor.selection());
    CHECK(editor.selection()->objectId == b);
    CHECK(editor.document().find(a)->id == a);

    // Deleted IDs are never reused.
    REQUIRE(editor.redo());
    const ObjectId c = test::makeBox(editor, tools, "100, 100", nullptr, {6.0, 6.0});
    CHECK(c != a);
    CHECK(c != b);
    CHECK(c.value > b.value);
}

TEST_CASE("failed operations change nothing") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "1000, 1000", "500");
    const auto depth = editor.undoDepth();
    const Document before = editor.document();
    CHECK(tools.setTotalHeight(id, "0").has_value());
    CHECK(tools.setTotalHeight(id, "-100").has_value());
    CHECK(tools.setTotalHeight(id, "abc").has_value());
    CHECK(tools.submitPushDistance("-600").has_value()); // would cross the base
    CHECK_FALSE(editor.commit("bad", [](const Document& d) { return extrudeProfile(d, ObjectId{999}, 1.0); }));
    CHECK(editor.message()->kind == Message::Kind::Error);
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.undoDepth() == depth);
    // Setting the same height again is a no-op, not a history entry.
    CHECK_FALSE(tools.setTotalHeight(id, "500").has_value());
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("history is bounded and dirty state follows undo to the saved point") {
    Editor editor;
    ToolController tools(editor);
    CHECK_FALSE(editor.isDirty());
    const ObjectId id = test::makeBox(editor, tools, "1000, 1000", "500");
    CHECK(editor.isDirty());
    editor.markSaved();
    CHECK_FALSE(editor.isDirty());
    REQUIRE_FALSE(tools.setTotalHeight(id, "600").has_value());
    CHECK(editor.isDirty());
    editor.undo();
    CHECK_FALSE(editor.isDirty());
    editor.redo();
    CHECK(editor.isDirty());

    for (int i = 0; i < 260; ++i) {
        REQUIRE_FALSE(tools.setTotalHeight(id, std::to_string(700 + i)).has_value());
    }
    CHECK(editor.undoDepth() == Editor::kMaxHistory);
}

TEST_CASE("display unit changes are undoable edits") {
    Editor editor;
    REQUIRE(editor.commit("Units", [](const Document& d) { return setDisplayUnit(d, LengthUnit::Inch); }));
    CHECK(editor.document().displayUnit == LengthUnit::Inch);
    editor.undo();
    CHECK(editor.document().displayUnit == LengthUnit::Millimeter);
}
