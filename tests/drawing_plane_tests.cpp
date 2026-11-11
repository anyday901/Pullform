// Drawing on any face and plane: the shared DrawingPlane (face under the
// pointer, arrow-key locks, plane fixed once drawing starts), rectangles on
// walls and tilted planes, rotated rectangles, and pushing a rectangle back
// into a wall.
#include "helpers.hpp"

#include <numbers>

#include "app/tools/drawing_plane.hpp"
#include "app/tools/rectangle_tool.hpp"
#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/plane.hpp"
#include "core/geometry/solid.hpp"

using namespace pf;
using pf::test::downAt;
using pf::test::near;

namespace {

/// A ray from `from` through `target`, picked against the document.
PointerInput toward(const Editor& editor, const Vec3& target, const Vec3& from, Vec2 screen = {0, 0}) {
    PointerInput in;
    in.ray = Ray{from, glm::normalize(target - from)};
    in.pick = pickDocument(in.ray, editor.document());
    in.screen = screen;
    return in;
}

/// Looking at the front of a wall (the y = 0 face, facing -Y) from 10 m away.
PointerInput atWall(const Editor& editor, double x, double z, Vec2 screen = {0, 0}) {
    return toward(editor, {x, 0.0, z}, {x, -10.0, z}, screen);
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

bool nearVec(const Vec3& a, const Vec3& b, double eps = 1e-9) { return glm::length(a - b) <= eps; }

/// World bounds of one mesh face of an object (its outer loop).
Bounds faceBounds(const ProfileObject& o, std::optional<FaceId> face) {
    Bounds b;
    REQUIRE(face);
    REQUIRE(face->kind == FaceKind::Mesh);
    const auto* f = o.mesh->face(mesh::FaceId{static_cast<std::uint32_t>(face->side)});
    REQUIRE(f);
    for (const Vec3& p : o.mesh->loopPositions(f->loops[0])) b.extend(localToWorld(o, p));
    return b;
}

/// A 4 m long, 200 mm thick, 3 m high wall from the origin.
ObjectId makeWall(Editor& editor, ToolController& tools) { return test::makeBox(editor, tools, "4000, 200", "3000"); }

} // namespace

TEST_CASE("plane frames: axes line up with the model and round-trip points") {
    const PlaneBasis ground = planeThrough({3.0, 4.0, 0.0}, {0.0, 0.0, 1.0});
    CHECK(ground == groundPlane());

    const PlaneBasis wall = planeThrough({1.0, 0.0, 2.0}, {0.0, -1.0, 0.0}); // front wall, facing -Y
    CHECK(wall.xAxis == Vec3{1.0, 0.0, 0.0});
    CHECK(wall.yAxis == Vec3{0.0, 0.0, 1.0});
    CHECK(planeNormal(wall) == Vec3{0.0, -1.0, 0.0});
    CHECK(wall.origin == Vec3{0.0, 0.0, 0.0});

    const PlaneBasis side = planeThrough({5.0, 1.0, 2.0}, {1.0, 0.0, 0.0}); // facing +X
    CHECK(side.xAxis == Vec3{0.0, 1.0, 0.0});
    CHECK(side.yAxis == Vec3{0.0, 0.0, 1.0});
    CHECK(side.origin == Vec3{5.0, 0.0, 0.0});

    const PlaneBasis down = planeThrough({0.0, 0.0, 2.0}, {0.0, 0.0, -1.0});
    CHECK(glm::cross(down.xAxis, down.yAxis) == Vec3{0.0, 0.0, -1.0});

    // A 30 degree roof slope: x stays horizontal, y runs uphill.
    const Vec3 n = glm::normalize(Vec3{0.0, -std::sin(0.5236), std::cos(0.5236)});
    const PlaneBasis roof = planeThrough({0.0, 0.0, 3.0}, n);
    CHECK(near(roof.xAxis.z, 0.0, 1e-12));
    CHECK(roof.yAxis.z > 0.0);
    CHECK(nearVec(glm::cross(roof.xAxis, roof.yAxis), n, 1e-12));
    const Vec3 p = fromPlane(roof, {1.25, -0.5});
    CHECK(near(planeDistance(roof, p), 0.0, 1e-12));
    CHECK(nearVec(Vec3(toPlane(roof, p), 0.0), Vec3{1.25, -0.5, 0.0}, 1e-12));

    // The rotation takes local X, Y, Z to the plane's axes.
    Transform t;
    t.rotation = planeRotation(wall);
    const Mat3 m = rotationMatrix(t);
    CHECK(m * Vec3{1.0, 0.0, 0.0} == wall.xAxis);
    CHECK(m * Vec3{0.0, 1.0, 0.0} == wall.yAxis);
    CHECK(m * Vec3{0.0, 0.0, 1.0} == Vec3{0.0, -1.0, 0.0});

    // A face tilted by 0.001 degrees keeps its own plane (no snap to the axis).
    const double tilt = 0.001 * std::numbers::pi / 180.0;
    const PlaneBasis slight = planeThrough({0.0, 0.0, 1.0}, {0.0, std::sin(tilt), std::cos(tilt)});
    CHECK(planeNormal(slight).y > 1e-6);
    CHECK(planeNormal(planeThrough({0.0, 0.0, 1.0}, {0.0, 1e-14, 1.0})) == Vec3{0.0, 0.0, 1.0});

    // Facing the viewer flips a plane seen from behind.
    CHECK(planeNormal(facingViewer(wall, {0.0, -1.0, 0.0})) == Vec3{0.0, 1.0, 0.0});
    CHECK(facingViewer(wall, {0.0, 1.0, 0.0}) == wall);
}

TEST_CASE("acceptance: draw a window rectangle on a wall, push it in 100 mm, then through to a hole") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = makeWall(editor, tools);
    tools.setTool(Tool::Rectangle);

