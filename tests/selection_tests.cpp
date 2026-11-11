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
