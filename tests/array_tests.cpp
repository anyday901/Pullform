// Array copies (roadmap item 16): "5x" and "/5" after a Move or Rotate copy.
#include "helpers.hpp"

#include "app/tools/tool_support.hpp"
#include "core/commands/operations.hpp"
#include "core/document/units.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace pf;
using pf::test::downAt;
using pf::test::near;

namespace {

/// Base centres of every object, sorted by x then y.
std::vector<Vec3> centres(const Document& doc) {
    std::vector<Vec3> out;
    for (const auto& o : doc.objects) out.push_back(baseCenter(*o));
    std::ranges::sort(out, [](const Vec3& a, const Vec3& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    return out;
}

/// A 100 mm square post, 1200 mm tall, centred on (0.05, 0.05).
ObjectId makePost(Editor& editor, ToolController& tools) { return test::makeBox(editor, tools, "100, 100", "1200"); }

/// Move-copies the selection 2000 mm along X with typed entry.
void copyAlongX(ToolController& tools, const char* distance = "2000") {
    tools.setTool(Tool::Move);
    tools.toggleMoveCopy();
    tools.toggleAxisLock(MoveAxis::X);
    REQUIRE_FALSE(tools.submitMoveDistance(distance).has_value());
}

} // namespace

TEST_CASE("parseArrayCount reads 5x, x5, *5 and /5, and leaves distances alone") {
    for (const char* text : {"5x", "x5", "*5", "5*", " 5 X ", "5 x"}) {
        const auto a = parseArrayCount(text);
        REQUIRE(a);
        REQUIRE(*a);
        CHECK((*a)->count == 5);
        CHECK_FALSE((*a)->divide);
    }
    const auto d = parseArrayCount("/12");
    REQUIRE(d);
    REQUIRE(*d);
    CHECK((*d)->count == 12);
    CHECK((*d)->divide);
    for (const char* text : {"500", "2m", "1, 2, 3", "x", "45", "6.5'"}) CHECK_FALSE(parseArrayCount(text).has_value());
    for (const char* text : {"0x", "2.5x", "/0", "-3x", "1001x", "5mx"}) {
        const auto bad = parseArrayCount(text);
        REQUIRE(bad);
        CHECK_FALSE(*bad);
    }
}

TEST_CASE("arrayObjects and radialArrayObjects place every copy") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = makePost(editor, tools);
    const auto line = arrayObjects(editor.document(), {a}, {1.0, 0.0, 0.0}, 3);
    REQUIRE(line);
    REQUIRE(line->objectIds.size() == 3);
    CHECK(line->objectId == line->objectIds.back());
    for (int i = 0; i < 3; ++i) {
        CHECK(near(line->document.find(line->objectIds[static_cast<std::size_t>(i)])->transform.translation.x, i + 1.0));
    }
    // Four quarter turns about the origin from (2.05, 0.05): every quadrant.
    const auto turned = test::makeBox(editor, tools, "100, 100", "500", {2.0, 0.0});
    const auto ring = radialArrayObjects(editor.document(), {turned}, Vec3{0.0}, {0.0, 0.0, 1.0}, std::numbers::pi / 2.0, 3);
    REQUIRE(ring);
    REQUIRE(ring->objectIds.size() == 3);
    const Vec3 c1 = baseCenter(*ring->document.find(ring->objectIds[0]));
    const Vec3 c3 = baseCenter(*ring->document.find(ring->objectIds[2]));
    CHECK(near(c1.x, -0.05, 1e-9));
    CHECK(near(c1.y, 2.05, 1e-9));
    CHECK(near(c3.x, 0.05, 1e-9));
    CHECK(near(c3.y, -2.05, 1e-9));
    CHECK_FALSE(arrayObjects(editor.document(), {a}, {1.0, 0.0, 0.0}, 0));
    CHECK_FALSE(arrayObjects(editor.document(), {a}, {1.0, 0.0, 0.0}, kMaxArrayCount + 1));
}

TEST_CASE("Accept: a fence of 10 posts from one post in two commands") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId post = makePost(editor, tools);
    const auto depth = editor.undoDepth();

    copyAlongX(tools);                                       // command 1: Move copy 2000 mm along X
    REQUIRE_FALSE(tools.submitMoveDistance("9x").has_value()); // command 2: nine copies at that spacing

    REQUIRE(editor.document().objects.size() == 10);
    const auto c = centres(editor.document());
    for (std::size_t i = 0; i < c.size(); ++i) {
        CHECK(near(c[i].x, 0.05 + 2.0 * static_cast<double>(i), 1e-9));
        CHECK(near(c[i].y, 0.05, 1e-9));
    }
    // One undo entry for the whole fence, labelled as an array.
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Array 9 copies 2000 mm apart along X");
    // The farthest post is selected, so the fence can be extended from it.
    REQUIRE(editor.selection());
    CHECK(near(baseCenter(*editor.document().find(editor.selection()->objectId)).x, 18.05, 1e-9));
    CHECK(editor.selectedObjects().size() == 1);