    // Hovering the wall shows the plane of its face, not the ground.
    tools.pointerMove(atWall(editor, 1.03, 0.92));
    REQUIRE(tools.snapIndicator());
    CHECK(near(tools.snapIndicator()->point.y, 0.0));
    CHECK(near(tools.snapIndicator()->point.z, 0.9, 1e-12)); // grid in the wall's plane

    click(tools, atWall(editor, 1.03, 0.92, {100, 300}));
    REQUIRE(tools.active());
    tools.pointerMove(atWall(editor, 2.02, 2.08, {200, 200}));
    const auto* preview = std::get_if<RectanglePreview>(&editor.preview());
    REQUIRE(preview);
    REQUIRE(preview->placement);
    CHECK(preview->outline.size() == 4);
    CHECK(near(preview->width, 1.0, 1e-12));
    CHECK(near(preview->depth, 1.2, 1e-12));
    click(tools, atWall(editor, 2.02, 2.08, {200, 200}));
    REQUIRE_FALSE(tools.active());

    // The rectangle went into the wall: a hole in its front face and a face
    // filling it, which is now selected.
    REQUIRE(editor.document().objects.size() == 1);
    const ProfileObject& drawn = *editor.document().find(wall);
    REQUIRE(drawn.isMeshGroup());
    CHECK(drawn.mesh->faces().size() == 7);
    REQUIRE(editor.selection());
    const auto window = editor.selection()->face;
    const Bounds b = faceBounds(drawn, window);
    CHECK(nearVec(b.min, {1.0, 0.0, 0.9}, 1e-12));
    CHECK(nearVec(b.max, {2.0, 0.0, 2.1}, 1e-12));

    // The window, not the rest of the wall, is under the pointer.
    const auto onWindow = atWall(editor, 1.5, 1.5, {150, 250});
    REQUIRE(onWindow.pick);
    CHECK(onWindow.pick->objectId == wall);
    CHECK(onWindow.pick->faceId == *window);

    // Push it into the wall: the pointer behind the face (at y = +0.1).
    tools.setTool(Tool::PushPull);
    tools.pointerDown(onWindow);
    PointerInput behind;
    behind.ray = Ray{{1.8, 0.1, 10.0}, {0.0, 0.0, -1.0}};
    behind.screen = {150, 320};
    tools.pointerMove(behind);
    const auto* push = std::get_if<PushPullPreview>(&editor.preview());
    REQUIRE(push);
    CHECK(near(push->delta, -0.1, 1e-12));
    CHECK_FALSE(push->error.has_value());
    tools.pointerUp(behind);
    REQUIRE_FALSE(tools.active());

