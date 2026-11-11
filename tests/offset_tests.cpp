// Offset (F): a face's boundary moved in or out in its own plane.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/offset.hpp"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <string>

using namespace pf;
using pf::mesh::Mesh;

namespace {

void drawLoop(Mesh& m, const std::vector<Vec3>& pts) {
    for (std::size_t i = 0; i < pts.size(); ++i) REQUIRE(m.addEdge(pts[i], pts[(i + 1) % pts.size()]));
}

std::vector<Vec3> square(double x0, double y0, double size, double z = 0.0) {
    return {{x0, y0, z}, {x0 + size, y0, z}, {x0 + size, y0 + size, z}, {x0, y0 + size, z}};
}

double faceArea(const Mesh& m, mesh::FaceId id) {
    const auto* f = m.face(id);
    REQUIRE(f);
    double area = 0.0;
    for (const auto& loop : f->loops) area += mesh::signedArea(m.loopPositions(loop), f->normal);
    return area;
}

/// The only face whose outer loop has `corners` corners and the given area.
std::optional<mesh::FaceId> faceWith(const Mesh& m, double area, std::size_t loops) {
    std::optional<mesh::FaceId> found;
    for (const auto& [id, f] : m.faces()) {
        if (f.loops.size() == loops && test::near(faceArea(m, mesh::FaceId{id}), area, 1e-9)) {
            REQUIRE_FALSE(found.has_value());
            found = mesh::FaceId{id};
        }
    }
    return found;
}

mesh::FaceId onlyFace(const Mesh& m) {
    REQUIRE(m.faces().size() == 1);
    return mesh::FaceId{m.faces().begin()->first};
}

} // namespace

TEST_CASE("offset: a square offset inward becomes a ring around a smaller square") {
    Mesh m;
    drawLoop(m, square(0, 0, 1));
    const auto face = onlyFace(m);
    const auto r = mesh::offsetFace(m, face, 0.1);
    REQUIRE(r);
    CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
    REQUIRE(m.faces().size() == 2);
    REQUIRE(r->ring == face); // the face keeps its id as the ring
    CHECK(m.face(face)->loops.size() == 2);
    CHECK(test::near(faceArea(m, face), 1.0 - 0.64, 1e-9));
    CHECK(faceWith(m, 0.64, 1).has_value());
    CHECK(r->createdEdges.size() == 4);
    CHECK(m.findVertex({0.1, 0.1, 0.0}).has_value());
    CHECK(m.findVertex({0.9, 0.9, 0.0}).has_value());
}

TEST_CASE("offset: a square offset outward gets a ring around it") {
    Mesh m;
    drawLoop(m, square(0, 0, 1));
    const auto face = onlyFace(m);
    const auto r = mesh::offsetFace(m, face, -0.25);
    REQUIRE(r);
    CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
    REQUIRE(m.faces().size() == 2);
    REQUIRE(r->ring.has_value());
    CHECK(*r->ring != face);
    CHECK(test::near(faceArea(m, *r->ring), 1.5 * 1.5 - 1.0, 1e-9));
    CHECK(test::near(faceArea(m, face), 1.0, 1e-9)); // the face itself is untouched
    CHECK(glm::dot(m.face(*r->ring)->normal, m.face(face)->normal) > 0.99);
    CHECK(m.findVertex({-0.25, -0.25, 0.0}).has_value());
}

TEST_CASE("offset: a concave L shape offsets with mitred corners") {
    // L: 2 x 2 with the top-right 1 x 1 quarter cut away.
    Mesh m;
    drawLoop(m, {{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}});
    const auto face = onlyFace(m);
    CHECK(test::near(faceArea(m, face), 3.0, 1e-9));
    const auto r = mesh::offsetFace(m, face, 0.2);
    REQUIRE(r);
    CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
    // The inner L: legs 0.6 wide; the reflex corner moves to (0.8, 0.8).
    const double inner = 1.6 * 0.6 + 0.6 * 1.0;
    REQUIRE(faceWith(m, inner, 1).has_value());
    CHECK(m.findVertex({0.8, 0.8, 0.0}).has_value());
    CHECK(m.findVertex({0.2, 0.2, 0.0}).has_value());
    CHECK(m.findVertex({1.8, 0.8, 0.0}).has_value());
    CHECK(test::near(faceArea(m, face), 3.0 - inner, 1e-9));
}

