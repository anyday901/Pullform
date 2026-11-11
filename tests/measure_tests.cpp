// Tape measure and dimension annotations: exact distances, attached anchors,
// history, validation and the project file.
#include "helpers.hpp"

#include "app/viewport/camera.hpp"
#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"

#include <glm/geometric.hpp>

using namespace pf;

namespace {

constexpr double kPixelAngle = 0.0005;

/// A pointer looking at `target` from `from` (snapping on), with the pick
/// resolved against the document as the viewport does it.
PointerInput lookAt(const Editor& editor, const Vec3& target, const Vec3& from, Vec2 screen) {
    PointerInput in;
    in.ray = Ray{from, glm::normalize(target - from)};
    in.pick = pickDocument(in.ray, editor.document());
    in.screen = screen;
    in.pixelAngle = kPixelAngle;
    return in;
}

/// Looking at a point from the front-left, above: corners on the -X/-Y sides
/// of a box at the origin are visible.
PointerInput at(const Editor& editor, const Vec3& target, Vec2 screen = {100, 100}) {
    return lookAt(editor, target, target + Vec3{-6.0, -8.0, 7.0}, screen);
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

const MeasurePreview& measurePreview(const Editor& editor) {
    const auto* m = std::get_if<MeasurePreview>(&editor.preview());
    REQUIRE(m);
    return *m;
}

} // namespace

TEST_CASE("tape measure: the distance between two corners matches the model exactly") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "2000, 1000", "500");
    const auto depth = editor.undoDepth();

    tools.setTool(Tool::TapeMeasure);
    click(tools, at(editor, {0.0, 0.0, 0.0}, {100, 100}));
    tools.pointerMove(at(editor, {1.0, 0.02, 0.0}, {150, 100}));
    CHECK(measurePreview(editor).end.has_value()); // live second point
    click(tools, at(editor, {2.0, 0.0, 0.0}, {200, 100}));
    const auto& m = measurePreview(editor);
    REQUIRE(m.start);
    REQUIRE(m.end);
    CHECK(m.fixed);
    // Bit-exact: the corners are the model's own coordinates.
    CHECK(measureBetween(*m.start, *m.end).length == 2.0);
    CHECK(*m.end == *objectVertex(*editor.document().find(box), 1));
    REQUIRE(editor.message());
    CHECK(editor.message()->text.find("2000 mm") != std::string::npos);
    CHECK(editor.undoDepth() == depth); // measuring never edits the document

    // The top corner diagonally opposite: sqrt(2^2 + 1^2 + 0.5^2), from the same doubles.
    click(tools, at(editor, {0.0, 0.0, 0.0}, {100, 100})); // starts over
    CHECK_FALSE(measurePreview(editor).fixed);
    click(tools, lookAt(editor, {2.0, 1.0, 0.5}, {2.0 + 6.0, 1.0 + 8.0, 7.5}, {300, 50}));
    const auto& d = measurePreview(editor);
    CHECK(measureBetween(*d.start, *d.end).length == glm::length(Vec3{2.0, 1.0, 0.5}));
    CHECK(measureBetween(*d.start, *d.end).delta == Vec3{2.0, 1.0, 0.5});

    // Esc clears it; typed values are refused with an explanation.
    CHECK(tools.submitMeasure("100").has_value());
    CHECK(tools.cancel());
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("dimension: click, click, place; it stays attached to the object") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "2000, 1000", "500");
    const auto depth = editor.undoDepth();

    tools.setTool(Tool::Dimension);
    click(tools, at(editor, {0.0, 0.0, 0.0}, {100, 100}));
    // A vertical edge: bottom corner to top corner above it.
    click(tools, at(editor, {0.0, 0.0, 0.5}, {100, 60}));
    CHECK(measurePreview(editor).fixed);
    CHECK(editor.undoDepth() == depth); // placing the line is still a preview
    tools.pointerMove(at(editor, {-0.3, -0.3, 0.25}, {80, 80}));
    REQUIRE(measurePreview(editor).offset);
    // The offset is perpendicular to the measured (vertical) segment.
    CHECK(measurePreview(editor).offset->z == doctest::Approx(0.0));
    click(tools, at(editor, {-0.3, -0.3, 0.25}, {80, 80}));

    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Add dimension 500 mm");
    REQUIRE(editor.document().dimensions.size() == 1);
    const Dimension dim = editor.document().dimensions[0];
    CHECK(dim.id.value == editor.document().nextId - 1);
    REQUIRE(dim.a.object);
    REQUIRE(dim.b.object);
    CHECK(*dim.a.object == box);
    CHECK(dim.a.vertices == std::vector<int>{0});
    CHECK(dim.b.vertices == std::vector<int>{4}); // cap corner above corner 0
    CHECK(layoutDimension(editor.document(), dim)->length == 0.5);
    CHECK_FALSE(tools.active()); // ready for the next dimension

    // Changing the height moves the attached end with the cap.
    tools.setTool(Tool::Select);
    REQUIRE_FALSE(tools.setTotalHeight(box, "1200").has_value());
    CHECK(layoutDimension(editor.document(), editor.document().dimensions[0])->length == 1.2);
    REQUIRE(editor.undo());
    CHECK(layoutDimension(editor.document(), editor.document().dimensions[0])->length == 0.5);

    // Undo removes the dimension; redo restores it with the same ID.
    REQUIRE(editor.undo());
    CHECK(editor.document().dimensions.empty());
    REQUIRE(editor.redo());
    REQUIRE(editor.document().dimensions.size() == 1);
    CHECK(editor.document().dimensions[0].id == dim.id);
}