    const ProfileObject& recess = *editor.document().find(wall);
    CHECK(recess.mesh->faces().size() == 11); // 6 walls, 4 sides of the recess, its bottom
    const Bounds rb = faceBounds(recess, editor.selection()->face);
    CHECK(nearVec(rb.min, {1.0, 0.1, 0.9}, 1e-12));
    CHECK(nearVec(rb.max, {2.0, 0.1, 2.1}, 1e-12));
    CHECK_FALSE(validateDocument(editor.document()));

    // Another 100 mm reaches the back of the wall and cuts a hole.
    REQUIRE_FALSE(tools.submitPushDistance("-100").has_value());
    const ProfileObject& opened = *editor.document().find(wall);
    CHECK(opened.mesh->faces().size() == 10);
    PointerInput through;
    through.ray = Ray{{1.5, -10.0, 1.5}, {0.0, 1.0, 0.0}};
    CHECK_FALSE(pickDocument(through.ray, editor.document()).has_value()); // you can see through it
    CHECK_FALSE(validateDocument(editor.document()));

    // Undo brings back the recess, then the flat window; a file round trip keeps the opening.
    editor.undo();
    CHECK(editor.document().find(wall)->mesh->faces().size() == 11);
    editor.undo();
    CHECK(editor.document().find(wall)->mesh->faces().size() == 7);
    editor.redo();
    editor.redo();
    const auto loaded = parseProject(serializeProject(editor.document(), std::nullopt));
    REQUIRE(loaded);
    CHECK(sameContent(loaded->document, editor.document()));
}

TEST_CASE("typed sizes on a wall follow the pointer and stay exact; typed push back") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = makeWall(editor, tools);
    tools.setTool(Tool::Rectangle);
    click(tools, atWall(editor, 3.0, 2.0, {300, 100}));
    tools.pointerMove(atWall(editor, 2.5, 1.0, {250, 200})); // left and down
    REQUIRE_FALSE(tools.submitRectangleSize("600, 400").has_value());
    const Bounds b = faceBounds(*editor.document().find(wall), editor.selection()->face);
    CHECK(nearVec(b.min, {2.4, 0.0, 1.6}, 1e-12));
    CHECK(nearVec(b.max, {3.0, 0.0, 2.0}, 1e-12));

    // With it selected, a typed negative distance pushes it back.
    tools.setTool(Tool::PushPull);
    REQUIRE_FALSE(tools.submitPushDistance("-100").has_value());
    const Bounds pushed = faceBounds(*editor.document().find(wall), editor.selection()->face);
    CHECK(nearVec(pushed.min, {2.4, 0.1, 1.6}, 1e-12));
    CHECK(nearVec(pushed.max, {3.0, 0.1, 2.0}, 1e-12));
}

TEST_CASE("the plane stays fixed once the first corner is placed") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000, 1000", "800");
    tools.setTool(Tool::Rectangle);
    // First corner on the box's top...
    click(tools, downAt(editor, 0.2, 0.3, {100, 100}));
    // ...second beyond it, over empty ground: still at the top's height.
    const PointerInput off = toward(editor, {2.0, 1.5, 0.8}, {2.0, -8.5, 10.8}, {300, 300});
    CHECK_FALSE(off.pick.has_value());
    click(tools, off);
    // Hanging over the box's edge, it is a rectangle of its own; the box is untouched.
    REQUIRE(editor.document().objects.size() == 2);
    CHECK_FALSE(editor.document().find(box)->isMeshGroup());
    const Bounds b = objectBounds(*editor.document().objects[1]);
    CHECK(nearVec(b.min, {0.2, 0.3, 0.8}, 1e-12));
    CHECK(nearVec(b.max, {2.0, 1.5, 0.8}, 1e-12));
}

