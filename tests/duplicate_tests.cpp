#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/geometry/solid.hpp"

using namespace pf;
using pf::test::downAt;

TEST_CASE("duplicateObject copies with a new stable ID and an offset") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "2000, 1000", "750");
    const Document before = editor.document();

    const auto r = duplicateObject(before, a, {3.0, -1.0, 0.5});
    REQUIRE(r);
    REQUIRE(r->objectId);
    const ObjectId b = *r->objectId;
    CHECK(b != a);
    CHECK(b.value == before.nextId);
    CHECK(r->document.nextId == before.nextId + 1);
    CHECK(r->document.objects.size() == 2);

    const auto* orig = r->document.find(a);
    const auto* copy = r->document.find(b);
    REQUIRE(orig);
    REQUIRE(copy);
    CHECK(*orig == *before.find(a));
    CHECK(copy->profile == orig->profile);
    CHECK(copy->extrusion == orig->extrusion);
    CHECK(copy->transform.translation == orig->transform.translation + Vec3{3.0, -1.0, 0.5});
    CHECK(copy->name == "Box " + std::to_string(b.value));
    CHECK(sameContent(editor.document(), before)); // pure: input untouched
}

TEST_CASE("duplicate names follow the new ID or get a copy suffix") {
    CHECK(duplicateName("Box 3", ObjectId{7}) == "Box 7");
    CHECK(duplicateName("Rectangle 12", ObjectId{40}) == "Rectangle 40");
    CHECK(duplicateName("Shed wall", ObjectId{5}) == "Shed wall copy");
    CHECK(duplicateName("Box 3b", ObjectId{5}) == "Box 3b copy");
    CHECK(duplicateName("Box ", ObjectId{5}) == "Box  copy");
    CHECK(duplicateName("", ObjectId{5}) == "Copy");
}

TEST_CASE("duplicate rejects missing objects and out-of-range placements") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "1000, 1000", "500");
    CHECK_FALSE(duplicateObject(editor.document(), ObjectId{999}, {}));
    CHECK_FALSE(duplicateObject(editor.document(), a, {20000.0, 0.0, 0.0}));
    CHECK_FALSE(duplicateObject(editor.document(), a, {std::nan(""), 0.0, 0.0}));

    const auto depth = editor.undoDepth();
    const Document before = editor.document();
    CHECK_FALSE(editor.commit("Duplicate", [a](const Document& d) { return duplicateObject(d, a, {1e9, 0, 0}); }));
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("editing the copy leaves the original unchanged, and vice versa") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "2000, 1000", "750");
    editor.select(Selection{a, FaceId::cap()});
    REQUIRE_FALSE(tools.duplicateSelection().has_value());
    REQUIRE(editor.selection());
    const ObjectId b = editor.selection()->objectId;
    CHECK(b != a);
    CHECK(editor.selection()->face == FaceId::cap()); // copy selected, same face

    REQUIRE_FALSE(tools.setTotalHeight(b, "1200").has_value());
    CHECK(editor.document().find(b)->extrusion->height == doctest::Approx(1.2));
    CHECK(editor.document().find(a)->extrusion->height == doctest::Approx(0.75));

    REQUIRE_FALSE(tools.setTotalHeight(a, "300").has_value());
    CHECK(editor.document().find(a)->extrusion->height == doctest::Approx(0.3));
    CHECK(editor.document().find(b)->extrusion->height == doctest::Approx(1.2));

    REQUIRE(editor.commit("Delete", [a](const Document& d) { return deleteObject(d, a); }));
    REQUIRE(editor.document().find(b));
    CHECK(editor.document().find(b)->extrusion->height == doctest::Approx(1.2));
}

TEST_CASE("Duplicate places the copy beside the original, pickable, with one undo entry") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "2000, 1000", "750");
    editor.select(Selection{a, std::nullopt});
    const auto depth = editor.undoDepth();
    const Document before = editor.document();

    REQUIRE_FALSE(tools.duplicateSelection().has_value());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Duplicate " + idString(a));
    const ObjectId b = editor.selection()->objectId;
    const auto* copy = editor.document().find(b);
    REQUIRE(copy);
    CHECK(copy->transform.translation == Vec3{2.0, 0.0, 0.0}); // one width over, touching
    CHECK(editor.message()->kind == Message::Kind::Info);

    // The copy picks as itself; the original still picks as the original.
    const auto hitCopy = pickDocument(Ray{{3.0, 0.5, 5}, {0, 0, -1}}, editor.document());
    REQUIRE(hitCopy);
    CHECK(hitCopy->objectId == b);
    CHECK(hitCopy->faceId == FaceId::cap());
    const auto hitOrig = pickDocument(Ray{{1.0, 0.5, 5}, {0, 0, -1}}, editor.document());
    REQUIRE(hitOrig);
    CHECK(hitOrig->objectId == a);

    // Repeating builds a row.
    REQUIRE_FALSE(tools.duplicateSelection().has_value());
    CHECK(editor.document().find(editor.selection()->objectId)->transform.translation == Vec3{4.0, 0.0, 0.0});

    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.selection()->objectId == a);
    REQUIRE(editor.redo());
    CHECK(editor.document().find(b));
    CHECK(editor.selection()->objectId == b);
}

