// Line tool (L): chains of edges, typed lengths, loops that close into faces,
// and push/pull of the drawn face, driven through the real tool state machine.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/snap.hpp"
#include "core/geometry/solid.hpp"

#include <cmath>

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;

namespace {

/// Points the pointer at (x, y) on the ground (sets the direction for a typed length).
void pointAt(Editor& editor, ToolController& tools, double x, double y) { tools.pointerMove(downAt(editor, x, y, {400, 400})); }

void clickAt(Editor& editor, ToolController& tools, double x, double y, Vec2 screen = {100, 100}) {
    tools.pointerDown(downAt(editor, x, y, screen));
    tools.pointerUp(downAt(editor, x, y, screen));
}

const mesh::Mesh& onlyMesh(const Editor& editor) {
    REQUIRE(editor.document().objects.size() == 1);
    REQUIRE(editor.document().objects[0]->mesh);
    return *editor.document().objects[0]->mesh;
}

double faceArea(const mesh::Mesh& m, std::uint32_t face) {
    const auto* f = m.face(mesh::FaceId{face});
    REQUIRE(f);
    double area = mesh::signedArea(m.loopPositions(f->loops[0]), f->normal);
    for (std::size_t i = 1; i < f->loops.size(); ++i) area += mesh::signedArea(m.loopPositions(f->loops[i]), f->normal);
    return area;
}

/// The group's faces as one-sided triangles (the render mesh of a group is
/// two-sided), for checkSolid.
MeshData oneSided(const ProfileObject& o) {
    const mesh::Triangulation t = mesh::triangulate(*o.mesh);
    MeshData data;
    for (std::size_t i = 0; i < t.indices.size(); ++i) {
        const Vec3 n = o.mesh->face(t.triangleFaces[i / 3])->normal;
        data.positions.push_back(localToWorld(o, t.positions[t.indices[i]]));
        data.normals.push_back(n);
        data.indices.push_back(static_cast<std::uint32_t>(i));
    }
    return data;
}

/// Draws the L-shaped floor plan of the acceptance check by typing each
/// length with the pointer showing the direction: 6 m x 6 m with a 3 m x 3 m
/// corner missing.
void drawLPlan(Editor& editor, ToolController& tools) {
    tools.setTool(Tool::Line);
    clickAt(editor, tools, 0.0, 0.0);
    REQUIRE(tools.active());
    const struct {
        double x, y;
        const char* length;
    } steps[] = {{5, 0, "6000"}, {6, 2, "3000"}, {4, 3, "3000"}, {3, 5, "3m"}, {1, 6, "3000mm"}, {0, 1, "6000"}};
    for (const auto& s : steps) {
        pointAt(editor, tools, s.x, s.y);
        REQUIRE_FALSE(tools.submit(s.length).has_value());
    }
}

} // namespace

TEST_CASE("Line: an L-shaped floor plan typed by lengths becomes one face that push/pull extrudes") {
    Editor editor;
    ToolController tools(editor);
    drawLPlan(editor, tools);

    // Closing the loop made one face and ended the chain; each segment was one entry.
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 6);
    CHECK(editor.undoLabel() == "Draw line 6000 mm");
    const mesh::Mesh& plan = onlyMesh(editor);
    CHECK(plan.vertices().size() == 6);
    CHECK(plan.edges().size() == 6);
    REQUIRE(plan.faces().size() == 1);
    const auto faceId = plan.faces().begin()->first;
    CHECK(faceArea(plan, faceId) == doctest::Approx(27.0)); // 6 x 6 - 3 x 3
    CHECK(plan.faces().begin()->second.normal.z == doctest::Approx(1.0)); // a ground face looks up
    CHECK_FALSE(validateMesh(plan, "plan").has_value());

    // Push/pull: click the face, move up, click (click-move-click).
    tools.setTool(Tool::PushPull);
    tools.pointerDown(downAt(editor, 1.0, 1.0, {200, 300}));
    tools.pointerUp(downAt(editor, 1.0, 1.0, {200, 300}));
    REQUIRE(tools.active());
    tools.pointerMove(levelAt(1.0, 1.0, 2.43, {200, 200}));
    const auto* preview = std::get_if<PushPullPreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->face == FaceKind::Mesh);
    CHECK(preview->delta == doctest::Approx(2.4)); // grid-snapped
    REQUIRE(preview->result);
    CHECK(editor.undoDepth() == 6); // moving only previews
    tools.pointerDown(levelAt(1.0, 1.0, 2.43, {200, 200}));
    tools.pointerUp(levelAt(1.0, 1.0, 2.43, {200, 200}));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 7);

    const auto& solid = *editor.document().objects[0];
    const mesh::Mesh& m = *solid.mesh;
    CHECK(m.faces().size() == 8); // base, cap and six walls
    CHECK(m.vertices().size() == 12);
    CHECK(m.edges().size() == 18);
    CHECK_FALSE(validateMesh(m, "solid").has_value());
    const SolidCheck check = checkSolid(oneSided(solid));
    CHECK(check.ok());
    CHECK(check.signedVolume == doctest::Approx(27.0 * 2.4));
    CHECK(objectBounds(solid).size().z == doctest::Approx(2.4));
    // The cap is selected, so a typed distance pushes it further (its walls stretch).
    REQUIRE(editor.selection());
    REQUIRE(editor.selection()->face);
    CHECK(editor.selection()->face->kind == FaceKind::Mesh);
    REQUIRE_FALSE(tools.submitPushDistance("600").has_value());
    const auto& taller = *editor.document().objects[0];
    CHECK(taller.mesh->faces().size() == 8);
    CHECK(objectBounds(taller).size().z == doctest::Approx(3.0));
    CHECK(checkSolid(oneSided(taller)).signedVolume == doctest::Approx(27.0 * 3.0));
    // Pushing the cap down through the base is refused, and nothing changes.
    const auto depth = editor.undoDepth();
    CHECK(tools.submitPushDistance("-3000").has_value());
    CHECK(editor.undoDepth() == depth);

    // Undo walks back through the push/pulls and then one segment at a time.
    editor.undo();
    editor.undo();
    CHECK(onlyMesh(editor).faces().size() == 1);
    editor.undo();
    CHECK(onlyMesh(editor).faces().empty());
    CHECK(onlyMesh(editor).edges().size() == 5);
}

