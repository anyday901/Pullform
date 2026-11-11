// Multi-selection: the selection model, window/crossing region selection and
// operations on several objects as one undo entry. These drive the core only
// (Editor, operations, region_select); tool gestures are in tools_tests.cpp.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/commands/selection.hpp"
#include "core/geometry/region_select.hpp"
#include "core/geometry/solid.hpp"

#include <numbers>

using namespace pf;

namespace {

/// Adds a w x d x h box (meters) with its minimum corner at (x, y, 0).
ObjectId addBox(Editor& editor, double x, double y, double w = 1.0, double d = 1.0, double h = 1.0) {
    REQUIRE(editor.commit("Rectangle", [&](const Document& doc) {
        return createRectangle(doc, RectanglePlacement{{x, y, 0.0}, w, d});
    }, [](const OpResult& r) -> std::optional<Selection> { return Selection{*r.objectId, std::nullopt}; }));
    const ObjectId id = editor.selection()->objectId;
    REQUIRE(editor.commit("Extrude", [&](const Document& doc) { return extrudeProfile(doc, id, h); }));
    return id;
}

/// Looking straight down: 100 pixels per meter, screen y grows toward -Y.
std::optional<Vec2> topDown(const Vec3& p) { return Vec2{p.x * 100.0, -p.y * 100.0}; }

ScreenRect rectMeters(double x0, double y0, double x1, double y1) {
    return ScreenRect{*topDown({x0, y0, 0}), *topDown({x1, y1, 0})};
}

} // namespace

TEST_CASE("selection set: add, toggle, remove and primary") {
    SelectionSet s;
    CHECK(s.empty());
    s.add(Selection{ObjectId{1}, FaceId::cap()});
    s.add(Selection{ObjectId{2}, std::nullopt});
    CHECK(s.size() == 2);
    CHECK(s.primary()->objectId == ObjectId{2});

    s.add(Selection{ObjectId{1}, FaceId::cap()}); // already in: order kept, becomes primary
    CHECK(s.objects() == std::vector<ObjectId>{ObjectId{1}, ObjectId{2}});
    CHECK(s.primary()->objectId == ObjectId{1});

    s.toggle(Selection{ObjectId{1}, std::nullopt}); // removes; last remaining becomes primary
    CHECK(s.objects() == std::vector<ObjectId>{ObjectId{2}});
    CHECK(s.primary() == Selection{ObjectId{2}, std::nullopt});
    s.toggle(Selection{ObjectId{3}, std::nullopt});
    CHECK(s.size() == 2);
    CHECK(s.primary()->objectId == ObjectId{3});
    s.clear();
    CHECK(s.empty());
    CHECK_FALSE(s.primary());
}

TEST_CASE("click rules: plain replaces, Shift toggles, Ctrl adds, empty space clears") {
    const Selection a{ObjectId{1}, FaceId::cap()};
    const Selection b{ObjectId{2}, FaceId::cap()};
    SelectionSet s = applyClick({}, a, SelectModifier::Replace);
    CHECK(s.objects() == std::vector<ObjectId>{a.objectId});

    s = applyClick(s, b, SelectModifier::Toggle); // Shift adds
    CHECK(s.size() == 2);
    s = applyClick(s, a, SelectModifier::Toggle); // Shift again removes
    CHECK(s.objects() == std::vector<ObjectId>{b.objectId});

    s = applyClick(s, a, SelectModifier::Add); // Ctrl adds
    s = applyClick(s, a, SelectModifier::Add); // and never removes
    CHECK(s.size() == 2);
    CHECK(s.primary() == a);

    CHECK(applyClick(s, std::nullopt, SelectModifier::Toggle) == s); // modifier + empty space keeps it
    CHECK(applyClick(s, std::nullopt, SelectModifier::Add) == s);
    CHECK(applyClick(s, std::nullopt, SelectModifier::Replace).empty());

    s = applyClick(s, b, SelectModifier::Replace);
    CHECK(s.objects() == std::vector<ObjectId>{b.objectId});
}