    REQUIRE(editor.undo());
    CHECK(editor.document().objects.size() == 1);
    CHECK(editor.document().find(post));
    REQUIRE(editor.redo());
    CHECK(editor.document().objects.size() == 10);
}

TEST_CASE("/n spreads the copies between, and the array stays editable until the next action") {
    Editor editor;
    ToolController tools(editor);
    makePost(editor, tools);
    const auto depth = editor.undoDepth();
    copyAlongX(tools, "10000");
    CHECK(tools.hint().find("5x") != std::string::npos);

    REQUIRE_FALSE(tools.submitMoveDistance("/5").has_value());
    auto c = centres(editor.document());
    REQUIRE(c.size() == 6);
    for (std::size_t i = 0; i < c.size(); ++i) CHECK(near(c[i].x, 0.05 + 2.0 * static_cast<double>(i), 1e-9));
    CHECK(editor.undoLabel() == "Array 5 copies 2000 mm apart along X");

    // Changing the count again replaces the array (still one entry).
    REQUIRE_FALSE(tools.submitMoveDistance("/2").has_value());
    c = centres(editor.document());
    REQUIRE(c.size() == 3);
    CHECK(near(c[1].x, 5.05, 1e-9));
    CHECK(near(c[2].x, 10.05, 1e-9));

    // A new distance keeps "/2" and re-spaces over the new span.
    REQUIRE_FALSE(tools.submitMoveDistance("4000").has_value());
    c = centres(editor.document());
    REQUIRE(c.size() == 3);
    CHECK(near(c[2].x, 4.05, 1e-9));
    // Back to a multiple: 3 copies 4 m apart.
    REQUIRE_FALSE(tools.submitMoveDistance("3x").has_value());
    c = centres(editor.document());
    REQUIRE(c.size() == 4);
    CHECK(near(c[3].x, 12.05, 1e-9));
    // "1x" is the plain copy again.
    REQUIRE_FALSE(tools.submitMoveDistance("1x").has_value());
    CHECK(editor.document().objects.size() == 2);
    CHECK(editor.undoLabel() == "Copy 4000 mm along X");
    CHECK(editor.undoDepth() == depth + 1);

    // Any other action ends it: here a selection change.
    editor.select(Selection{editor.document().objects.front()->id, std::nullopt});
    const Document before = editor.document();
    const auto e = tools.submitMoveDistance("5x");
    REQUIRE(e);
    CHECK(e->find("Make a copy") != std::string::npos);
    CHECK(sameContent(editor.document(), before));
}

TEST_CASE("array editing ends with another edit, undo and a tool change") {
    Editor editor;
    ToolController tools(editor);
    makePost(editor, tools);

    SUBCASE("another edit") {
        copyAlongX(tools);
        const ObjectId copy = editor.selection()->objectId;
        REQUIRE_FALSE(tools.setTotalHeight(copy, "900").has_value());
        CHECK(tools.submitMoveDistance("5x").has_value());
        CHECK(editor.document().objects.size() == 2);
    }
    SUBCASE("undo") {
        copyAlongX(tools);
        REQUIRE(editor.undo());
        CHECK(tools.submitMoveDistance("5x").has_value());
        CHECK(editor.document().objects.size() == 1);
    }
    SUBCASE("tool change") {
        copyAlongX(tools);
        tools.setTool(Tool::Select);
        tools.setTool(Tool::Move);
        CHECK(tools.submitMoveDistance("5x").has_value());
        CHECK(editor.document().objects.size() == 2);
    }
    SUBCASE("tapping Ctrl for another copy") {
        copyAlongX(tools);
        copyAlongX(tools); // a second typed copy extends the row instead of editing the first
        CHECK(editor.document().objects.size() == 3);
        CHECK_FALSE(tools.moveCopy());
        tools.setTool(Tool::Rotate);
        tools.toggleRotateCopy();
        REQUIRE_FALSE(tools.submitRotateAngle("90").has_value());
        tools.toggleRotateCopy();
        REQUIRE_FALSE(tools.submitRotateAngle("90").has_value());
        CHECK(editor.document().objects.size() == 5);
    }
    SUBCASE("a plain move is not a copy") {
        tools.setTool(Tool::Move);
        tools.toggleAxisLock(MoveAxis::X);
        REQUIRE_FALSE(tools.submitMoveDistance("2000").has_value());
        CHECK(tools.submitMoveDistance("5x").has_value());
        CHECK(editor.document().objects.size() == 1);
    }
}

