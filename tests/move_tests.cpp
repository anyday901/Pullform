// Move tool: typed distances, axis locking, drag gestures and one undo entry per move.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/solid.hpp"

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;
using pf::test::makeBox;

namespace {

Vec3 translationOf(const Editor& editor, ObjectId id) { return editor.document().find(id)->transform.translation; }

} // namespace

TEST_CASE("move an object 500 mm along X by typing; undo restores it") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = makeBox(editor, tools, "2000, 1000", "750");
    const auto before = *editor.document().find(id);
    const auto depth = editor.undoDepth();

    tools.setTool(Tool::Move);
    tools.toggleAxisLock(MoveAxis::X);
    REQUIRE(tools.axisLock() == MoveAxis::X);
    CHECK(tools.hint().find("Locked to X") != std::string::npos);
    REQUIRE_FALSE(tools.submitMoveDistance("500").has_value());

    const auto& after = *editor.document().find(id);
    CHECK(after.transform.translation == Vec3{0.5, 0.0, 0.0}); // exact, not approximately
    CHECK(after.id == before.id);
    CHECK(after.profile == before.profile);
    CHECK(after.extrusion == before.extrusion);
    CHECK(editor.undoDepth() == depth + 1); // one entry per move
    CHECK(editor.undoLabel() == "Move 500 mm along X");
    CHECK_FALSE(tools.axisLock().has_value()); // the lock is released after the move
    CHECK(editor.selection()->objectId == id);

    REQUIRE(editor.undo());
    CHECK(*editor.document().find(id) == before);
    REQUIRE(editor.redo());
    CHECK(translationOf(editor, id) == Vec3{0.5, 0.0, 0.0});
}

TEST_CASE("typed move: units, negative distances, Y and Z locks and x, y, z vectors") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = makeBox(editor, tools, "1000, 1000", "1000");
    tools.setTool(Tool::Move);

    tools.toggleAxisLock(MoveAxis::Y);
    REQUIRE_FALSE(tools.submitMoveDistance("-2m").has_value());
    CHECK(translationOf(editor, id) == Vec3{0.0, -2.0, 0.0});
    CHECK(editor.undoLabel() == "Move 2000 mm along Y");

    tools.toggleAxisLock(MoveAxis::Z);
    REQUIRE_FALSE(tools.submitMoveDistance("300").has_value());
    CHECK(translationOf(editor, id) == Vec3{0.0, -2.0, 0.3});

    REQUIRE_FALSE(tools.submitMoveDistance("100, 200, -300").has_value());
    CHECK(translationOf(editor, id).x == doctest::Approx(0.1));
    CHECK(translationOf(editor, id).y == doctest::Approx(-1.8));
    CHECK(translationOf(editor, id).z == doctest::Approx(0.0));
    CHECK(editor.undoDepth() == 5); // rectangle, push/pull, three moves
}

TEST_CASE("pressing the locked axis again releases the lock; changing tools clears it") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Move);
    tools.toggleAxisLock(MoveAxis::Z);
    tools.toggleAxisLock(MoveAxis::Z);
    CHECK_FALSE(tools.axisLock().has_value());
    tools.toggleAxisLock(MoveAxis::X);
    tools.toggleAxisLock(MoveAxis::Y); // switching axes keeps a lock, on the new axis
    CHECK(tools.axisLock() == MoveAxis::Y);
    tools.setTool(Tool::Select);
    CHECK_FALSE(tools.axisLock().has_value());
}

TEST_CASE("typed move is refused without a target, a direction, or a valid distance") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Move);
    CHECK(tools.submitMoveDistance("500").has_value()); // nothing selected

    const ObjectId id = makeBox(editor, tools, "1000, 1000", nullptr);
    tools.setTool(Tool::Move);
    const auto depth = editor.undoDepth();
    CHECK(tools.submitMoveDistance("500").value().find("Lock an axis") != std::string::npos);
    CHECK(tools.submitMoveDistance("500, 0").has_value()); // two values: neither a length nor a vector
    CHECK(tools.submitMoveDistance("0, 0, 0").has_value());
    tools.toggleAxisLock(MoveAxis::X);
    CHECK(tools.submitMoveDistance("0").has_value());
    CHECK(tools.submitMoveDistance("abc").has_value());
    CHECK(tools.submitMoveDistance("20km").has_value());
    CHECK(tools.submitMoveDistance("11000m").has_value()); // beyond the 10 km range
    CHECK(editor.undoDepth() == depth);
    CHECK(translationOf(editor, id) == Vec3{0.0});
    CHECK(tools.axisLock() == MoveAxis::X); // a refused move keeps the lock for the next try
}