TEST_CASE("editor: select all, sanitize and the primary selection") {
    Editor editor;
    const ObjectId a = addBox(editor, 0, 0);
    const ObjectId b = addBox(editor, 2, 0);
    const ObjectId c = addBox(editor, 4, 0);
    editor.setSelection(selectAll(editor.document()));
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b, c});
    CHECK(editor.selection()->objectId == c); // the primary is still a single Selection
    CHECK(editor.isSelected(b));

    // A selection naming a missing object drops it.
    SelectionSet withGhost = editor.selectionSet();
    withGhost.add(Selection{ObjectId{999}, std::nullopt});
    editor.setSelection(withGhost);
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b, c});
    CHECK(editor.selection()->objectId == c);

    editor.select(Selection{a, FaceId::cap()}); // single select replaces everything
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a});
    CHECK(editor.selection()->face == FaceId::cap());
}

TEST_CASE("window selection takes objects entirely inside; crossing takes objects it touches") {
    Editor editor;
    const ObjectId a = addBox(editor, 0, 0);
    const ObjectId b = addBox(editor, 2, 0);
    const ObjectId c = addBox(editor, 4, 0);
    const Document& doc = editor.document();

    CHECK(regionModeForDrag({0, 0}, {10, 10}) == RegionMode::Window);
    CHECK(regionModeForDrag({10, 0}, {0, 10}) == RegionMode::Crossing);

    // Around a and b, and half of c.
    const auto r = rectMeters(-0.5, -0.5, 4.5, 1.5);
    CHECK(objectsInRegion(doc, r, RegionMode::Window, topDown) == std::vector<ObjectId>{a, b});
    CHECK(objectsInRegion(doc, r, RegionMode::Crossing, topDown) == std::vector<ObjectId>{a, b, c});

    // A small rectangle inside one face: window selects nothing, crossing selects that box.
    const auto inner = rectMeters(2.2, 0.2, 2.4, 0.4);
    CHECK(objectsInRegion(doc, inner, RegionMode::Window, topDown).empty());
    CHECK(objectsInRegion(doc, inner, RegionMode::Crossing, topDown) == std::vector<ObjectId>{b});

    // A thin rectangle crossing b's middle without any corner inside.
    const auto across = rectMeters(1.5, 0.45, 3.5, 0.55);
    CHECK(objectsInRegion(doc, across, RegionMode::Crossing, topDown) == std::vector<ObjectId>{b});

    // Between the boxes: nothing either way.
    const auto gap = rectMeters(1.2, -0.5, 1.8, 1.5);
    CHECK(objectsInRegion(doc, gap, RegionMode::Crossing, topDown).empty());

    // A corner behind the eye never counts as inside a window.
    const ScreenProjector behind = [](const Vec3& p) -> std::optional<Vec2> {
        if (p.z > 0.5) return std::nullopt;
        return topDown(p);
    };
    CHECK(objectsInRegion(doc, r, RegionMode::Window, behind).empty());
}

TEST_CASE("region rules: Shift toggles and Ctrl adds the found objects") {
    const ObjectId a{1}, b{2}, c{3};
    SelectionSet s = SelectionSet::ofObjects({a, b});
    CHECK(applyRegion(s, {c}, SelectModifier::Replace).objects() == std::vector<ObjectId>{c});
    CHECK(applyRegion(s, {b, c}, SelectModifier::Add).objects() == std::vector<ObjectId>{a, b, c});
    CHECK(applyRegion(s, {b, c}, SelectModifier::Toggle).objects() == std::vector<ObjectId>{a, c});
}