TEST_CASE("offset: too large an offset is refused and the mesh is unchanged") {
    Mesh m;
    drawLoop(m, square(0, 0, 1));
    const Mesh before = m;
    const auto face = onlyFace(m);
    CHECK_FALSE(mesh::offsetFace(m, face, 0.5));
    CHECK_FALSE(mesh::offsetFace(m, face, 0.7));
    CHECK_FALSE(mesh::offsetFace(m, face, 0.0));
    CHECK_FALSE(mesh::offsetFace(m, face, std::nan("")));
    CHECK(m == before);

    // An L whose legs are 1 wide: 0.5 would close the legs.
    Mesh l;
    drawLoop(l, {{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}});
    CHECK_FALSE(mesh::offsetLoops(l, onlyFace(l), 0.5));
    CHECK(mesh::offsetLoops(l, onlyFace(l), 0.45));
}

TEST_CASE("offset: a small chamfer that closes up is dropped") {
    // A 2 x 2 square with its (2, 2) corner cut by a 0.1 chamfer.
    Mesh m;
    drawLoop(m, {{0, 0, 0}, {2, 0, 0}, {2, 1.9, 0}, {1.9, 2, 0}, {0, 2, 0}});
    const auto face = onlyFace(m);
    const auto loops = mesh::offsetLoops(m, face, 0.3);
    REQUIRE(loops);
    REQUIRE(loops->size() == 1);
    CHECK(loops->front().points.size() == 4); // the chamfer edge is gone: a plain square
    CHECK(loops->front().sourceEdges.size() == 4);
    // A small offset keeps it.
    const auto small = mesh::offsetLoops(m, face, 0.01);
    REQUIRE(small);
    CHECK(small->front().points.size() == 5);
}

TEST_CASE("offset: a face with a hole shrinks its outer loop and grows the hole") {
    // A 3 x 3 square with a 1 x 1 hole in the middle (the inner face erased).
    Mesh m;
    drawLoop(m, square(0, 0, 3));
    drawLoop(m, square(1, 1, 1));
    REQUIRE(m.faces().size() == 2);
    const auto inner = faceWith(m, 1.0, 1);
    REQUIRE(inner);
    REQUIRE(m.eraseFace(*inner));
    const auto face = onlyFace(m);
    REQUIRE(m.face(face)->loops.size() == 2);

    SUBCASE("inward") {
        const auto r = mesh::offsetFace(m, face, 0.2);
        REQUIRE(r);
        CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
        // Outer ring, the middle face (with the grown hole) and a ring round the hole.
        REQUIRE(m.faces().size() == 3);
        CHECK(test::near(faceArea(m, face), 9.0 - 2.6 * 2.6, 1e-9));
        CHECK(faceWith(m, 2.6 * 2.6 - 1.4 * 1.4, 2).has_value());
        CHECK(faceWith(m, 1.4 * 1.4 - 1.0, 2).has_value());
        CHECK(m.findVertex({0.8, 0.8, 0.0}).has_value());
    }
    SUBCASE("outward") {
        const auto r = mesh::offsetFace(m, face, -0.2);
        REQUIRE(r);
        CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
        // The face, a ring round it, and a ring inside the hole (the hole shrinks).
        REQUIRE(m.faces().size() == 3);
        CHECK(test::near(faceArea(m, face), 8.0, 1e-9));
        CHECK(faceWith(m, 3.4 * 3.4 - 9.0, 2).has_value());
        CHECK(faceWith(m, 1.0 - 0.6 * 0.6, 2).has_value());
        CHECK(m.findVertex({1.2, 1.2, 0.0}).has_value());
        for (const auto& [id, f] : m.faces()) CHECK(f.normal.z > 0.99);
    }
    SUBCASE("outward far enough closes the hole, which is left out") {
        const auto loops = mesh::offsetLoops(m, face, -0.6);
        REQUIRE(loops);
        CHECK(loops->size() == 1);
    }
    SUBCASE("inward until the hole meets the outer loop is refused") {
        CHECK_FALSE(mesh::offsetLoops(m, face, 0.5));
        CHECK(mesh::offsetLoops(m, face, 0.45));
    }
}