TEST_CASE("arrow keys lock a vertical or horizontal plane") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Rectangle);

    SUBCASE("Right: a plane facing the red axis, through the first corner") {
        CHECK(tools.key(ToolKey::ArrowRight));
        CHECK(tools.hint().find("red") != std::string::npos);
        const Vec3 eye{11.0, 2.0, 6.0};
        click(tools, toward(editor, {1.0, 1.0, 0.0}, eye, {100, 400}));
        REQUIRE(tools.active());
        click(tools, toward(editor, {1.0, 3.0, 2.0}, eye, {200, 200}));
        REQUIRE(editor.document().objects.size() == 1);
        const ProfileObject& r = *editor.document().objects[0];
        CHECK(nearVec(profileNormalWorld(r), {1.0, 0.0, 0.0}, 1e-15)); // facing the viewer at +X
        const Bounds b = objectBounds(r);
        CHECK(nearVec(b.min, {1.0, 1.0, 0.0}, 1e-12));
        CHECK(nearVec(b.max, {1.0, 3.0, 2.0}, 1e-12));
        // The lock ends with the rectangle.
        CHECK(tools.hint().find("Locked") == std::string::npos);
    }
    SUBCASE("Left: facing the green axis; the same key releases") {
        CHECK(tools.key(ToolKey::ArrowLeft));
        CHECK(tools.hint().find("green") != std::string::npos);
        CHECK(tools.key(ToolKey::ArrowLeft));
        CHECK(tools.hint().find("Locked") == std::string::npos);
        CHECK(tools.key(ToolKey::ArrowLeft));
        const Vec3 eye{2.0, -9.0, 6.0};
        click(tools, toward(editor, {0.0, 0.0, 0.0}, eye, {100, 400}));
        click(tools, toward(editor, {2.0, 0.0, 1.5}, eye, {200, 200}));
        REQUIRE(editor.document().objects.size() == 1);
        CHECK(nearVec(profileNormalWorld(*editor.document().objects[0]), {0.0, -1.0, 0.0}, 1e-15));
        CHECK(nearVec(objectBounds(*editor.document().objects[0]).max, {2.0, 0.0, 1.5}, 1e-12));
    }
    SUBCASE("an arrow during the gesture turns the plane about the first corner") {
        const Vec3 eye{2.0, -9.0, 6.0};
        click(tools, toward(editor, {0.0, 0.0, 0.0}, eye, {100, 400}));
        REQUIRE(tools.active());
        CHECK(tools.key(ToolKey::ArrowLeft)); // now the vertical plane y = 0
        click(tools, toward(editor, {1.0, 0.0, 1.0}, eye, {200, 200}));
        REQUIRE(editor.document().objects.size() == 1);
        CHECK(nearVec(objectBounds(*editor.document().objects[0]).max, {1.0, 0.0, 1.0}, 1e-12));
    }
}

TEST_CASE("Up locks a horizontal plane through a box corner; Down locks the hovered face") {
    Editor editor;
    ToolController tools(editor);
    makeWall(editor, tools);
    tools.setTool(Tool::Rectangle);

    SUBCASE("Down keeps drawing on the wall's plane past its edge") {
        tools.pointerMove(atWall(editor, 3.5, 2.5));
        CHECK(tools.key(ToolKey::ArrowDown));
        CHECK(tools.hint().find("hovered") != std::string::npos);
        // Past the wall's end the pointer is over nothing, but the plane holds.
        const PointerInput past = toward(editor, {5.0, 0.0, 2.0}, {5.0, -10.0, 2.0}, {100, 100});
        CHECK_FALSE(past.pick.has_value());
        click(tools, past);
        click(tools, toward(editor, {6.0, 0.0, 3.0}, {6.0, -10.0, 3.0}, {200, 50}));
        REQUIRE(editor.document().objects.size() == 2);
        const Bounds b = objectBounds(*editor.document().objects[1]);
        CHECK(nearVec(b.min, {5.0, 0.0, 2.0}, 1e-12));
        CHECK(nearVec(b.max, {6.0, 0.0, 3.0}, 1e-12));
    }
    SUBCASE("Up over the wall's front: a horizontal plane at the pointer's height") {
        CHECK(tools.key(ToolKey::ArrowUp));
        click(tools, atWall(editor, 1.0, 1.0, {100, 100}));
        const Vec3 eye{1.5, -6.0, 8.0};
        click(tools, toward(editor, {2.0, -1.0, 1.0}, eye, {200, 200}));
        REQUIRE(editor.document().objects.size() == 2);
        const Bounds b = objectBounds(*editor.document().objects[1]);
        CHECK(near(b.min.z, 1.0, 1e-12));
        CHECK(near(b.max.z, 1.0, 1e-12));
        CHECK(nearVec(b.min, {1.0, -1.0, 1.0}, 1e-12));
    }
}

