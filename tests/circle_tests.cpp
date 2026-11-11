// Circle and Polygon tools: regular profiles stored with their true curve,
// drawn with the real tool state machine, extruded, snapped to and saved.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/circle.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/snap.hpp"
#include "core/geometry/solid.hpp"

#include <algorithm>
#include <numbers>

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;

namespace {

/// A ray looking straight down at (x, y), with point snapping on (1 mrad per pixel).
PointerInput snapDownAt(const Editor& editor, double x, double y, Vec2 screen = {0, 0}) {
    PointerInput in = downAt(editor, x, y, screen);
    in.pixelAngle = 0.001;
    return in;
}

const ProfileObject& only(const Editor& editor) {
    REQUIRE(editor.document().objects.size() == 1);
    return *editor.document().objects[0];
}

} // namespace

TEST_CASE("regular boundaries are recognised and their shape recovered") {
    const auto b = regularBoundary(0.3, 48, 0.25);
    REQUIRE(b.size() == 48);
    const auto shape = regularShape(b);
    REQUIRE(shape);
    CHECK(shape->radius == doctest::Approx(0.3));
    CHECK(shape->start == doctest::Approx(0.25));
    CHECK(shape->segments == 48);

    // Quarter turns land exactly on the axes.
    const auto square = regularBoundary(2.0, 4, 0.0);
    CHECK(square[1] == Vec2{0.0, 2.0});
    CHECK(square[2] == Vec2{-2.0, 0.0});

    auto bent = b;
    bent[7].x += 1e-4;
    CHECK_FALSE(regularShape(bent));
    auto clockwise = b;
    std::reverse(clockwise.begin() + 1, clockwise.end());
    CHECK_FALSE(regularShape(clockwise));
    CHECK_FALSE(regularShape(regularBoundary(0.3, 2, 0.0)));
}

TEST_CASE("segment counts and radii are checked") {
    CHECK_FALSE(checkSegments(3));
    CHECK_FALSE(checkSegments(999));
    CHECK(checkSegments(2));
    CHECK(checkSegments(1000));
    CHECK_FALSE(checkCircle(0.3, 48));
    CHECK(checkCircle(0.0, 48));
    CHECK(checkCircle(0.00005, 24));          // below 0.1 mm
    CHECK(checkCircle(0.001, 999));           // sides shorter than 10 um
    CHECK_FALSE(circleFromPoints({0, 0, 0}, {0.00001, 0, 0}, 24));
    CHECK(circleFromPoints({1, 2, 0}, {1, 3, 0}, 24)->start == doctest::Approx(std::numbers::pi / 2));
}

TEST_CASE("typed segment counts") {
    CHECK(*parseSegmentCount("24s").value() == 24);
    CHECK(*parseSegmentCount(" 48 S ").value() == 48);
    CHECK_FALSE(parseSegmentCount("2s").value()); // a count, but out of range
    CHECK_FALSE(parseSegmentCount("2.5s").value());
    CHECK_FALSE(parseSegmentCount("300").has_value()); // a length, not a count
    CHECK_FALSE(parseSegmentCount("300mm").has_value());
    CHECK_FALSE(parseSegmentCount("s").has_value());
}