TEST_CASE("offset: a face looking down offsets outward with rings looking down too") {
    Mesh m;
    drawLoop(m, square(0, 0, 1));
    const auto face = onlyFace(m);
    // Turn the face over (as the base of a box is).
    mesh::Face down = *m.face(face);
    mesh::Face flippedFace{{}, -down.normal};
    for (const auto& loop : down.loops) {
        mesh::Loop l;
        for (auto it = loop.rbegin(); it != loop.rend(); ++it) l.push_back({it->edge, !it->reversed});
        flippedFace.loops.push_back(l);
    }
    m.insertFace(face, flippedFace);
    REQUIRE_FALSE(mesh::validateMesh(m, "m").has_value());
    const auto r = mesh::offsetFace(m, face, -0.1);
    REQUIRE(r);
    CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
    REQUIRE(r->ring);
    CHECK(m.face(*r->ring)->normal.z < -0.99);
    // Inward from below: the inner square is (0.1..0.9) as from above.
    Mesh m2 = m;
    REQUIRE(mesh::offsetFace(m2, face, 0.1));
    CHECK(m2.findVertex({0.1, 0.1, 0.0}).has_value());
}

TEST_CASE("offset: a box's top face offset inward (the wall coping)") {
    Document doc;
    RectanglePlacement wall;
    wall.width = 2.0;
    wall.depth = 0.2;
    auto rect = createRectangle(doc, wall);
    REQUIRE(rect);
    const ObjectId id = *rect->objectId;
    auto box = extrudeProfile(rect->document, id, 1.0);
    REQUIRE(box);
    mesh::FaceId ring;
    const auto r = offsetFace(box->document, id, mesh::faceIdFor(pf::FaceId::cap()), 0.02, &ring);
    REQUIRE(r);
    const auto& o = *r->document.find(id);
    REQUIRE(o.isMeshGroup());
    const auto& m = *o.mesh;
    CHECK(ring == mesh::FaceId{2}); // the cap's id
    CHECK(m.faces().size() == 7);   // base, ring, four sides, the inner top
    CHECK(m.face(ring)->loops.size() == 2);
    CHECK(test::near(faceArea(m, ring), 2.0 * 0.2 - 1.96 * 0.16, 1e-9));
    CHECK(m.findVertex({0.02, 0.02, 1.0}).has_value());
    CHECK(m.findVertex({1.98, 0.18, 1.0}).has_value());
    // Validated as a whole document object.
    CHECK_FALSE(validateObject(o, "o").has_value());
}

TEST_CASE("offset: a cylinder's cap keeps its circle as a curve") {
    Document doc;
    CirclePlacement placement;
    placement.radius = 0.3;
    placement.segments = 48;
    auto c = createCircle(doc, placement);
    REQUIRE(c);
    const ObjectId id = *c->objectId;
    auto cyl = extrudeProfile(c->document, id, 0.5);
    REQUIRE(cyl);
    const auto r = offsetFace(cyl->document, id, mesh::faceIdFor(pf::FaceId::cap()), 0.05);
    REQUIRE(r);
    const auto& m = *r->document.find(id)->mesh;
    const double expected = 0.3 - 0.05 / std::cos(std::numbers::pi / 48.0);
    bool found = false;
    for (const auto& [cid, curve] : m.curves()) {
        if (curve.kind == mesh::CurveKind::Circle && test::near(curve.radius, expected, 1e-9) && test::near(curve.center.z, 0.5)) {
            found = true;
            CHECK(curve.segments == 48);
            CHECK(curve.edges.size() == 48);
            CHECK(test::near(std::abs(curve.sweep), 2.0 * std::numbers::pi, 1e-9));
        }
    }
    CHECK(found);
}

TEST_CASE("offset: an arc in a rounded face stays an arc") {
    // A 1 x 1 square whose top side bulges into a half circle (radius 0.5).
    Mesh m;
    REQUIRE(m.addEdge({1, 1, 0}, {1, 0, 0}));
    REQUIRE(m.addEdge({1, 0, 0}, {0, 0, 0}));
    REQUIRE(m.addEdge({0, 0, 0}, {0, 1, 0}));
    mesh::Curve arc;
    arc.kind = mesh::CurveKind::Arc;
    arc.center = {0.5, 1, 0};
    arc.radius = 0.5;
    arc.start = 0.0;
    arc.sweep = std::numbers::pi;
    std::vector<Vec3> pts;
    for (int i = 0; i <= 12; ++i) {
        const double a = std::numbers::pi * i / 12.0;
        pts.push_back({0.5 + 0.5 * std::cos(a), 1 + 0.5 * std::sin(a), 0});
    }
    REQUIRE(m.addCurve(arc, pts));
    const auto face = onlyFace(m);
    REQUIRE(mesh::offsetFace(m, face, 0.1));
    CHECK_FALSE(mesh::validateMesh(m, "m").has_value());
    bool inner = false;
    for (const auto& [cid, c] : m.curves()) {
        if (test::near(c.radius, 0.5 - 0.1 / std::cos(std::numbers::pi / 24.0), 1e-9)) {
            inner = true;
            CHECK(c.kind == mesh::CurveKind::Arc);
            CHECK(c.edges.size() == 12);
        }
    }
    CHECK(inner);
}

