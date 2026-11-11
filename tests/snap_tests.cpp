// Endpoint, midpoint and inference snapping, driven through the tool state
// machine with synthetic pointer events (as the viewport produces them).
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/snap.hpp"
#include "core/geometry/solid.hpp"

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;

namespace {

// Looking straight down from 50 m, the default 12 px snap radius is then
// 0.3 m on the ground.
constexpr double kPixelAngle = 0.0005;

PointerInput snapAt(const Editor& editor, double x, double y, Vec2 screen = {0, 0}) {
    PointerInput in = downAt(editor, x, y, screen);
    in.pixelAngle = kPixelAngle;
    return in;
}

PointerInput snapLevel(double x, double y, double z, Vec2 screen) {
    PointerInput in = levelAt(x, y, z, screen);
    in.pixelAngle = kPixelAngle;
    return in;
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

} // namespace

TEST_CASE("snap features: deterministic corners and edges with a bounding sphere") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "500");
    const auto f = snapFeatures(*editor.document().find(id));
    CHECK(f.vertices.size() == 8);
    CHECK(f.edges.size() == 12);
    for (const auto& v : f.vertices) CHECK(glm::length(v - f.center) <= f.radius + 1e-12);
    // Midpoints are exact halves of their edges.
    bool found = false;
    for (std::size_t i = 0; i < f.edges.size(); ++i) {
        if (f.midpoint(i) == Vec3{1.0, 0.0, 0.0}) found = true;
    }
    CHECK(found);

    // A feature reference resolves to the same point, and follows edits.
    for (int i = 0; i < 8; ++i) CHECK(*resolveSnapFeature(editor.document(), id, SnapKind::Endpoint, i) == f.vertices[std::size_t(i)]);
    CHECK_FALSE(resolveSnapFeature(editor.document(), id, SnapKind::Endpoint, 8).has_value());
    CHECK_FALSE(resolveSnapFeature(editor.document(), ObjectId{999}, SnapKind::Endpoint, 0).has_value());

    // A planar rectangle has four corners and four edges.
    const ObjectId flat = test::makeBox(editor, tools, "300, 300", nullptr, {5.0, 5.0});
    const auto g = snapFeatures(*editor.document().find(flat));
    CHECK(g.vertices.size() == 4);
    CHECK(g.edges.size() == 4);
}

TEST_CASE("snap index: endpoints win over nearby midpoints, hidden points are ignored") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "500");
    SnapIndex index;
    CHECK(index.sync(editor.document()));
    CHECK_FALSE(index.sync(editor.document())); // unchanged: fast path

    const auto down = [](double x, double y) { return Ray{{x, y, 50.0}, {0.0, 0.0, -1.0}}; };
    const double r = kPixelAngle * 12.0;

    auto hit = index.nearest(down(2.05, 1.03), r);
    REQUIRE(hit);
    CHECK(hit->kind == SnapKind::Endpoint);
    CHECK(hit->point == Vec3{2.0, 1.0, 0.5}); // the top corner (nearest the eye)
    CHECK(*hit->objectId == id);
    CHECK(hit->featurePoint == hit->point);

    hit = index.nearest(down(1.02, 1.01), r);
    REQUIRE(hit);
    CHECK(hit->kind == SnapKind::Midpoint);
    CHECK(hit->point == Vec3{1.0, 1.0, 0.5});

    // Far from every feature: nothing.
    CHECK_FALSE(index.nearest(down(8.0, 8.0), r).has_value());
    // Excluding the object: nothing either.
    CHECK_FALSE(index.nearest(down(2.05, 1.03), r, std::nullopt, id).has_value());

    // A face between the eye and the box hides its corners.
    CHECK_FALSE(index.nearest(down(2.05, 1.03), r, 10.0).has_value());
    // The bottom corner is hidden by the cap the pointer is on; the top one is not.
    hit = index.nearest(down(1.97, 0.97), r, 49.5);
    REQUIRE(hit);
    CHECK(hit->point == Vec3{2.0, 1.0, 0.5});

    // Removing the object empties the index.
    REQUIRE(editor.commit("Delete", [&](const Document& d) { return deleteObject(d, id); }));
    CHECK(index.sync(editor.document()));
    CHECK(index.entries().empty());
}