TEST_CASE("drag a box across the ground: snapped, previewed, one entry; Esc cancels") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    tools.snap().step = 0.1;
    const ObjectId id = makeBox(editor, tools, "1000, 1000", "500");
    const auto depth = editor.undoDepth();

    tools.setTool(Tool::Move);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100})); // the cap, at z = 0.5
    REQUIRE(tools.active());
    for (int i = 1; i <= 10; ++i) tools.pointerMove(downAt(editor, 0.5 + 0.2 * i, 0.5 + 0.1 * i, {100.0 + 10 * i, 100}));
    tools.pointerMove(downAt(editor, 2.53, 1.47, {220, 100}));
    const auto* preview = std::get_if<MovePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->objectId == id);
    CHECK(preview->offset.x == doctest::Approx(2.0)); // 2.03 snaps to the 100 mm grid
    CHECK(preview->offset.y == doctest::Approx(1.0));
    CHECK(preview->offset.z == 0.0); // a free move stays in the horizontal plane
    CHECK(editor.undoDepth() == depth); // moving only previews
    CHECK(translationOf(editor, id) == Vec3{0.0});

    tools.pointerUp(downAt(editor, 2.53, 1.47, {220, 100}));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(translationOf(editor, id).x == doctest::Approx(2.0));
    CHECK(translationOf(editor, id).y == doctest::Approx(1.0));
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));

    // Esc mid-gesture: no change, no entry.
    tools.pointerDown(downAt(editor, 2.5, 1.5, {100, 100}));
    tools.pointerUp(downAt(editor, 2.5, 1.5, {100, 100}));
    tools.pointerMove(downAt(editor, 5.0, 5.0, {300, 300}));
    CHECK(tools.cancel());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(translationOf(editor, id).x == doctest::Approx(2.0));
}

TEST_CASE("axis lock during a click-move-click gesture constrains the preview") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    const ObjectId id = makeBox(editor, tools, "1000, 1000", "500");
    tools.setTool(Tool::Move);

    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(downAt(editor, 1.7, 0.9, {150, 120}));
    CHECK(std::get<MovePreview>(editor.preview()).offset.y == doctest::Approx(0.4));

    // Lock X with the pointer where it is: the preview drops the Y component at once.
    tools.toggleAxisLock(MoveAxis::X);
    auto p = std::get<MovePreview>(editor.preview());
    CHECK(p.axis == MoveAxis::X);
    CHECK(p.offset.x == doctest::Approx(1.2));
    CHECK(p.offset.y == 0.0);
    CHECK(p.offset.z == 0.0);

    // Lock Z: a horizontal ray at height 1.3 lifts the cap point from 0.5 to 1.3.
    tools.toggleAxisLock(MoveAxis::Z);
    tools.pointerMove(levelAt(0.5, 0.5, 1.3, {150, 60}));
    p = std::get<MovePreview>(editor.preview());
    CHECK(p.offset.x == 0.0);
    CHECK(p.offset.y == 0.0);
    CHECK(p.offset.z == doctest::Approx(0.8).epsilon(1e-6));

    // A typed distance during the gesture uses the locked axis exactly.
    REQUIRE_FALSE(tools.submitMoveDistance("250").has_value());
    CHECK(translationOf(editor, id) == Vec3{0.0, 0.0, 0.25});
    CHECK_FALSE(tools.active());
}

TEST_CASE("a typed distance during an unlocked move follows the pointer direction") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    const ObjectId id = makeBox(editor, tools, "1000, 1000", nullptr);
    tools.setTool(Tool::Move);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(downAt(editor, 0.5 + 0.3, 0.5 + 0.4, {150, 120})); // direction (0.6, 0.8)
    REQUIRE_FALSE(tools.submitMoveDistance("1000").has_value());
    CHECK(translationOf(editor, id).x == doctest::Approx(0.6));
    CHECK(translationOf(editor, id).y == doctest::Approx(0.8));
    CHECK(editor.undoLabel() == "Move 1000 mm");
}

TEST_CASE("a zero-distance move gesture is a no-op; clicking empty space clears the selection") {
    Editor editor;
    ToolController tools(editor);
    makeBox(editor, tools, "1000, 1000", "500");
    const auto depth = editor.undoDepth();
    tools.setTool(Tool::Move);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth);

    tools.pointerDown(downAt(editor, 8.0, 8.0));
    CHECK_FALSE(tools.active());
    CHECK_FALSE(editor.selection().has_value());
}