// ---- The Offset tool (F) ----------------------------------------------------------

namespace {

/// A 2000 x 200 mm wall, 1 m high, at the origin.
ObjectId makeWall(Editor& editor, ToolController& tools, Vec2 at = {0, 0}) {
    return test::makeBox(editor, tools, "2000, 200", "1000", at);
}

const mesh::Mesh& meshOf(const Editor& editor, ObjectId id) {
    const auto* o = editor.document().find(id);
    REQUIRE(o);
    REQUIRE(o->isMeshGroup());
    return *o->mesh;
}

/// Whether group `id` has a vertex at world point `p`.
bool hasWorldVertex(const Editor& editor, ObjectId id, const Vec3& p) {
    const auto* o = editor.document().find(id);
    REQUIRE(o);
    const Vec3 local = glm::transpose(rotationMatrix(o->transform)) * (p - o->transform.translation);
    return meshOf(editor, id).findVertex(local).has_value();
}

} // namespace

TEST_CASE("offset tool: Accept: offset a wall's top face by 20 mm and push the ring up to make a coping") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = makeWall(editor, tools);
    const std::size_t depth = editor.undoDepth();

    tools.setTool(Tool::Offset);
    CHECK(std::string(toolName(Tool::Offset)) == "Offset"); // as std::string: MSVC cannot print string_view
    // Click the top face near its front edge, then type the distance.
    tools.pointerDown(test::downAt(editor, 1.0, 0.001, {100, 100}));
    tools.pointerUp(test::downAt(editor, 1.0, 0.001, {100, 100}));
    REQUIRE(tools.active());
    REQUIRE(std::holds_alternative<OffsetPreview>(editor.preview()));
    REQUIRE_FALSE(tools.submit("20").has_value());
    CHECK_FALSE(tools.active());
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Offset 20 mm");

    const auto& m = meshOf(editor, wall);
    const mesh::FaceId ring{2}; // the wall's top face, now a ring
    REQUIRE(m.face(ring));
    CHECK(m.face(ring)->loops.size() == 2);
    CHECK(test::near(faceArea(m, ring), 2.0 * 0.2 - 1.96 * 0.16, 1e-9));
    CHECK(m.findVertex({0.02, 0.02, 1.0}).has_value());
    CHECK(m.findVertex({1.98, 0.18, 1.0}).has_value());
    // The ring is selected, ready for Push/Pull.
    REQUIRE(editor.selection());
    CHECK(editor.selection()->face == pf::FaceId::meshFace(ring.value));

    // Undo brings the plain box back in one step.
    REQUIRE(editor.undo());
    CHECK_FALSE(editor.document().find(wall)->isMeshGroup());
}

// The second half of the Accept check: pushing the ring up. A ring shares its
// edges with the wall below and with the inner face, so it needs push/pull of
// any face (roadmap item 14). Remove the skip when that lands.
TEST_CASE("offset tool: Accept: the ring pushes up into a coping" * doctest::skip()) {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = makeWall(editor, tools);
    tools.setTool(Tool::Offset);
    tools.pointerDown(test::downAt(editor, 1.0, 0.001, {100, 100}));
    REQUIRE_FALSE(tools.submit("20").has_value());
    REQUIRE(editor.commit("Push/pull", [&](const Document& d) { return pushPullFace(d, wall, mesh::FaceId{2}, 0.05); }));
    const auto& m = meshOf(editor, wall);
    CHECK(m.findVertex({0.0, 0.0, 1.05}).has_value());
    CHECK(m.findVertex({0.02, 0.02, 1.05}).has_value());
    CHECK(m.findVertex({0.02, 0.02, 1.0}).has_value()); // the inner top stays down
}