TEST_CASE("the second rectangle snaps exactly to a corner of the first") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().step = 0.1;
    // An off-grid first rectangle: 0.37 x 0.23 m from (0.013, 0.021).
    tools.setTool(Tool::Rectangle);
    tools.snap().enabled = false;
    click(tools, downAt(editor, 0.013, 0.021, {10, 10}));
    REQUIRE_FALSE(tools.submitRectangleSize("370, 230").has_value());
    tools.snap().enabled = true;
    const auto& first = *editor.document().objects[0];
    const Vec3 corner = first.transform.translation + Vec3{0.37, 0.23, 0.0};

    // Hover near the corner (but off it and off-grid): the endpoint wins over the grid.
    tools.pointerMove(snapAt(editor, corner.x + 0.04, corner.y - 0.03, {100, 100}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Endpoint);
    CHECK(tools.hint().starts_with("[Endpoint]"));

    click(tools, snapAt(editor, corner.x + 0.04, corner.y - 0.03, {100, 100}));
    REQUIRE(tools.active());
    tools.pointerMove(snapAt(editor, 3.04, 2.06, {300, 300}));
    click(tools, snapAt(editor, 3.04, 2.06, {300, 300}));
    REQUIRE(editor.document().objects.size() == 2);
    const auto& second = *editor.document().objects[1];
    // Exactly the corner, bit for bit, not the nearest grid point.
    CHECK(second.transform.translation.x == corner.x);
    CHECK(second.transform.translation.y == corner.y);
    // The far corner is on the grid.
    CHECK(test::near(second.transform.translation.x + second.profile.boundary[2].x, 3.0, 1e-12));
}

TEST_CASE("a top corner of a box snaps the rectangle to the ground point below it") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "800");
    tools.setTool(Tool::Rectangle);
    tools.pointerMove(snapAt(editor, 1.03, 1.02));
    const auto& snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->kind == SnapKind::Endpoint);
    CHECK(snap->point == Vec3{1.0, 1.0, 0.0});
    CHECK(snap->featurePoint == Vec3{1.0, 1.0, 0.8});
    REQUIRE(snap->guides.size() == 1);
    CHECK(snap->guides[0].axis == 2); // vertical guide down to the ground
}

TEST_CASE("midpoint snap and inference guides from a remembered point") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().step = 0.1;
    test::makeBox(editor, tools, "2000, 1000", nullptr); // planar, (0,0)-(2,1)
    tools.setTool(Tool::Rectangle);

    // Midpoint of the far edge.
    tools.pointerMove(snapAt(editor, 1.04, 1.03));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Midpoint);
    CHECK(tools.snapIndicator()->point == Vec3{1.0, 1.0, 0.0});
    REQUIRE_FALSE(tools.inferenceReferences().empty());
    CHECK(tools.inferenceReferences().front() == Vec3{1.0, 1.0, 0.0});

    // Move away along Y: the pointer lines up with the midpoint's x and locks to it.
    tools.pointerMove(snapAt(editor, 1.12, 4.04));
    const auto& snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->kind == SnapKind::Inference);
    CHECK(snap->point.x == 1.0);
    CHECK(test::near(snap->point.y, 4.0, 1e-12)); // the other coordinate still on the grid
    REQUIRE(snap->guides.size() == 1);
    CHECK(snap->guides[0].axis == 1);
    CHECK(snap->guides[0].from == Vec3{1.0, 1.0, 0.0});

    // Far from every reference: plain grid snap, no guides.
    tools.pointerMove(snapAt(editor, 6.03, 6.04));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Grid);
    CHECK(tools.snapIndicator()->guides.empty());

    // Replacing the project (New / Open) forgets the old project's references.
    tools.pointerMove(snapAt(editor, 1.04, 1.03)); // remember the midpoint again
    editor.resetDocument(Document{});
    tools.pointerMove(snapAt(editor, 1.12, 4.04));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Grid);
    CHECK(test::near(tools.snapIndicator()->point.x, 1.1, 1e-12));
    CHECK(tools.inferenceReferences().empty());
    test::makeBox(editor, tools, "2000, 1000", nullptr);
    tools.setTool(Tool::Rectangle);

    // Point snap off: grid only, even right on a corner.
    tools.snap().points = false;
    tools.pointerMove(snapAt(editor, 2.04, 1.03));
    CHECK(tools.snapIndicator()->kind == SnapKind::Grid);
    CHECK(test::near(tools.snapIndicator()->point.x, 2.0, 1e-12));
}