TEST_CASE("acceptance: select three boxes by window, move them 1 m with one undo entry") {
    Editor editor;
    const ObjectId a = addBox(editor, 0, 0);
    const ObjectId b = addBox(editor, 2, 0);
    const ObjectId c = addBox(editor, 4, 0);
    const ObjectId outside = addBox(editor, 0, 5);

    const auto found = objectsInRegion(editor.document(), rectMeters(-0.5, -0.5, 5.5, 1.5), RegionMode::Window, topDown);
    editor.setSelection(applyRegion(editor.selectionSet(), found, SelectModifier::Replace));
    REQUIRE(editor.selectedObjects() == std::vector<ObjectId>{a, b, c});

    const auto depth = editor.undoDepth();
    const auto ids = editor.selectedObjects();
    REQUIRE(editor.commit("Move 3 objects", [&](const Document& d) { return moveObjects(d, ids, Vec3{1.0, 0.0, 0.0}); }));
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.document().find(a)->transform.translation.x == doctest::Approx(1.0));
    CHECK(editor.document().find(b)->transform.translation.x == doctest::Approx(3.0));
    CHECK(editor.document().find(c)->transform.translation.x == doctest::Approx(5.0));
    CHECK(editor.document().find(outside)->transform.translation.x == doctest::Approx(0.0));
    CHECK(editor.selectedObjects() == ids); // the selection stays on the moved objects

    editor.select(std::nullopt);
    REQUIRE(editor.undo());
    CHECK(editor.document().find(a)->transform.translation.x == doctest::Approx(0.0));
    CHECK(editor.document().find(b)->transform.translation.x == doctest::Approx(2.0));
    CHECK(editor.document().find(c)->transform.translation.x == doctest::Approx(4.0));
    CHECK(editor.selectedObjects() == ids); // undo restores the multi-selection
    REQUIRE(editor.redo());
    CHECK(editor.document().find(c)->transform.translation.x == doctest::Approx(5.0));
}

TEST_CASE("operations on a selection: rotate, scale, duplicate and delete as one entry each") {
    Editor editor;
    const ObjectId a = addBox(editor, 0, 0);
    const ObjectId b = addBox(editor, 2, 0);
    const std::vector<ObjectId> ids{a, b};
    const auto pivot = selectionBaseCenter(editor.document(), ids);
    REQUIRE(pivot);
    CHECK(pivot->x == doctest::Approx(1.5));
    CHECK(pivot->y == doctest::Approx(0.5));
    CHECK(pivot->z == doctest::Approx(0.0));

    SUBCASE("rotate turns the group about one centre") {
        const auto depth = editor.undoDepth();
        REQUIRE(editor.commit("Rotate", [&](const Document& d) {
            return rotateObjects(d, ids, *pivot, Vec3{0, 0, 1}, std::numbers::pi);
        }));
        CHECK(editor.undoDepth() == depth + 1);
        // Half a turn about (1.5, 0.5) swaps the boxes' footprints.
        const Bounds ba = objectBounds(*editor.document().find(a));
        const Bounds bb = objectBounds(*editor.document().find(b));
        CHECK(ba.min.x == doctest::Approx(2.0));
        CHECK(bb.min.x == doctest::Approx(0.0).epsilon(1e-9));
        CHECK(ba.min.y == doctest::Approx(0.0).epsilon(1e-9));
    }
    SUBCASE("uniform scale grows the group about its base centre") {
        REQUIRE(editor.commit("Scale", [&](const Document& d) { return scaleObjects(d, ids, Vec3{2.0}, *pivot); }));
        const Bounds ba = objectBounds(*editor.document().find(a));
        const Bounds bb = objectBounds(*editor.document().find(b));
        // Group spanned x 0..3, y 0..1, z 0..1; scaled by 2 about (1.5, 0.5, 0).
        CHECK(ba.min.x == doctest::Approx(-1.5));
        CHECK(ba.max.x == doctest::Approx(0.5));
        CHECK(bb.min.x == doctest::Approx(2.5));
        CHECK(bb.max.x == doctest::Approx(4.5));
        CHECK(ba.min.y == doctest::Approx(-0.5));
        CHECK(ba.max.z == doctest::Approx(2.0));
        CHECK(ba.min.z == doctest::Approx(0.0));
    }
    SUBCASE("duplicate copies every object and the copies can become the selection") {
        const auto count = editor.document().objects.size();
        REQUIRE(editor.commit("Duplicate", [&](const Document& d) { return duplicateObjects(d, ids, Vec3{0, 3, 0}); },
                              [](const OpResult& r) { return SelectionSet::ofObjects(r.objectIds); }));
        CHECK(editor.document().objects.size() == count + 2);
        const auto copies = editor.selectedObjects();
        REQUIRE(copies.size() == 2);
        CHECK(copies[0] != a);
        CHECK(editor.document().find(copies[0])->transform.translation.y == doctest::Approx(3.0));
        CHECK(editor.document().find(copies[1])->transform.translation.x == doctest::Approx(2.0));
        REQUIRE(editor.undo());
        CHECK(editor.document().objects.size() == count);
    }
    SUBCASE("delete removes every object and its dimensions; undo brings them back") {
        REQUIRE(editor.commit("Dimension", [&](const Document& d) {
            return addDimension(d, DimensionAnchor{Vec3{0.0}, a, {0}}, DimensionAnchor{Vec3{0.0}, b, {0}}, Vec3{0, -0.5, 0});
        }));
        editor.setSelection(SelectionSet::ofObjects(ids));
        REQUIRE(editor.commit("Delete", [&](const Document& d) { return deleteObjects(d, ids); }));
        CHECK(editor.document().objects.empty());
        CHECK(editor.document().dimensions.empty());
        CHECK(editor.selectionSet().empty());
        REQUIRE(editor.undo());
        CHECK(editor.document().objects.size() == 2);
        CHECK(editor.document().dimensions.size() == 1);
        CHECK(editor.selectedObjects() == ids);
    }
    SUBCASE("a failure on any object changes nothing") {
        const auto before = editor.document().objects;
        CHECK_FALSE(editor.commit("Move", [&](const Document& d) {
            return moveObjects(d, {a, ObjectId{999}}, Vec3{1, 0, 0});
        }));
        CHECK(editor.document().objects == before);
        CHECK_FALSE(moveObjects(editor.document(), {}, Vec3{1, 0, 0}));
        CHECK_FALSE(moveObjects(editor.document(), {a, a}, Vec3{1, 0, 0}));
    }
}