TEST_CASE("dimension: a midpoint attaches to its edge, and a typed offset places the line") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "2000, 1000", nullptr); // planar
    tools.setTool(Tool::Dimension);
    click(tools, at(editor, {1.0, 0.0, 0.0}, {100, 100})); // midpoint of the front edge
    click(tools, at(editor, {1.0, 1.0, 0.0}, {100, 60}));  // midpoint of the back edge
    CHECK(tools.submitMeasure("abc").has_value());          // bad text keeps the gesture
    CHECK(tools.active());
    REQUIRE_FALSE(tools.submitMeasure("300").has_value());
    REQUIRE(editor.document().dimensions.size() == 1);
    const auto& d = editor.document().dimensions[0];
    CHECK(*d.a.object == box);
    CHECK(d.a.vertices.size() == 2);
    CHECK(d.b.vertices.size() == 2);
    CHECK(glm::length(d.offset) == doctest::Approx(0.3));
    CHECK(glm::dot(d.offset, Vec3{0, 1, 0}) == doctest::Approx(0.0)); // perpendicular to the measured Y segment
    CHECK(layoutDimension(editor.document(), d)->length == 1.0);
    // Extruding keeps base corners and midpoints where they are.
    REQUIRE_FALSE(tools.submitPushDistance("400").has_value());
    CHECK(layoutDimension(editor.document(), editor.document().dimensions[0])->length == 1.0);
}

TEST_CASE("dimension: points off the model are fixed; zero length is refused") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Dimension);
    click(tools, at(editor, {3.0, 3.0, 0.0}));
    click(tools, at(editor, {3.0, 3.0, 0.0}));
    CHECK(editor.message()->kind == Message::Kind::Error);
    CHECK_FALSE(measurePreview(editor).fixed);
    click(tools, at(editor, {4.5, 3.0, 0.0}, {200, 100}));
    click(tools, at(editor, {4.0, 2.0, 0.0}, {150, 150}));
    REQUIRE(editor.document().dimensions.size() == 1);
    const auto& d = editor.document().dimensions[0];
    CHECK_FALSE(d.a.object);
    CHECK_FALSE(d.b.object);
    CHECK(layoutDimension(editor.document(), d)->length == doctest::Approx(1.5)); // on the 100 mm grid
    // The op itself refuses coincident ends.
    const DimensionAnchor p{{1, 1, 0}, std::nullopt, {}};
    CHECK_FALSE(addDimension(editor.document(), p, p, {0, -0.2, 0}).ok());
}