TEST_CASE("Line: Escape ends the chain and keeps the segments drawn so far") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Line);
    clickAt(editor, tools, 0.0, 0.0);
    clickAt(editor, tools, 2.0, 0.0);
    clickAt(editor, tools, 2.0, 1.5);
    CHECK(tools.active());
    CHECK(editor.undoDepth() == 2);
    CHECK(std::holds_alternative<LinePreview>(editor.preview()));
    CHECK(tools.cancel());
    CHECK_FALSE(tools.active());
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    CHECK(onlyMesh(editor).edges().size() == 2);
    CHECK(onlyMesh(editor).faces().empty());
    CHECK(editor.undoDepth() == 2);

    // A new chain from the open end joins the same group and can close the loop.
    clickAt(editor, tools, 2.0, 1.5);
    clickAt(editor, tools, 0.0, 1.5);
    CHECK(tools.active());
    clickAt(editor, tools, 0.0, 0.0);
    CHECK_FALSE(tools.active()); // the face ended the chain
    CHECK(onlyMesh(editor).faces().size() == 1);
}

TEST_CASE("Line: clicking the last point again ends the chain; a zero length is refused") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Line);
    CHECK(tools.submit("1000").has_value()); // no start point yet
    clickAt(editor, tools, 0.0, 0.0);
    CHECK(tools.submit("1000").has_value()); // no direction yet (pointer still on the start)
    CHECK(tools.submit("0, 0, 0").has_value());
    clickAt(editor, tools, 1.0, 0.0);
    clickAt(editor, tools, 1.0, 0.0);
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 1);
}

TEST_CASE("Line: arrow keys lock an axis for typed lengths; x, y, z draws an offset") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Line);
    clickAt(editor, tools, 1.0, 1.0);
    CHECK(tools.key(ToolKey::ArrowUp)); // lock Z
    tools.pointerMove(levelAt(1.0, 1.0, 1.73, {300, 200}));
    const auto* p = std::get_if<LinePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(p->axis == MoveAxis::Z);
    CHECK(p->locked);
    REQUIRE_FALSE(tools.submit("2500").has_value());
    const mesh::Mesh& m = onlyMesh(editor);
    REQUIRE(m.edges().size() == 1);
    const auto& o = *editor.document().objects[0];
    const auto& e = m.edges().begin()->second;
    const Vec3 a = localToWorld(o, m.vertex(e.v0)->position);
    const Vec3 b = localToWorld(o, m.vertex(e.v1)->position);
    CHECK(std::abs(b.z - a.z) == doctest::Approx(2.5));
    CHECK(a.x == doctest::Approx(1.0));
    CHECK(b.x == doctest::Approx(1.0));
    // The lock is still on; pressing the same key releases it.
    CHECK(tools.key(ToolKey::ArrowUp));
    REQUIRE_FALSE(tools.submit("0.5m, 0, 0").has_value());
    const auto& after = *editor.document().objects[0];
    CHECK(after.mesh->edges().size() == 2);
    CHECK(objectBounds(after).size().x == doctest::Approx(0.5));
    CHECK(objectBounds(after).size().z == doctest::Approx(2.5));
}