// ---- Tool gestures ---------------------------------------------------------

namespace {

/// A straight-down pointer at (x, y) meters, with the top-down projector and
/// screen position, so the Select tool can drag window and crossing boxes.
PointerInput at(const Editor& editor, double x, double y, bool shift = false, bool ctrl = false) {
    PointerInput in = test::downAt(editor, x, y, *topDown({x, y, 0}));
    in.project = topDown;
    in.shift = shift;
    in.ctrl = ctrl;
    return in;
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

/// Press at `from`, move to `to` and release: a box drag in the Select tool.
void drag(ToolController& tools, const PointerInput& from, const PointerInput& to) {
    tools.pointerDown(from);
    tools.pointerMove(to);
    tools.pointerUp(to);
}

struct ThreeBoxes {
    Editor editor;
    ToolController tools{editor};
    ObjectId a;
    ObjectId b;
    ObjectId c;
    ObjectId far;
    ThreeBoxes() {
        a = addBox(editor, 0, 0);
        b = addBox(editor, 2, 0);
        c = addBox(editor, 4, 0);
        far = addBox(editor, 0, 5);
        tools.setTool(Tool::Select);
        editor.select(std::nullopt);
    }
};

} // namespace

TEST_CASE("select tool: click, Shift, Ctrl and empty space") {
    ThreeBoxes s;
    auto& [editor, tools, a, b, c, far] = s;
    click(tools, at(editor, 0.5, 0.5));
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a});
    CHECK(editor.selection()->face == FaceId::cap()); // the primary still targets the clicked face

    click(tools, at(editor, 2.5, 0.5, true)); // Shift adds
    click(tools, at(editor, 4.5, 0.5, false, true)); // Ctrl adds
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b, c});
    click(tools, at(editor, 2.5, 0.5, true)); // Shift removes
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, c});
    click(tools, at(editor, 4.5, 0.5, false, true)); // Ctrl never removes
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, c});

    click(tools, at(editor, 1.5, 3.0, true)); // Shift on empty space keeps the selection
    CHECK(editor.selectedObjects().size() == 2);
    click(tools, at(editor, 1.5, 3.0)); // plain click on empty space clears
    CHECK(editor.selectionSet().empty());
    CHECK(editor.undoDepth() == 8); // selection is never history (4 boxes x draw + extrude)
}