TEST_CASE("deleting an object deletes its dimensions; undo brings both back") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "1000, 1000", "1000");
    const ObjectId b = test::makeBox(editor, tools, "500, 500", "500", {3.0, 0.0});
    const DimensionAnchor free{{9, 9, 0}, std::nullopt, {}};
    REQUIRE(editor.commit("dim", [&](const Document& d) { return addDimension(d, anchorAt(d, {1, 0, 0}), anchorAt(d, {3, 0, 0}), {0, -0.5, 0}); }));
    REQUIRE(editor.commit("dim", [&](const Document& d) { return addDimension(d, anchorAt(d, {3, 0, 0}), free, {0, -0.5, 0}); }));
    REQUIRE(editor.commit("dim", [&](const Document& d) { return addDimension(d, anchorAt(d, {0, 0, 0}), anchorAt(d, {0, 0, 1}), {-0.5, 0, 0}); }));
    REQUIRE(editor.document().dimensions.size() == 3);
    CHECK(*editor.document().dimensions[0].a.object == a);
    CHECK(*editor.document().dimensions[0].b.object == b);

    REQUIRE(editor.commit("Delete", [b](const Document& d) { return deleteObject(d, b); }));
    REQUIRE(editor.document().dimensions.size() == 1); // only the one on `a` is left
    CHECK(*editor.document().dimensions[0].a.object == a);
    REQUIRE(editor.undo());
    CHECK(editor.document().dimensions.size() == 3);

    // A dimension can be deleted on its own.
    const DimensionId id = editor.document().dimensions[1].id;
    REQUIRE(editor.commit("Delete dim", [id](const Document& d) { return deleteDimension(d, id); }));
    CHECK_FALSE(editor.document().findDimension(id));
    CHECK_FALSE(deleteDimension(editor.document(), id).ok());
}

TEST_CASE("dimension validation rejects bad references and IDs") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000, 1000", nullptr); // planar: corners 0..3
    Document doc = editor.document();
    const auto withDim = [&](Dimension d) {
        Document copy = doc;
        copy.nextId = 10;
        copy.dimensions.push_back(std::move(d));
        return validateDocument(copy);
    };
    const DimensionAnchor corner0{{0, 0, 0}, box, {0}};
    const DimensionAnchor free{{2, 0, 0}, std::nullopt, {}};
    CHECK_FALSE(withDim({DimensionId{5}, corner0, free, {0, -1, 0}}).has_value());
    CHECK(withDim({DimensionId{5}, DimensionAnchor{{}, box, {4}}, free, {}}).has_value()); // no cap corners yet
    CHECK(withDim({DimensionId{5}, DimensionAnchor{{}, ObjectId{77}, {0}}, free, {}}).has_value());
    CHECK(withDim({DimensionId{5}, DimensionAnchor{{}, box, {}}, free, {}}).has_value());
    CHECK(withDim({DimensionId{5}, DimensionAnchor{{}, box, {1, 1}}, free, {}}).has_value());
    CHECK(withDim({DimensionId{5}, DimensionAnchor{{1e9, 0, 0}, std::nullopt, {}}, free, {}}).has_value());
    CHECK(withDim({DimensionId{5}, corner0, free, {0, std::nan(""), 0}}).has_value());
    CHECK(withDim({DimensionId{box.value}, corner0, free, {}}).has_value()); // number already used by the object
    CHECK(withDim({DimensionId{10}, corner0, free, {}}).has_value());        // not below nextId
}