TEST_CASE("rotated rectangle: three clicks in any direction on the plane") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::RotatedRectangle);
    CHECK(tools.hint().starts_with("Rotated Rectangle"));

    SUBCASE("clicked") {
        click(tools, downAt(editor, 0.0, 0.0, {100, 100}));
        tools.pointerMove(downAt(editor, 3.0, 4.0, {200, 200}));
        const auto* first = std::get_if<RectanglePreview>(&editor.preview());
        REQUIRE(first);
        CHECK(first->outline.size() == 2);
        CHECK(near(first->width, 5.0, 1e-12));
        click(tools, downAt(editor, 3.0, 4.0, {200, 200}));
        REQUIRE(tools.active());
        CHECK(tools.measurementLabel() == "Depth");
        // The plane cannot turn away from the placed side.
        CHECK_FALSE(tools.key(ToolKey::ArrowRight));
        click(tools, downAt(editor, -1.6, 1.2, {150, 300}));
        REQUIRE_FALSE(tools.active());
    }
    SUBCASE("typed length and depth") {
        click(tools, downAt(editor, 0.0, 0.0, {100, 100}));
        tools.pointerMove(downAt(editor, 0.6, 0.8, {200, 200}));
        REQUIRE_FALSE(tools.submit("5000").has_value());
        REQUIRE(tools.active());
        tools.pointerMove(downAt(editor, -0.5, 0.5, {150, 300})); // to the left of the side
        REQUIRE_FALSE(tools.submit("2000").has_value());
        REQUIRE_FALSE(tools.active());
    }
    REQUIRE(editor.document().objects.size() == 1);
    const ProfileObject& r = *editor.document().objects[0];
    CHECK(near(r.profile.boundary[1].x, 5.0, 1e-12));
    CHECK(near(r.profile.boundary[3].y, 2.0, 1e-12));
    CHECK(nearVec(profileNormalWorld(r), {0.0, 0.0, 1.0}, 1e-15));
    CHECK(nearVec(profileToWorld(r, {0.0, 0.0}), {0.0, 0.0, 0.0}, 1e-12));
    CHECK(nearVec(profileToWorld(r, {5.0, 0.0}), {3.0, 4.0, 0.0}, 1e-12));
    CHECK(nearVec(profileToWorld(r, {5.0, 2.0}), {1.4, 5.2, 0.0}, 1e-12));
    CHECK(nearVec(profileToWorld(r, {0.0, 2.0}), {-1.6, 1.2, 0.0}, 1e-12));
    // It push/pulls like any rectangle.
    tools.setTool(Tool::PushPull);
    REQUIRE_FALSE(tools.submitPushDistance("500").has_value());
    CHECK(checkSolid(buildMesh(*editor.document().objects[0])).ok());
}