TEST_CASE("select tool: window and crossing boxes") {
    ThreeBoxes s;
    auto& [editor, tools, a, b, c, far] = s;

    SUBCASE("left to right selects objects entirely inside, with a live preview") {
        tools.pointerDown(at(editor, -0.5, 1.5));
        tools.pointerMove(at(editor, 4.5, -0.5)); // c only half inside
        CHECK(tools.active());
        const auto* box = std::get_if<SelectBoxPreview>(&editor.preview());
        REQUIRE(box);
        CHECK(box->mode == RegionMode::Window);
        CHECK(box->found == std::vector<ObjectId>{a, b});
        tools.pointerUp(at(editor, 4.5, -0.5));
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b});
        CHECK(std::holds_alternative<std::monostate>(editor.preview()));
        CHECK_FALSE(tools.active());
    }
    SUBCASE("right to left also selects objects the box touches") {
        drag(tools, at(editor, 4.5, 1.5), at(editor, 1.5, -0.5));
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{b, c});
    }
    SUBCASE("a drag that starts on an object is still a box") {
        drag(tools, at(editor, 2.5, 0.5), at(editor, 0.8, 0.2)); // crossing from inside b reaches a
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b});
    }
    SUBCASE("Shift toggles and Ctrl adds the boxed objects") {
        click(tools, at(editor, 0.5, 0.5));
        drag(tools, at(editor, 1.5, 1.5, false, true), at(editor, 3.5, -0.5, false, true));
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b});
        drag(tools, at(editor, -0.5, 1.5, true), at(editor, 5.5, -0.5, true));
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{c}); // a and b toggled off, c on
    }
    SUBCASE("Escape during a box keeps the previous selection") {
        click(tools, at(editor, 0.5, 5.5));
        tools.pointerDown(at(editor, -0.5, 1.5));
        tools.pointerMove(at(editor, 5.5, -0.5));
        CHECK(tools.cancel());
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{far});
        CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    }
    SUBCASE("a box over empty space clears") {
        click(tools, at(editor, 0.5, 0.5));
        drag(tools, at(editor, 1.2, 3.0), at(editor, 1.8, 4.0));
        CHECK(editor.selectionSet().empty());
    }
}

TEST_CASE("acceptance (tools): window-select three boxes, drag them 1 m, one undo entry") {
    ThreeBoxes s;
    auto& [editor, tools, a, b, c, far] = s;
    drag(tools, at(editor, -0.5, 1.5), at(editor, 5.5, -0.5));
    REQUIRE(editor.selectedObjects() == std::vector<ObjectId>{a, b, c});

    tools.setTool(Tool::Move);
    const auto depth = editor.undoDepth();
    tools.pointerDown(at(editor, 2.5, 0.5)); // grab b: the whole selection moves
    tools.pointerMove(at(editor, 3.0, 0.5));
    const auto* preview = std::get_if<MovePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->objectIds == std::vector<ObjectId>{a, b, c});
    CHECK(replacedObjects(editor.preview()) == std::vector<ObjectId>{a, b, c});
    tools.pointerUp(at(editor, 3.5, 0.5));

    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Move 1000 mm along X (3 objects)");
    CHECK(editor.document().find(a)->transform.translation.x == doctest::Approx(1.0));
    CHECK(editor.document().find(b)->transform.translation.x == doctest::Approx(3.0));
    CHECK(editor.document().find(c)->transform.translation.x == doctest::Approx(5.0));
    CHECK(editor.document().find(far)->transform.translation.x == doctest::Approx(0.0));
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b, c});

    REQUIRE(editor.undo());
    CHECK(editor.document().find(a)->transform.translation.x == doctest::Approx(0.0));
    CHECK(editor.document().find(b)->transform.translation.x == doctest::Approx(2.0));
    CHECK(editor.document().find(c)->transform.translation.x == doctest::Approx(4.0));
}