TEST_CASE("a rejected array leaves the copy as it was and still editable") {
    Editor editor;
    ToolController tools(editor);
    makePost(editor, tools);
    copyAlongX(tools, "5000");
    const Document before = editor.document();
    const auto label = editor.undoLabel();
    // 5 km apart: the far copies leave the +/-10 km range.
    REQUIRE_FALSE(tools.submitMoveDistance("5000000").has_value()); // 5 km re-spacing of one copy is fine
    CHECK(tools.submitMoveDistance("3x").has_value());
    REQUIRE_FALSE(tools.submitMoveDistance("5000").has_value());
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.undoLabel() == label);
    REQUIRE_FALSE(tools.submitMoveDistance("2x").has_value());
    CHECK(editor.document().objects.size() == 3);
}

TEST_CASE("Rotate copy mode places a turned copy; 5x makes a radial array") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    const ObjectId id = test::makeBox(editor, tools, "100, 100", "500", {2.0, 0.0});
    const ProfileObject original = *editor.document().find(id);
    const auto depth = editor.undoDepth();
    tools.setTool(Tool::Rotate);
    tools.toggleRotateCopy();
    CHECK(tools.rotateCopy());
    CHECK(tools.hint().find("Copying") != std::string::npos);

    // Centre at the origin, reference along +X, pointer turning counter-clockwise.
    tools.pointerDown(downAt(editor, 0.0, 0.0));
    tools.pointerDown(downAt(editor, 3.0, 0.0));
    tools.pointerMove(downAt(editor, 0.0, 3.0));
    const auto* p = std::get_if<RotatePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(p->copy);
    CHECK(replacedObjects(editor.preview()).empty()); // the original stays drawn
    REQUIRE_FALSE(tools.submitRotateAngle("60").has_value());

    CHECK(editor.document().objects.size() == 2);
    CHECK(*editor.document().find(id) == original);
    CHECK(editor.undoLabel() == "Rotate copy 60°");
    CHECK_FALSE(tools.rotateCopy()); // ends with the commit

    // Five copies 60° apart: a full ring of six.
    REQUIRE_FALSE(tools.submitRotateAngle("5x").has_value());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Radial array 5 copies 60° apart");
    REQUIRE(editor.document().objects.size() == 6);
    for (const auto& o : editor.document().objects) {
        const Vec3 c = baseCenter(*o);
        CHECK(std::hypot(c.x, c.y) == doctest::Approx(std::hypot(2.05, 0.05)));
    }

    // "/3" spreads three over the 60°; a new angle re-spaces them.
    REQUIRE_FALSE(tools.submitRotateAngle("/3").has_value());
    CHECK(editor.document().objects.size() == 4);
    CHECK(editor.undoLabel() == "Radial array 3 copies 20° apart");
    REQUIRE_FALSE(tools.submitRotateAngle("90").has_value());
    CHECK(editor.undoLabel() == "Radial array 3 copies 30° apart");
    const Vec3 last = baseCenter(*editor.document().find(editor.selection()->objectId));
    CHECK(near(last.x, -0.05, 1e-9)); // the farthest copy is a quarter turn round
    CHECK(near(last.y, 2.05, 1e-9));
    CHECK(editor.undoDepth() == depth + 1);

    REQUIRE(editor.undo());
    CHECK(editor.document().objects.size() == 1);
}

TEST_CASE("Rotate: typed copy with no gesture, array syntax without a copy, tool change") {
    Editor editor;
    ToolController tools(editor);
    makePost(editor, tools);
    tools.setTool(Tool::Rotate);
    CHECK(tools.submitRotateAngle("4x").has_value()); // nothing copied yet
    tools.toggleRotateCopy();
    REQUIRE_FALSE(tools.submitRotateAngle("90").has_value()); // turned copy about the base centre
    CHECK(editor.document().objects.size() == 2);
    REQUIRE_FALSE(tools.submitRotateAngle("x2").has_value());
    CHECK(editor.document().objects.size() == 3);
    tools.setTool(Tool::Move);
    tools.setTool(Tool::Rotate);
    CHECK_FALSE(tools.rotateCopy());
    CHECK(tools.submitRotateAngle("4x").has_value());
}

TEST_CASE("Editor::amendLast replaces the last entry or changes nothing") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = makePost(editor, tools);
    CHECK_FALSE(Editor{}.amendLast("x", [](const Document& d) { return Expected<OpResult>{OpResult{d, std::nullopt, {}}}; }));
    const auto depth = editor.undoDepth();
    REQUIRE(editor.commit("Copy", [a](const Document& d) { return duplicateObjects(d, {a}, {1, 0, 0}); }));
    const auto id = editor.stateId();
    REQUIRE(editor.amendLast("Array", [a](const Document& d) { return arrayObjects(d, {a}, {1, 0, 0}, 4); }));
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Array");
    CHECK(editor.document().objects.size() == 5);
    CHECK(editor.stateId() != id);
    const Document before = editor.document();
    CHECK_FALSE(editor.amendLast("Bad", [a](const Document& d) { return arrayObjects(d, {a}, {1, 0, 0}, 0); }));
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.undoLabel() == "Array");
    REQUIRE(editor.undo());
    CHECK(editor.document().objects.size() == 1);
}