TEST_CASE("dimensions save and reopen exactly; files without them are unchanged") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "2000, 1000", "500");
    const std::string before = serializeProject(editor.document(), std::nullopt);
    CHECK(before.find("dimensions") == std::string::npos);

    REQUIRE(editor.commit("dim", [](const Document& d) {
        return addDimension(d, anchorAt(d, {0, 0, 0}), anchorAt(d, {1, 0, 0}), {0, -0.4, 0}); // corner to midpoint
    }));
    REQUIRE(editor.commit("dim", [](const Document& d) {
        return addDimension(d, anchorAt(d, {2, 1, 0.5}), DimensionAnchor{{5.25, -1.125, 0}, std::nullopt, {}}, {0, 0, 0.3});
    }));
    const std::string text = serializeProject(editor.document(), std::nullopt);
    CHECK(text.find("\"dimensions\"") != std::string::npos);
    CHECK(text.find("\"dim-2\"") != std::string::npos);
    const auto loaded = parseProject(text);
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
    CHECK(sameContent(loaded->document, editor.document()));
    CHECK(serializeProject(loaded->document, std::nullopt) == text);

    // Broken dimension entries are reported with their path; nothing is applied.
    const auto broken = [&](const std::string& from, const std::string& to) {
        std::string t = text;
        const auto pos = t.find(from);
        REQUIRE(pos != std::string::npos);
        t.replace(pos, from.size(), to);
        const auto r = parseProject(t);
        REQUIRE_FALSE(r.ok());
        return r.error();
    };
    CHECK(broken("\"dim-2\"", "\"dimension-2\"").find("document.dimensions[0].id") != std::string::npos);
    CHECK(broken("\"object\": \"obj-1\"", "\"object\": \"obj-9\"").find("does not exist") != std::string::npos);
    CHECK(broken("\"offset\"", "\"offzet\"").find("offset: is missing") != std::string::npos);
}

TEST_CASE("anchors: corners, midpoints and free points") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "2000, 1000", "500");
    const Document& doc = editor.document();
    const auto* o = doc.find(box);
    CHECK(vertexCount(*o) == 8);
    CHECK(objectEdges(*o).size() == 12);
    const auto corner = anchorAt(doc, {2, 1, 0.5});
    REQUIRE(corner.object);
    CHECK(*resolveAnchor(doc, corner) == Vec3{2, 1, 0.5});
    const auto mid = anchorAt(doc, {2, 0.5, 0.5});
    CHECK(mid.vertices.size() == 2);
    CHECK(*resolveAnchor(doc, mid) == Vec3{2, 0.5, 0.5});
    const auto vertical = anchorAt(doc, {0, 1, 0.25});
    CHECK(vertical.vertices.size() == 2);
    const auto free = anchorAt(doc, {0.3, 0.3, 0.5}); // on the cap, not a feature
    CHECK_FALSE(free.object);
    CHECK(*resolveAnchor(doc, free) == Vec3{0.3, 0.3, 0.5});
    CHECK_FALSE(resolveAnchor(doc, DimensionAnchor{{}, ObjectId{42}, {0}}).has_value());
}

TEST_CASE("dimension geometry: perpendicular offsets and layout") {
    const Vec3 a{0, 0, 0}, b{2, 0, 0};
    // Looking down at a point beside a horizontal segment: the offset is the
    // horizontal perpendicular to it.
    const Ray down{{1.0, -0.7, 10.0}, {0, 0, -1}};
    const auto off = offsetFromRay(a, b, down);
    REQUIRE(off);
    CHECK(off->x == doctest::Approx(0.0));
    CHECK(off->y == doctest::Approx(-0.7));
    CHECK(off->z == doctest::Approx(0.0));
    // Looking straight along the segment gives no plane.
    CHECK_FALSE(offsetFromRay(a, b, Ray{{-5, 0, 0}, {1, 0, 0}}).has_value());
    CHECK(defaultOffsetDirection(a, b) == Vec3{0, -1, 0});
    CHECK(defaultOffsetDirection(a, Vec3{0, 0, 3}) == Vec3{1, 0, 0});
    const auto l = layoutDimension(a, b, {0, -0.5, 0});
    CHECK(l.lineA == Vec3{0, -0.5, 0});
    CHECK(l.lineB == Vec3{2, -0.5, 0});
    CHECK(l.label == Vec3{1, -0.5, 0});
    CHECK(l.length == 2.0);
}