TEST_CASE("an out-of-range drag keeps the gesture open and commits nothing") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    const ObjectId id = makeBox(editor, tools, "1000, 1000", nullptr);
    const auto depth = editor.undoDepth();
    tools.setTool(Tool::Move);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(downAt(editor, 20000.0, 0.5, {900, 100}));
    REQUIRE(std::get<MovePreview>(editor.preview()).error.has_value());
    tools.pointerDown(downAt(editor, 20000.0, 0.5, {900, 100}));
    CHECK(tools.active());
    CHECK(editor.undoDepth() == depth);
    REQUIRE(editor.message());
    CHECK(editor.message()->kind == Message::Kind::Error);
    tools.pointerMove(downAt(editor, 3.5, 0.5, {300, 100}));
    tools.pointerDown(downAt(editor, 3.5, 0.5, {300, 100}));
    CHECK(translationOf(editor, id).x == doctest::Approx(3.0));
}

TEST_CASE("moveObject only changes the translation and keeps the solid valid") {
    Document doc;
    auto r = createRectangle(doc, RectanglePlacement{{1.0, 2.0, 0.0}, 2.0, 1.0});
    REQUIRE(r);
    auto e = extrudeProfile(r->document, *r->objectId, 0.5);
    REQUIRE(e);
    const ObjectId id = *e->objectId;
    const auto moved = moveObject(e->document, id, {0.25, -0.5, 1.0});
    REQUIRE(moved);
    const auto& o = *moved->document.find(id);
    CHECK(o.transform.translation == Vec3{1.25, 1.5, 1.0});
    CHECK(o.name == e->document.find(id)->name);
    CHECK(moved->document.nextId == e->document.nextId);
    CHECK(checkSolid(buildMesh(o)).ok());
    CHECK(objectBounds(o).min.z == doctest::Approx(1.0));
    CHECK(e->document.find(id)->transform.translation == Vec3{1.0, 2.0, 0.0}); // input untouched

    CHECK_FALSE(moveObject(e->document, id, {0.0, 0.0, 0.0}));
    CHECK_FALSE(moveObject(e->document, id, {1e4, 0.0, 0.0}));
    CHECK_FALSE(moveObject(e->document, id, {std::nan(""), 0.0, 0.0}));
    CHECK_FALSE(moveObject(e->document, ObjectId{999}, {1.0, 0.0, 0.0}));
}

TEST_CASE("the destination click commits the clicked position, even without a move event first") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    tools.snap().step = 0.1;
    const ObjectId id = makeBox(editor, tools, "1000, 1000", nullptr);
    tools.setTool(Tool::Move);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(downAt(editor, 1.5, 0.5, {150, 100}));
    // The pointer jumps and clicks in the same frame: no pointerMove for (3.5, 2.5).
    tools.pointerDown(downAt(editor, 3.5, 2.5, {300, 200}));
    CHECK(translationOf(editor, id).x == doctest::Approx(3.0));
    CHECK(translationOf(editor, id).y == doctest::Approx(2.0));

    // Same for a drag released without a final move event.
    tools.pointerDown(downAt(editor, 3.5, 2.5, {100, 100}));
    tools.pointerMove(downAt(editor, 4.0, 2.5, {130, 100}));
    tools.pointerUp(downAt(editor, 5.5, 2.5, {200, 100}));
    CHECK(translationOf(editor, id).x == doctest::Approx(5.0));
}

TEST_CASE("locking an axis the pointer ray cannot resolve drops the off-axis part of the preview") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    const ObjectId id = makeBox(editor, tools, "1000, 1000", nullptr);
    tools.setTool(Tool::Move);
    tools.pointerDown(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerUp(downAt(editor, 0.5, 0.5, {100, 100}));
    tools.pointerMove(downAt(editor, 1.7, 0.9, {150, 120})); // free offset (1.2, 0.4, 0)
    // The last ray points straight down, parallel to Z: Z cannot be resolved from it.
    tools.toggleAxisLock(MoveAxis::Z);
    const auto& p = std::get<MovePreview>(editor.preview());
    CHECK(p.axis == MoveAxis::Z);
    CHECK(p.offset == Vec3{0.0}); // not the stale (1.2, 0.4, 0)
    tools.pointerDown(downAt(editor, 1.7, 0.9, {150, 120}));
    CHECK(translationOf(editor, id) == Vec3{0.0}); // zero distance: nothing committed
}