TEST_CASE("Line: the pointer infers the axes through the last point") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false; // only the inference moves the point
    tools.setTool(Tool::Line);
    clickAt(editor, tools, 0.0, 0.0);
    PointerInput in = downAt(editor, 3.07, 0.4, {300, 100});
    in.pixelAngle = 0.001; // 12 px at 50 m is 0.6 m
    tools.pointerMove(in);
    const auto* p = std::get_if<LinePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(p->axis == MoveAxis::X);
    CHECK_FALSE(p->locked);
    CHECK(p->end.x == doctest::Approx(3.07));
    CHECK(p->end.y == doctest::Approx(0.0));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Inference);
    // Far from every axis: the raw ground point.
    in = downAt(editor, 3.0, 2.0, {300, 160});
    in.pixelAngle = 0.001;
    tools.pointerMove(in);
    p = std::get_if<LinePreview>(&editor.preview());
    REQUIRE(p);
    CHECK_FALSE(p->axis.has_value());
    CHECK(p->end.y == doctest::Approx(2.0));
}

TEST_CASE("Line: the chain's own loose edges snap, so the loop can be closed by pointing at the start") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    tools.setTool(Tool::Line);
    const auto click = [&](double x, double y) {
        PointerInput in = downAt(editor, x, y, {100, 100});
        in.pixelAngle = 0.0005; // 0.3 m at 50 m
        tools.pointerDown(in);
        tools.pointerUp(in);
    };
    click(0.0, 0.0);
    click(2.0, 0.0);
    click(2.0, 2.0);
    click(0.0, 2.0);
    click(0.1, -0.1); // near the start: snaps onto its endpoint
    CHECK_FALSE(tools.active());
    CHECK(onlyMesh(editor).faces().size() == 1);
    CHECK(faceArea(onlyMesh(editor), onlyMesh(editor).faces().begin()->first) == doctest::Approx(4.0));
}

TEST_CASE("snap features of a drawn group include loose edges") {
    Editor editor;
    REQUIRE(editor.commit("Line", [](const Document& d) { return drawEdge(d, std::nullopt, {1, 1, 0}, {3, 1, 0}); }));
    const SnapFeatures f = snapFeatures(*editor.document().objects[0]);
    REQUIRE(f.vertices.size() == 2);
    REQUIRE(f.edges.size() == 1);
    CHECK(f.midpoint(0).x == doctest::Approx(2.0));
    CHECK(f.vertices[0].y == doctest::Approx(1.0));
}

TEST_CASE("mesh push/pull: a free face extrudes either way; a prism end stretches") {
    mesh::Mesh m;
    m.addEdge({0, 0, 0}, {2, 0, 0});
    m.addEdge({2, 0, 0}, {2, 1, 0});
    m.addEdge({2, 1, 0}, {0, 1, 0});
    m.addEdge({0, 1, 0}, {0, 0, 0});
    REQUIRE(m.faces().size() == 1);
    const mesh::FaceId base{m.faces().begin()->first};

    SUBCASE("down") {
        const auto r = m.pushPull(base, -0.5);
        REQUIRE(r);
        CHECK_FALSE(r->stretched);
        CHECK(m.faces().size() == 6);
        CHECK_FALSE(validateMesh(m, "down").has_value());
        CHECK(m.face(r->face)->normal.z == doctest::Approx(-1.0));
        CHECK(m.face(base)->normal.z == doctest::Approx(1.0)); // the original stays the top
    }
    SUBCASE("up, then stretch the cap") {
        const auto r = m.pushPull(base, 1.0);
        REQUIRE(r);
        CHECK(m.face(base)->normal.z == doctest::Approx(-1.0));
        const auto s = m.pushPull(r->face, 0.5);
        REQUIRE(s);
        CHECK(s->stretched);
        CHECK(m.faces().size() == 6);
        CHECK_FALSE(validateMesh(m, "stretched").has_value());
        for (const auto& [id, v] : m.vertices()) CHECK((v.position.z == doctest::Approx(0.0) || v.position.z == doctest::Approx(1.5)));
        // Not through the base: refused, nothing changes.
        const mesh::Mesh before = m;
        CHECK_FALSE(m.pushPull(r->face, -1.5));
        CHECK_FALSE(m.pushPull(r->face, -1.49995)); // walls shorter than 0.1 mm
        CHECK(m == before);
        // A side of the box is the end of a prism too: the box gets longer.
        const auto side = r->createdFaces.front();
        const auto longer = m.pushPull(side, 0.3);
        REQUIRE(longer);
        CHECK(longer->stretched);
        CHECK_FALSE(validateMesh(m, "longer").has_value());
    }
}