TEST_CASE("acceptance: a 600 mm circle with 48 sides extrudes to a cylinder whose center snaps") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;

    // Circle tool: type the segment count first, click the center, type the radius.
    tools.setTool(Tool::Circle);
    REQUIRE_FALSE(tools.submit("48s").has_value());
    tools.pointerDown(downAt(editor, 1.0, 2.0, {300, 300}));
    tools.pointerUp(downAt(editor, 1.0, 2.0, {300, 300}));
    tools.pointerMove(downAt(editor, 1.5, 2.0, {340, 300}));
    const auto* preview = std::get_if<CirclePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->segments == 48);
    REQUIRE(preview->placement);
    CHECK(preview->placement->radius == doctest::Approx(0.5));
    CHECK(editor.undoDepth() == 0); // moving only previews
    REQUIRE_FALSE(tools.submit("300").has_value()); // radius 300 mm: a 600 mm circle
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 1);

    const ObjectId id = only(editor).id;
    {
        const auto& circle = only(editor);
        CHECK(circle.profile.kind == ProfileKind::Circle);
        CHECK(circle.name == "Circle " + std::to_string(id.value));
        CHECK(circle.transform.translation == Vec3{1.0, 2.0, 0.0});
        const auto shape = regularShape(circle.profile.boundary);
        REQUIRE(shape);
        CHECK(shape->segments == 48);
        CHECK(shape->radius == doctest::Approx(0.3));
        CHECK(objectBounds(circle).size().x == doctest::Approx(0.6));
        CHECK(editor.selection()->face == FaceId::profile());
        // The mesh keeps the true circle beside its 48 segments.
        const auto m = mesh::objectMesh(circle);
        REQUIRE(m->curves().size() == 1);
        const auto& c = m->curves().begin()->second;
        CHECK(c.kind == mesh::CurveKind::Circle);
        CHECK(c.radius == doctest::Approx(0.3));
        CHECK(c.segments == 48);
        CHECK(c.edges.size() == 48);
        CHECK(m->edges().size() == 48);
    }

    // Push/pull the circle (it is selected) into a 1 m cylinder.
    REQUIRE_FALSE(tools.submitPushDistance("1000").has_value());
    const auto& cylinder = *editor.document().find(id);
    REQUIRE(cylinder.extrusion);
    CHECK(cylinder.extrusion->height == doctest::Approx(1.0));
    CHECK(cylinder.name == "Cylinder " + std::to_string(id.value));
    CHECK(checkSolid(buildMesh(cylinder)).ok());
    const auto m = mesh::objectMesh(cylinder);
    CHECK(m->faces().size() == 50);
    CHECK(m->curves().size() == 2); // base and cap circles
    int soft = 0;
    for (const auto& [eid, e] : m->edges()) soft += e.soft && e.smooth ? 1 : 0;
    CHECK(soft == 48); // the side edges: a smooth surface, not 48 lines
    CHECK_FALSE(validateObject(cylinder, "cylinder"));

    // The cap's center snaps, from a pointer near it (not on a corner).
    const SnapResult top = tools.snapPointer(snapDownAt(editor, 1.004, 2.003));
    CHECK(top.kind == SnapKind::Center);
    CHECK(top.objectId == id);
    CHECK(top.point.x == doctest::Approx(1.0));
    CHECK(top.point.y == doctest::Approx(2.0));
    CHECK(top.point.z == doctest::Approx(1.0));
    CHECK(resolveSnapFeature(editor.document(), id, SnapKind::Center, top.feature) == top.point);
    // A rectangle can start from the center (on the cap, the face under the pointer).
    tools.setTool(Tool::Rectangle);
    tools.pointerDown(snapDownAt(editor, 1.004, 2.003));
    const auto* rect = std::get_if<RectanglePreview>(&editor.preview());
    REQUIRE(rect);
    CHECK(rect->start.x == doctest::Approx(1.0));
    CHECK(rect->start.y == doctest::Approx(2.0));
    CHECK(rect->start.z == doctest::Approx(1.0));
}

TEST_CASE("circle by click-click and by drag, with a pointer-set radius") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    tools.snap().step = 0.1;
    tools.setTool(Tool::Circle);
    CHECK(tools.measurementLabel() == "Radius (or segments, e.g. 24s)");

    // Press, drag, release: one gesture.
    tools.pointerDown(downAt(editor, 0.02, 0.01, {100, 100}));
    tools.pointerMove(downAt(editor, 0.5, 0.0, {160, 100}));
    tools.pointerUp(downAt(editor, 0.51, 0.02, {160, 100}));
    const auto& c = only(editor);
    CHECK(c.transform.translation == Vec3{0.0, 0.0, 0.0}); // grid-snapped center
    CHECK(regularShape(c.profile.boundary)->radius == doctest::Approx(0.5));
    CHECK(regularShape(c.profile.boundary)->segments == kDefaultCircleSegments);
    // The first vertex is where the pointer was released, so it is on the circle.
    CHECK(c.profile.boundary[0].x == doctest::Approx(0.5));
    CHECK(c.profile.boundary[0].y == doctest::Approx(0.0));

    // A radius below 0.1 mm keeps the center and reports the problem.
    tools.pointerDown(downAt(editor, 3.0, 3.0, {300, 300}));
    tools.pointerUp(downAt(editor, 3.0, 3.0, {300, 300}));
    tools.pointerDown(downAt(editor, 3.0, 3.0, {300, 300}));
    CHECK(tools.active());
    CHECK(editor.document().objects.size() == 1);
    CHECK(tools.cancel());
    CHECK(editor.undoDepth() == 1);

    // Typed radius without a center draws at the origin; bad text is refused.
    CHECK(tools.submit("abc").has_value());
    CHECK(tools.submit("1000s").has_value());
    REQUIRE_FALSE(tools.submit("250").has_value());
    CHECK(editor.document().objects.size() == 2);
}