TEST_CASE("offset tool: the distance follows the pointer in and out of the face") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = makeWall(editor, tools);
    tools.snap().enabled = false; // exact pointer distances
    tools.setTool(Tool::Offset);
    tools.pointerDown(test::downAt(editor, 1.0, 0.001, {100, 100}));

    SUBCASE("inward by clicking") {
        tools.pointerMove(test::downAt(editor, 1.0, 0.051, {100, 150}));
        const auto* p = std::get_if<OffsetPreview>(&editor.preview());
        REQUIRE(p);
        CHECK(test::near(p->distance, 0.05, 1e-9));
        REQUIRE(p->loops.size() == 1);
        CHECK(p->loops.front().size() == 4);
        CHECK_FALSE(p->error.has_value());
        tools.pointerDown(test::downAt(editor, 1.0, 0.051, {100, 150}));
        CHECK_FALSE(tools.active());
        CHECK(meshOf(editor, wall).findVertex({0.05, 0.05, 1.0}).has_value());
    }
    SUBCASE("outward, then a typed distance keeps the direction") {
        // Off the wall: the pointer ray meets the top face's plane outside it.
        tools.pointerMove(test::downAt(editor, 1.0, -0.1, {100, 50}));
        const auto* p = std::get_if<OffsetPreview>(&editor.preview());
        REQUIRE(p);
        CHECK(p->distance < 0.0);
        REQUIRE_FALSE(tools.submit("30").has_value());
        const auto& m = meshOf(editor, wall);
        CHECK(m.findVertex({-0.03, -0.03, 1.0}).has_value());
        CHECK(m.findVertex({2.03, 0.23, 1.0}).has_value());
    }
    SUBCASE("too far in is refused and the gesture stays") {
        const std::size_t depth = editor.undoDepth();
        tools.pointerMove(test::downAt(editor, 1.0, 0.1, {100, 200})); // the middle of the 200 mm wall
        const auto* p = std::get_if<OffsetPreview>(&editor.preview());
        REQUIRE(p);
        CHECK(test::near(p->distance, 0.099, 1e-9));
        CHECK_FALSE(p->error.has_value());
        CHECK(tools.submit("150").has_value()); // more than half the wall's width
        CHECK(tools.active());
        CHECK(editor.undoDepth() == depth);
        tools.cancel();
        CHECK_FALSE(tools.active());
    }
}

TEST_CASE("offset tool: a typed distance offsets the selected face; double-click repeats") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = makeWall(editor, tools);
    const ObjectId b = makeWall(editor, tools, {0, 1});
    tools.setTool(Tool::Offset);
    // Nothing selected with a face: refused.
    editor.select(std::nullopt);
    CHECK(tools.submit("20").has_value());
    // Select the top of wall a (a click starts a gesture; Esc drops it but keeps the selection).
    tools.pointerDown(test::downAt(editor, 1.0, 0.1, {10, 10}));
    tools.cancel();
    editor.cancelPreview();
    REQUIRE(editor.selection());
    REQUIRE_FALSE(tools.submit("-20").has_value()); // negative: outward
    CHECK(meshOf(editor, a).findVertex({-0.02, -0.02, 1.0}).has_value());
    // Double-click wall b's top: the same offset again.
    PointerInput dbl = test::downAt(editor, 1.0, 1.1, {300, 10});
    dbl.clicks = 2;
    tools.pointerDown(dbl);
    CHECK_FALSE(tools.active());
    CHECK(hasWorldVertex(editor, b, {-0.02, 0.98, 1.0}));
}

TEST_CASE("offset: signed distance from a face's edges") {
    Mesh m;
    drawLoop(m, square(0, 0, 3));
    drawLoop(m, square(1, 1, 1));
    REQUIRE(m.eraseFace(*faceWith(m, 1.0, 1)));
    const auto face = onlyFace(m);
    CHECK(test::near(mesh::signedBoundaryDistance(m, face, {0.5, 1.5, 0.0}), 0.5));
    CHECK(test::near(mesh::signedBoundaryDistance(m, face, {1.5, 1.2, 0.0}), -0.2)); // in the hole
    CHECK(test::near(mesh::signedBoundaryDistance(m, face, {-0.25, 1.5, 0.0}), -0.25));
    CHECK(test::near(mesh::signedBoundaryDistance(m, face, {0.5, 1.5, 7.0}), 0.5)); // projected onto the plane
}