TEST_CASE("Duplicate without a selection does nothing") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "500");
    editor.select(std::nullopt);
    const auto depth = editor.undoDepth();
    CHECK(tools.duplicateSelection().has_value());
    CHECK(editor.undoDepth() == depth);
    CHECK(editor.document().objects.size() == 1);
}

TEST_CASE("a duplicated planar face stays planar") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "500, 500", nullptr);
    editor.select(Selection{a, FaceId::profile()});
    REQUIRE_FALSE(tools.duplicateSelection().has_value());
    const ObjectId b = editor.selection()->objectId;
    CHECK_FALSE(editor.document().find(b)->extrusion.has_value());
    CHECK(editor.document().find(b)->name == "Rectangle " + std::to_string(b.value));

}

TEST_CASE("a copy saves and reopens with its own ID") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "1000, 2000", "400");
    editor.select(Selection{a, std::nullopt});
    REQUIRE_FALSE(tools.duplicateSelection().has_value());
    const ObjectId b = editor.selection()->objectId;

    const auto loaded = parseProject(serializeProject(editor.document(), std::nullopt));
    REQUIRE(loaded);
    CHECK(sameContent(loaded->document, editor.document()));
    REQUIRE(loaded->document.find(b));
    CHECK(loaded->document.find(b)->transform.translation == Vec3{1.0, 0.0, 0.0});
    CHECK(loaded->document.nextId == editor.document().nextId);
}

// ---- Copy while moving (Move tool copy mode) --------------------------------

TEST_CASE("Move in copy mode places a copy at the destination and leaves the original") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "2000, 1000", "750");
    const ProfileObject original = *editor.document().find(a);
    const auto depth = editor.undoDepth();

    tools.setTool(Tool::Move);
    tools.toggleMoveCopy();
    CHECK(tools.moveCopy());
    CHECK(tools.hint().find("Copying") != std::string::npos);
    tools.toggleAxisLock(MoveAxis::X);
    REQUIRE_FALSE(tools.submitMoveDistance("3000").has_value());

    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Copy 3000 mm along X");
    CHECK(editor.document().objects.size() == 2);
    CHECK(*editor.document().find(a) == original); // original did not move
    const ObjectId b = editor.selection()->objectId;
    CHECK(b != a);
    CHECK(editor.document().find(b)->transform.translation == Vec3{3.0, 0.0, 0.0});
    CHECK_FALSE(tools.moveCopy()); // copy mode ends with the commit, like the axis lock

    REQUIRE(editor.undo());
    CHECK(editor.document().objects.size() == 1);
    CHECK(*editor.document().find(a) == original);
}

TEST_CASE("drag-copy: the preview keeps the original, toggling mid-drag works, zero distance copies nothing") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    tools.snap().step = 0.1;
    const ObjectId a = test::makeBox(editor, tools, "1000, 1000", "500");
    const auto depth = editor.undoDepth();
    tools.setTool(Tool::Move);

    // Zero-distance copy gesture is a no-op.
    tools.toggleMoveCopy();
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    CHECK(editor.undoDepth() == depth);
    CHECK(editor.document().objects.size() == 1);
    tools.toggleMoveCopy(); // back off
    CHECK_FALSE(tools.moveCopy());

    // Start a plain move, switch to copy part-way through the drag.
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(downAt(editor, 1.5, 0.5, {150, 100}));
    const auto* p = std::get_if<MovePreview>(&editor.preview());
    REQUIRE(p);
    CHECK_FALSE(p->copy);
    tools.toggleMoveCopy();
    p = std::get_if<MovePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(p->copy);
    tools.pointerMove(downAt(editor, 2.52, 0.5, {200, 100}));
    tools.pointerUp(downAt(editor, 2.52, 0.5, {200, 100}));

    CHECK(editor.undoDepth() == depth + 1);
    REQUIRE(editor.document().objects.size() == 2);
    CHECK(editor.document().find(a)->transform.translation == Vec3{0.0});
    const ObjectId b = editor.selection()->objectId;
    CHECK(b != a);
    CHECK(editor.document().find(b)->transform.translation.x == doctest::Approx(2.0));
    CHECK(editor.selection()->face == FaceId::cap()); // picked on the cap; the copy keeps it

    // Editing the copy leaves the original unchanged.
    REQUIRE_FALSE(tools.setTotalHeight(b, "900").has_value());
    CHECK(editor.document().find(a)->extrusion->height == doctest::Approx(0.5));
}

TEST_CASE("Move copy mode clears on tool change and an out-of-range copy is refused") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "500");
    tools.setTool(Tool::Move);
    tools.toggleMoveCopy();
    tools.setTool(Tool::Select);
    tools.setTool(Tool::Move);
    CHECK_FALSE(tools.moveCopy());

    tools.toggleMoveCopy();
    const auto depth = editor.undoDepth();
    const Document before = editor.document();
    CHECK(tools.submitMoveDistance("20000000, 0, 0").has_value()); // 20 km
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.undoDepth() == depth);
    CHECK(tools.moveCopy()); // still armed for the next try
}