TEST_CASE("polygon tool: sides, hard edges, name and center snap") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    tools.setTool(Tool::Polygon);
    CHECK(tools.hint().find("6 sides") != std::string::npos);
    REQUIRE_FALSE(tools.submit("8s").has_value());
    CHECK(tools.hint().find("8 sides") != std::string::npos);
    tools.pointerDown(downAt(editor, 0.0, 0.0, {100, 100}));
    tools.pointerUp(downAt(editor, 0.0, 0.0, {100, 100}));
    tools.pointerMove(downAt(editor, 0.0, 1.0, {100, 160}));
    tools.pointerDown(downAt(editor, 0.0, 1.0, {100, 160}));
    const auto& p = only(editor);
    CHECK(p.profile.kind == ProfileKind::Polygon);
    CHECK(p.name.starts_with("Polygon "));
    CHECK(p.profile.boundary.size() == 8);
    CHECK(p.profile.boundary[0].y == doctest::Approx(1.0)); // a corner where the pointer was

    REQUIRE_FALSE(tools.submitPushDistance("500").has_value());
    const auto& prism = only(editor);
    CHECK(prism.name.starts_with("Prism "));
    const auto m = mesh::objectMesh(prism);
    REQUIRE(m->curves().size() == 2);
    CHECK(m->curves().begin()->second.kind == mesh::CurveKind::Polygon);
    for (const auto& [eid, e] : m->edges()) CHECK_FALSE(e.soft); // a prism keeps its corners
    CHECK(checkSolid(buildMesh(prism)).ok());

    const SnapResult base = tools.snapPointer(snapDownAt(editor, 0.003, 0.002));
    CHECK(base.kind == SnapKind::Center);
    CHECK(base.point.z == doctest::Approx(0.5)); // the cap, the nearer of the two centers
}

TEST_CASE("circles and polygons scale uniformly, duplicate and become mesh groups with their curves") {
    Document doc;
    auto made = createCircle(doc, *circleFromRadius({2, 0, 0}, 0.5, 0.0, 12), ProfileKind::Circle);
    REQUIRE(made);
    const ObjectId id = *made->objectId;
    auto box = extrudeProfile(made->document, id, 0.4);
    REQUIRE(box);
    doc = box->document;
    const Vec3 baseCenter = objectBaseCenter(*doc.find(id));
    CHECK(baseCenter == Vec3{2.0, 0.0, 0.0});

    auto scaled = scaleObject(doc, id, {2.0, 2.0, 3.0});
    REQUIRE(scaled);
    const auto& big = *scaled->document.find(id);
    CHECK(regularShape(big.profile.boundary)->radius == doctest::Approx(1.0));
    CHECK(big.extrusion->height == doctest::Approx(1.2));
    CHECK(objectBaseCenter(big) == baseCenter);
    CHECK_FALSE(scaleObject(doc, id, {2.0, 1.0, 1.0})); // would no longer be a circle

    auto copy = duplicateObject(doc, id, {3, 0, 0});
    REQUIRE(copy);
    CHECK(copy->document.find(*copy->objectId)->name == "Cylinder " + std::to_string(copy->objectId->value));

    // A line drawn across the cap turns the cylinder into a mesh group; the
    // circles stay, so the center still snaps.
    const auto& cyl = *doc.find(id);
    const Vec3 a = profileToWorld(cyl, cyl.profile.boundary[0], 0.4);
    const Vec3 b = profileToWorld(cyl, cyl.profile.boundary[6], 0.4);
    auto drawn = drawEdge(doc, id, a, b);
    REQUIRE(drawn);
    const auto& group = *drawn->document.find(id);
    REQUIRE(group.isMeshGroup());
    CHECK(group.mesh->curves().size() == 2);
    const auto f = snapFeatures(group);
    REQUIRE(f.curveCenters.size() == 2);
    CHECK(f.curveCenters[1].z == doctest::Approx(0.4));

    CHECK_FALSE(createCircle(doc, *circleFromRadius({0, 0, 0}, 1.0, 0.0, 6), ProfileKind::Rectangle));
}