TEST_CASE("a push/pull height snaps to another object's cap") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().step = 0.1;
    // Cap at 0.737 m, off the grid, and away from the drag ray so only the height lines up.
    test::makeBox(editor, tools, "1000, 1000", "737", {0.0, 5.0});
    const ObjectId flat = test::makeBox(editor, tools, "1000, 1000", nullptr, {3.0, 0.0});
    tools.setTool(Tool::PushPull);

    // Height inference: dragging near 0.737 locks to the cap height exactly.
    tools.pointerDown(snapAt(editor, 3.5, 0.5, {200, 300}));
    tools.pointerMove(snapLevel(3.5, 0.5, 0.75, {200, 200}));
    auto* preview = std::get_if<PushPullPreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->height == 0.737);
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Inference);
    REQUIRE(tools.snapIndicator()->guides.size() == 1);
    CHECK(tools.snapIndicator()->guides[0].to == Vec3{3.5, 0.5, 0.737});

    // Away from the cap level: grid snap.
    tools.pointerMove(snapLevel(3.5, 0.5, 1.41, {200, 150}));
    preview = std::get_if<PushPullPreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(test::near(preview->height, 1.4, 1e-12));
    CHECK(tools.snapIndicator()->kind == SnapKind::Grid);

    // Back near the cap and commit: the stored height is exactly the cap's.
    tools.pointerMove(snapLevel(3.5, 0.5, 0.72, {200, 210}));
    tools.pointerUp(snapLevel(3.5, 0.5, 0.72, {200, 210}));
    REQUIRE(editor.document().find(flat)->extrusion);
    CHECK(editor.document().find(flat)->extrusion->height == 0.737);
    CHECK_FALSE(tools.snapIndicator().has_value());
}

TEST_CASE("a push/pull height snaps to a corner the pointer is on") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "1234");
    const ObjectId flat = test::makeBox(editor, tools, "500, 500", nullptr, {3.0, 0.0});
    tools.setTool(Tool::PushPull);
    tools.pointerDown(snapAt(editor, 3.2, 0.2, {200, 300}));
    tools.pointerUp(snapAt(editor, 3.2, 0.2, {200, 300}));
    // Click-move-click: the second click is on the other box's top corner.
    PointerInput onCorner = snapAt(editor, 1.02, 0.98, {150, 200});
    tools.pointerMove(onCorner);
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Endpoint);
    tools.pointerDown(onCorner);
    tools.pointerUp(onCorner);
    REQUIRE(editor.document().find(flat)->extrusion);
    CHECK(editor.document().find(flat)->extrusion->height == 1.234);
}

TEST_CASE("snapPointer gives 3D points for other tools") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "500");
    SnapResult s = tools.snapPointer(snapAt(editor, 1.97, 0.04));
    CHECK(s.kind == SnapKind::Endpoint);
    CHECK(s.point == Vec3{2.0, 0.0, 0.5});
    CHECK(*s.objectId == id);
    CHECK(*resolveSnapFeature(editor.document(), id, SnapKind::Endpoint, s.feature) == s.point);

    s = tools.snapPointer(snapAt(editor, 0.6, 0.4)); // middle of the cap, no feature nearby
    CHECK(s.kind == SnapKind::OnFace);
    CHECK(test::near(s.point.z, 0.5));

    s = tools.snapPointer(snapAt(editor, 7.03, 7.02)); // empty ground
    CHECK(s.kind == SnapKind::Grid);
    CHECK(s.point.z == 0.0);

    // Without a pixel size (no viewport), point snapping is off.
    s = tools.snapPointer(downAt(editor, 1.97, 0.04));
    CHECK(s.kind == SnapKind::OnFace);
}