TEST_CASE("camera projection is the inverse of the pointer ray") {
    Camera camera;
    const Vec2 size{1280, 720};
    for (const Vec2 px : {Vec2{640, 360}, Vec2{10, 20}, Vec2{1200, 700}}) {
        const Ray r = camera.rayThrough(px, size);
        const auto back = camera.project(r.origin + r.direction * 7.0, size);
        REQUIRE(back);
        CHECK(back->x == doctest::Approx(px.x));
        CHECK(back->y == doctest::Approx(px.y));
    }
    CHECK_FALSE(camera.project(camera.position() - camera.forward(), size).has_value()); // behind the eye
    CHECK(camera.worldPerPixel(camera.target(), size) > 0.0);
}

TEST_CASE("measuring works on the bare ground with grid snap off, and the line can be placed above the horizon") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    tools.setTool(Tool::Dimension);
    click(tools, at(editor, {0.0, 0.0, 0.0}, {100, 100}));
    tools.pointerMove(at(editor, {1.234, 0.0, 0.0}, {150, 100}));
    REQUIRE(measurePreview(editor).end);
    CHECK(measurePreview(editor).end->x == doctest::Approx(1.234)); // live, unsnapped
    click(tools, at(editor, {2.0, 0.0, 0.0}, {200, 100}));
    REQUIRE(measurePreview(editor).fixed);

    // Looking up past the horizon: no model, no ground, but the placement plane is hit.
    PointerInput sky;
    sky.ray = Ray{{1.0, -5.0, 1.0}, glm::normalize(Vec3{0.0, 1.0, 0.2})};
    sky.screen = {150, 20};
    sky.pixelAngle = kPixelAngle;
    REQUIRE_FALSE(intersectHorizontalPlane(sky.ray, 0.0).has_value());
    tools.pointerMove(sky);
    REQUIRE(measurePreview(editor).offset);
    const Vec3 moved = *measurePreview(editor).offset;
    CHECK(moved.x == doctest::Approx(0.0));
    CHECK(glm::length(moved) > 0.1);
    click(tools, sky);
    REQUIRE(editor.document().dimensions.size() == 1);
    CHECK(glm::length(editor.document().dimensions[0].offset - moved) == doctest::Approx(0.0));
    CHECK(layoutDimension(editor.document(), editor.document().dimensions[0])->length == doctest::Approx(2.0));

    // A first click into the sky does nothing.
    click(tools, sky);
    CHECK_FALSE(tools.active());
}

TEST_CASE("an attached dimension follows Move; Duplicate leaves it on the original") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "2000, 1000", "500");
    REQUIRE(editor.commit("dim", [](const Document& d) {
        return addDimension(d, anchorAt(d, {0, 0, 0}), DimensionAnchor{{0, -3, 0}, std::nullopt, {}}, {-0.5, 0, 0});
    }));
    CHECK(layoutDimension(editor.document(), editor.document().dimensions[0])->length == 3.0);
    REQUIRE(editor.commit("Move", [box](const Document& d) { return moveObject(d, box, {0, -1, 0}); }));
    const auto moved = layoutDimension(editor.document(), editor.document().dimensions[0]);
    CHECK(moved->a == Vec3{0, -1, 0});
    CHECK(moved->length == 2.0);
    REQUIRE(editor.commit("Duplicate", [box](const Document& d) { return duplicateObject(d, box, {5, 0, 0}); }));
    REQUIRE(editor.document().dimensions.size() == 1);
    CHECK(*editor.document().dimensions[0].a.object == box);
    // The copy's ID never collides with the dimension's.
    CHECK(editor.document().objects.back()->id.value != editor.document().dimensions[0].id.value);
}