TEST_CASE("circles and polygons save and open with their curves") {
    Document doc;
    auto c = createCircle(doc, *circleFromRadius({1, 1, 0}, 0.3, 0.1, 48), ProfileKind::Circle);
    REQUIRE(c);
    auto cyl = extrudeProfile(c->document, *c->objectId, 1.0);
    REQUIRE(cyl);
    auto p = createCircle(cyl->document, *circleFromRadius({-2, 0, 0}, 0.5, 0.0, 5), ProfileKind::Polygon);
    REQUIRE(p);
    doc = p->document;

    const std::string text = serializeProject(doc, std::nullopt);
    CHECK(text.find(R"("kind": "circle")") != std::string::npos);
    CHECK(text.find(R"("kind": "polygon")") != std::string::npos);
    const auto loaded = parseProject(text);
    REQUIRE(loaded);
    CHECK(sameContent(loaded->document, doc));
    const auto m = mesh::objectMesh(*loaded->document.objects[0]);
    CHECK(m->curves().size() == 2);
    CHECK_FALSE(validateDocument(loaded->document));

    // A circle whose boundary is not regular is refused.
    auto bentDoc = doc;
    ProfileObject bentCircle = *doc.objects[0];
    bentCircle.profile.boundary[3].x += 1e-4;
    bentDoc.objects[0] = std::make_shared<const ProfileObject>(bentCircle);
    CHECK_FALSE(parseProject(serializeProject(bentDoc, std::nullopt)));
}

TEST_CASE("a circle on a wall pushed in 100 mm: a round hole-shaped recess facing the wall") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = test::makeBox(editor, tools, "4000, 200", "3000");
    const auto atWall = [&editor](double x, double z, Vec2 screen) {
        PointerInput in;
        in.ray = Ray{{x, -10.0, z}, {0.0, 1.0, 0.0}};
        in.pick = pickDocument(in.ray, editor.document());
        in.screen = screen;
        return in;
    };
    tools.setTool(Tool::Circle);
    tools.pointerDown(atWall(2.0, 1.5, {100, 100}));
    tools.pointerUp(atWall(2.0, 1.5, {100, 100}));
    REQUIRE(tools.active());
    tools.pointerMove(atWall(2.0, 2.0, {100, 60})); // straight up the wall
    REQUIRE_FALSE(tools.submit("300").has_value());
    REQUIRE(editor.document().objects.size() == 2);
    const ProfileObject& disc = *editor.document().objects[1];
    CHECK(disc.profile.kind == ProfileKind::Circle);
    const Vec3 n = profileNormalWorld(disc);
    CHECK(n.y == doctest::Approx(-1.0));
    const Bounds b = objectBounds(disc);
    CHECK(b.min.x == doctest::Approx(1.7));
    CHECK(b.max.x == doctest::Approx(2.3));
    CHECK(b.min.z == doctest::Approx(1.2));
    CHECK(b.max.z == doctest::Approx(1.8));
    CHECK(std::abs(b.max.y) < 1e-12);
    // The first corner points where the pointer was: straight up.
    const auto top = objectVertex(disc, 0);
    REQUIRE(top);
    CHECK(top->z == doctest::Approx(1.8));

    // A dimension on corner 5, then push the circle 100 mm into the wall.
    const Vec3 corner5 = *objectVertex(disc, 5);
    REQUIRE(editor.commit("Dimension", [&](const Document& d) {
        return addDimension(d, DimensionAnchor{{}, disc.id, {5}}, DimensionAnchor{{0, -1, 0}, std::nullopt, {}}, Vec3{0, -0.2, 0});
    }));
    editor.select(Selection{disc.id, FaceId::profile()});
    REQUIRE_FALSE(tools.submitPushDistance("-100").has_value());
    const ProfileObject& recess = *editor.document().find(disc.id);
    REQUIRE(recess.extrusion);
    CHECK(recess.extrusion->height == doctest::Approx(0.1));
    CHECK(profileNormalWorld(recess).y == doctest::Approx(1.0)); // extruded into the wall
    const Bounds rb = objectBounds(recess);
    CHECK(rb.min.x == doctest::Approx(1.7));
    CHECK(rb.max.y == doctest::Approx(0.1));
    CHECK(checkSolid(buildMesh(recess)).ok());
    CHECK_FALSE(validateDocument(editor.document()));
    // The footprint did not move, and the dimension still measures the same point.
    const auto& dim = editor.document().dimensions.back();
    const auto end = resolveAnchor(editor.document(), dim.a);
    REQUIRE(end);
    CHECK(glm::length(*end - corner5) < 1e-9);
    CHECK(editor.document().find(wall)->extrusion->height == 3.0);

    // The far circle's center snaps from inside the wall's plane.
    const auto f = snapFeatures(recess);
    REQUIRE(f.curveCenters.size() == 2);
    CHECK(f.curveCenters[0].x == doctest::Approx(2.0));
    CHECK(f.curveCenters[0].z == doctest::Approx(1.5));
    CHECK(std::abs(f.curveCenters[0].y) < 1e-12);
    CHECK(f.curveCenters[1].y == doctest::Approx(0.1));

    const auto loaded = parseProject(serializeProject(editor.document(), std::nullopt));
    REQUIRE(loaded);
    CHECK(sameContent(loaded->document, editor.document()));
}