TEST_CASE("rotated rectangle on the other side of its first side and on a wall") {
    // Depth to the right of the side starts from the far corner but keeps the
    // rectangle facing the plane's normal.
    const auto r = rotatedRectangle({0, 0, 0}, {2, 0, 0}, {0, 0, 1}, -1.0);
    REQUIRE(r);
    CHECK(r->width == 2.0);
    CHECK(r->depth == 1.0);
    const auto c = rectangleCorners(*r);
    CHECK(nearVec(c[0], {0.0, -1.0, 0.0}, 1e-15));
    CHECK(nearVec(c[2], {2.0, 0.0, 0.0}, 1e-15));
    CHECK_FALSE(rotatedRectangle({0, 0, 0}, {0, 0, 0}, {0, 0, 1}, 1.0));
    CHECK_FALSE(rotatedRectangle({0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0.0));

    Editor editor;
    ToolController tools(editor);
    makeWall(editor, tools);
    tools.setTool(Tool::RotatedRectangle);
    click(tools, atWall(editor, 1.0, 1.0, {100, 300}));
    click(tools, atWall(editor, 2.0, 2.0, {200, 200})); // a diagonal first side on the wall
    click(tools, atWall(editor, 0.5, 1.5, {80, 250}));
    REQUIRE(editor.document().objects.size() == 1); // drawn into the wall
    const ProfileObject& w = *editor.document().objects[0];
    REQUIRE(editor.selection());
    REQUIRE(editor.selection()->face);
    const auto* f = w.mesh->face(mesh::FaceId{static_cast<std::uint32_t>(editor.selection()->face->side)});
    REQUIRE(f);
    const auto pts = w.mesh->loopPositions(f->loops[0]);
    REQUIRE(pts.size() == 4);
    double perimeter = 0.0;
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(near(localToWorld(w, pts[i]).y, 0.0, 1e-12));
        perimeter += glm::length(pts[(i + 1) % 4] - pts[i]);
    }
    CHECK(near(perimeter, 2.0 * (std::sqrt(2.0) + std::sqrt(0.5)), 1e-12));
}

TEST_CASE("pushing a rectangle backwards keeps its dimensions on the same points") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", nullptr);
    DimensionAnchor a{{0.0, 0.0, 0.0}, id, {0}};
    DimensionAnchor b{{0.0, 0.0, 0.0}, id, {1, 2}};
    REQUIRE(editor.commit("Dimension", [&](const Document& d) { return addDimension(d, a, b, {0.0, -0.5, 0.0}); }));
    const auto before = layoutDimension(editor.document(), editor.document().dimensions[0]);
    REQUIRE(before);
    REQUIRE(editor.commit("Push back", [&](const Document& d) { return extrudeProfile(d, id, -0.25); }));
    const ProfileObject& o = *editor.document().find(id);
    CHECK(o.extrusion->height == 0.25);
    CHECK(nearVec(profileNormalWorld(o), {0.0, 0.0, -1.0}, 1e-15));
    const Bounds bounds = objectBounds(o);
    CHECK(nearVec(bounds.min, {0.0, 0.0, -0.25}, 1e-15));
    CHECK(nearVec(bounds.max, {2.0, 1.0, 0.0}, 1e-15));
    CHECK(checkSolid(buildMesh(o)).ok());
    const auto& dim = editor.document().dimensions[0];
    CHECK(nearVec(*resolveAnchor(editor.document(), dim.a), {0.0, 0.0, 0.0}, 1e-15));
    CHECK(nearVec(*resolveAnchor(editor.document(), dim.b), {2.0, 0.5, 0.0}, 1e-15));
    // A cap still cannot go below its base.
    CHECK_FALSE(setExtrusionHeight(editor.document(), id, -0.1));
}

TEST_CASE("DrawingPlane can be used by any tool") {
    Editor editor;
    ToolController tools(editor);
    makeWall(editor, tools);
    SnapSettings snap;
    PointSnapper snapper(editor, snap);
    ToolContext ctx(editor, snap, snapper);
    DrawingPlane plane;
    const auto hit = plane.hover(ctx, atWall(editor, 0.52, 0.47));
    REQUIRE(hit);
    CHECK(planeNormal(hit->plane) == Vec3{0.0, -1.0, 0.0});
    CHECK(nearVec(hit->point, {0.5, 0.0, 0.5}, 1e-12));
    CHECK_FALSE(plane.drawing());
    plane.begin(hit->plane, hit->point);
    CHECK(plane.drawing());
    // Over the ground in front of the wall, points stay on the wall's plane.
    const auto p = plane.point(ctx, toward(editor, {1.0, 0.0, 0.2}, {1.0, -10.0, 5.0}));
    REQUIRE(p);
    CHECK(near(p->y, 0.0, 1e-12));
    plane.end();
    CHECK_FALSE(plane.drawing());
    // Off the model the ground is the plane.
    const auto g = plane.hover(ctx, downAt(editor, 8.0, 8.0));
    REQUIRE(g);
    CHECK(g->plane == groundPlane());
}