TEST_CASE("move tool: typed distance and copy act on the selection; an unselected object moves alone") {
    ThreeBoxes s;
    auto& [editor, tools, a, b, c, far] = s;
    editor.setSelection(SelectionSet::ofObjects({a, b}));
    tools.setTool(Tool::Move);

    SUBCASE("typed x, y, z with nothing grabbed") {
        REQUIRE_FALSE(tools.submitMoveDistance("0, 1000, 0").has_value());
        CHECK(editor.document().find(a)->transform.translation.y == doctest::Approx(1.0));
        CHECK(editor.document().find(b)->transform.translation.y == doctest::Approx(1.0));
        CHECK(editor.document().find(c)->transform.translation.y == doctest::Approx(0.0));
    }
    SUBCASE("copy mode duplicates the whole selection in one entry and selects the copies") {
        const auto count = editor.document().objects.size();
        const auto depth = editor.undoDepth();
        tools.toggleMoveCopy();
        tools.toggleAxisLock(MoveAxis::Y);
        tools.pointerDown(at(editor, 0.5, 0.5));
        REQUIRE_FALSE(tools.submitMoveDistance("2000").has_value());
        CHECK(editor.document().objects.size() == count + 2);
        CHECK(editor.undoDepth() == depth + 1);
        const auto copies = editor.selectedObjects();
        REQUIRE(copies.size() == 2);
        CHECK(editor.document().find(copies[0])->transform.translation.y == doctest::Approx(2.0));
        CHECK(editor.document().find(a)->transform.translation.y == doctest::Approx(0.0));
        CHECK(editor.selection()->objectId == copies[0]); // the grabbed object's copy is primary
    }
    SUBCASE("grabbing an object outside the selection moves only it") {
        tools.toggleAxisLock(MoveAxis::X);
        tools.pointerDown(at(editor, 4.5, 0.5));
        REQUIRE_FALSE(tools.submitMoveDistance("1000").has_value());
        CHECK(editor.document().find(c)->transform.translation.x == doctest::Approx(5.0));
        CHECK(editor.document().find(a)->transform.translation.x == doctest::Approx(0.0));
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{c});
    }
}

TEST_CASE("rotate, scale and duplicate act on the whole selection as one undo entry") {
    ThreeBoxes s;
    auto& [editor, tools, a, b, c, far] = s;
    editor.setSelection(SelectionSet::ofObjects({a, b}));
    const auto depth = editor.undoDepth();

    SUBCASE("typed rotation turns the group about its base centre") {
        tools.setTool(Tool::Rotate);
        REQUIRE_FALSE(tools.submitRotateAngle("180").has_value());
        CHECK(editor.undoDepth() == depth + 1);
        CHECK(objectBounds(*editor.document().find(a)).min.x == doctest::Approx(2.0));
        CHECK(objectBounds(*editor.document().find(b)).min.x == doctest::Approx(0.0).epsilon(1e-9));
        CHECK(editor.selectedObjects() == std::vector<ObjectId>{a, b});
    }
    SUBCASE("rotate gesture previews every object") {
        tools.setTool(Tool::Rotate);
        tools.pointerDown(at(editor, 0.5, 0.5));   // centre on a's cap
        tools.pointerDown(at(editor, 1.5, 0.5));   // reference along +X
        tools.pointerMove(at(editor, 0.5, 1.5));   // a quarter turn
        const auto* p = std::get_if<RotatePreview>(&editor.preview());
        REQUIRE(p);
        CHECK(p->group.size() == 2);
        CHECK(replacedObjects(editor.preview()) == std::vector<ObjectId>{a, b});
        tools.pointerDown(at(editor, 0.5, 1.5));
        CHECK(editor.undoDepth() == depth + 1);
        CHECK(editor.undoLabel().find("(2 objects)") != std::string::npos);
    }
    SUBCASE("typed scale grows the group about its base centre") {
        tools.setTool(Tool::Scale);
        REQUIRE_FALSE(tools.submitScaleFactors("2").has_value());
        CHECK(editor.undoDepth() == depth + 1);
        CHECK(objectBounds(*editor.document().find(a)).min.x == doctest::Approx(-1.5));
        CHECK(objectBounds(*editor.document().find(b)).max.x == doctest::Approx(4.5));
        CHECK(objectBounds(*editor.document().find(c)).min.x == doctest::Approx(4.0)); // not selected
    }
    SUBCASE("duplicate copies the group beside itself") {
        const auto count = editor.document().objects.size();
        REQUIRE_FALSE(tools.duplicateSelection().has_value());
        CHECK(editor.undoDepth() == depth + 1);
        CHECK(editor.document().objects.size() == count + 2);
        const auto copies = editor.selectedObjects();
        REQUIRE(copies.size() == 2);
        CHECK(editor.document().find(copies[0])->transform.translation.x == doctest::Approx(3.0)); // group is 3 m wide
        CHECK(editor.document().find(copies[1])->transform.translation.x == doctest::Approx(5.0));
    }
}
