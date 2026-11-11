// Push/pull of any planar face (roadmap item 14): side faces, recesses and
// bosses, openings cut through a wall, Ctrl for a new starting face.

#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/mesh.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <cmath>
#include <numbers>

using namespace pf;
using namespace pf::test;

namespace {

/// A box mesh w x d x h with its corner at the origin, numbered as meshFromProfile
/// numbers it (base 1, cap 2, side i = 3 + i; side 0 faces -Y).
mesh::Mesh boxMesh(double w, double d, double h) {
    ProfileObject o;
    o.profile.boundary = {{0, 0}, {w, 0}, {w, d}, {0, d}};
    o.extrusion = Extrusion{h};
    return mesh::meshFromProfile(o);
}

struct Solid {
    bool closed = false; ///< every directed triangle edge has exactly one twin running the other way
    double volume = 0.0; ///< signed: positive when the faces look outward
    [[nodiscard]] bool ok() const { return closed && volume > 0.0; }
};

/// Closedness and volume of a mesh from its one-sided triangulation.
Solid solid(const mesh::Mesh& m) {
    const mesh::Triangulation t = mesh::triangulate(m);
    Solid s;
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (std::size_t i = 0; i + 2 < t.indices.size(); i += 3) {
        const Vec3& a = t.positions[t.indices[i]];
        const Vec3& b = t.positions[t.indices[i + 1]];
        const Vec3& c = t.positions[t.indices[i + 2]];
        s.volume += glm::dot(a, glm::cross(b, c)) / 6.0;
        for (int k = 0; k < 3; ++k) ++directed[{t.indices[i + k], t.indices[i + (k + 1) % 3]}];
    }
    s.closed = !directed.empty() && std::ranges::all_of(directed, [&](const auto& kv) {
        const auto twin = directed.find({kv.first.second, kv.first.first});
        return kv.second == 1 && twin != directed.end() && twin->second == 1;
    });
    return s;
}

/// Draws a closed polygon into the mesh and returns the face it made inside.
mesh::FaceId drawLoop(mesh::Mesh& m, const std::vector<Vec3>& pts) {
    std::vector<mesh::FaceId> made;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto r = m.addEdge(pts[i], pts[(i + 1) % pts.size()]);
        REQUIRE(r);
        made.insert(made.end(), r->createdFaces.begin(), r->createdFaces.end());
    }
    // The face whose outline is exactly the drawn points.
    for (const auto f : made) {
        const auto* face = m.face(f);
        if (!face) continue;
        const auto vs = m.loopPositions(face->loops[0]);
        if (vs.size() != pts.size()) continue;
        if (std::ranges::all_of(pts, [&](const Vec3& p) {
                return std::ranges::any_of(vs, [&](const Vec3& q) { return glm::length(p - q) < 1e-9; });
            })) {
            return f;
        }
    }
    FAIL("no face for the drawn loop");
    return mesh::FaceId{};
}

std::vector<Vec3> windowOnFront(double x0, double x1, double z0, double z1) {
    return {{x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}};
}

} // namespace

TEST_CASE("push/pull: a window drawn on a 200 mm wall and pushed in 200 mm cuts a hole") {
    mesh::Mesh m = boxMesh(4.0, 0.2, 3.0);
    const auto window = drawLoop(m, windowOnFront(1.0, 2.0, 0.9, 2.1));
    REQUIRE(m.faces().size() == 7);
    const auto r = m.pushPull(window, -0.2);
    REQUIRE(r);
    CHECK(r->cut);
    CHECK(r->face.value == 0);
    CHECK_FALSE(validateMesh(m, "wall"));
    // Front and back with a hole each, four jambs, and the four other walls.
    CHECK(m.faces().size() == 10);
    int holed = 0;
    for (const auto& [id, f] : m.faces()) holed += f.loops.size() == 2 ? 1 : 0;
    CHECK(holed == 2);
    const Solid s = solid(m);
    CHECK(s.ok());
    CHECK(s.volume == doctest::Approx(4.0 * 0.2 * 3.0 - 1.0 * 1.2 * 0.2));
}

TEST_CASE("push/pull: a face on a wall makes a recess, a boss, and fills its hole again") {
    mesh::Mesh m = boxMesh(4.0, 0.2, 3.0);
    const auto window = drawLoop(m, windowOnFront(1.0, 2.0, 0.9, 2.1));
    const double full = 4.0 * 0.2 * 3.0;

    SUBCASE("recess") {
        const auto r = m.pushPull(window, -0.1);
        REQUIRE(r);
        CHECK_FALSE(r->cut);
        CHECK(r->face == window); // the pushed face keeps its id
        CHECK(r->createdFaces.size() == 4);
        CHECK_FALSE(validateMesh(m, "recess"));
        for (const Vec3& p : m.loopPositions(m.face(window)->loops[0])) CHECK(p.y == doctest::Approx(0.1));
        CHECK(m.face(window)->normal.y == doctest::Approx(-1.0));
        const Solid s = solid(m);
        CHECK(s.ok());
        CHECK(s.volume == doctest::Approx(full - 1.2 * 0.1));

        // Pulled back out by the same distance, the wall is whole again.
        const auto back = m.pushPull(window, 0.1);
        REQUIRE(back);
        CHECK_FALSE(validateMesh(m, "filled"));
        CHECK(m.faces().size() == 6);
        CHECK(m.vertices().size() == 8);
        CHECK(solid(m).volume == doctest::Approx(full));
    }
    SUBCASE("boss") {
        const auto r = m.pushPull(window, 0.05);
        REQUIRE(r);
        CHECK_FALSE(validateMesh(m, "boss"));
        const Solid s = solid(m);
        CHECK(s.ok());
        CHECK(s.volume == doctest::Approx(full + 1.2 * 0.05));
    }
    SUBCASE("too far: through the back of the wall is refused") {
        const mesh::Mesh before = m;
        std::string why;
        CHECK_FALSE(m.pushPull(window, -0.3, false, &why));
        CHECK(why.find("pass through") != std::string::npos);
        CHECK(m == before);
    }
}

TEST_CASE("push/pull: a door touching the floor pushed through makes a doorway") {
    mesh::Mesh m = boxMesh(4.0, 0.2, 3.0);
    m.addEdge({1.0, 0, 0}, {1.0, 0, 2.1});
    m.addEdge({1.0, 0, 2.1}, {2.0, 0, 2.1});
    const auto r0 = m.addEdge({2.0, 0, 2.1}, {2.0, 0, 0});
    REQUIRE(r0);
    REQUIRE(m.faces().size() == 7);
    std::optional<mesh::FaceId> door;
    for (const auto f : r0->createdFaces) {
        if (m.face(f) && m.loopPositions(m.face(f)->loops[0]).size() == 4) door = f;
    }
    REQUIRE(door);
    const auto r = m.pushPull(*door, -0.2);
    REQUIRE(r);
    CHECK(r->cut);
    CHECK_FALSE(validateMesh(m, "door"));
    const Solid s = solid(m);
    CHECK(s.ok());
    CHECK(s.volume == doctest::Approx(4.0 * 0.2 * 3.0 - 1.0 * 2.1 * 0.2));
    // The floor of the wall is split in two on either side of the doorway.
    int floors = 0;
    for (const auto& [id, f] : m.faces()) floors += f.normal.z < -0.5 && std::abs(m.loopPositions(f.loops[0]).front().z) < 1e-12 ? 1 : 0;
    CHECK(floors == 2);
}

TEST_CASE("push/pull: side and base faces of a box stretch it") {
    mesh::Mesh m = boxMesh(2.0, 1.0, 1.0);
    const auto side = m.pushPull(mesh::FaceId{3}, 0.5); // the -Y side
    REQUIRE(side);
    CHECK(side->stretched);
    const auto base = m.pushPull(mesh::FaceId{1}, 0.25); // the base, pulled down
    REQUIRE(base);
    CHECK(base->stretched);
    CHECK_FALSE(validateMesh(m, "stretched"));
    CHECK(m.faces().size() == 6);
    CHECK(solid(m).volume == doctest::Approx(2.0 * 1.5 * 1.25));
}

TEST_CASE("push/pull: Ctrl makes a new starting face; twice gives a block of three bands, then a step") {
    mesh::Mesh m = boxMesh(2.0, 1.0, 1.0);
    const auto first = m.pushPull(mesh::FaceId{2}, 0.5, true);
    REQUIRE(first);
    CHECK_FALSE(first->stretched);
    CHECK(first->face == mesh::FaceId{2});
    CHECK(first->createdFaces.size() == 4);
    const auto second = m.pushPull(mesh::FaceId{2}, 0.5, true);
    REQUIRE(second);
    CHECK_FALSE(validateMesh(m, "ctrl"));
    CHECK(m.faces().size() == 14);
    CHECK(solid(m).ok());
    CHECK(solid(m).volume == doctest::Approx(2.0 * 1.0 * 2.0));

    // Push the top band of the -Y side back by 400 mm: a stepped block.
    std::optional<mesh::FaceId> band;
    for (const auto f : second->createdFaces) {
        if (m.face(f)->normal.y < -0.5) band = f;
    }
    REQUIRE(band);
    const auto step = m.pushPull(*band, -0.4);
    REQUIRE(step);
    CHECK_FALSE(validateMesh(m, "step"));
    const Solid s = solid(m);
    CHECK(s.ok());
    CHECK(s.volume == doctest::Approx(2.0 * 1.0 * 2.0 - 2.0 * 0.4 * 0.5));
    // The tread of the step faces up at the height of the second band.
    bool tread = false;
    for (const auto& [id, f] : m.faces()) {
        if (f.normal.z > 0.5 && m.loopPositions(f.loops[0]).front().z == doctest::Approx(1.5)) tread = true;
    }
    CHECK(tread);
}

TEST_CASE("push/pull: Ctrl into the walls beside the face is refused") {
    mesh::Mesh m = boxMesh(2.0, 1.0, 1.0);
    const mesh::Mesh before = m;
    std::string why;
    CHECK_FALSE(m.pushPull(mesh::FaceId{2}, -0.5, true, &why));
    CHECK_FALSE(why.empty());
    CHECK(m == before);
}

TEST_CASE("push/pull: a box with a softened or erased edge still pushes") {
    SUBCASE("softened") {
        mesh::Mesh m = boxMesh(2.0, 1.0, 1.0);
        mesh::Edge e = *m.edge(mesh::EdgeId{9});
        e.soft = true;
        e.smooth = true;
        m.insertEdge(mesh::EdgeId{9}, e);
        const auto r = m.pushPull(mesh::FaceId{2}, 0.5);
        REQUIRE(r);
        CHECK(r->stretched);
        CHECK(m.edge(mesh::EdgeId{9})->soft);
    }
    SUBCASE("a vertical edge erased (two walls gone)") {
        mesh::Mesh m = boxMesh(2.0, 1.0, 1.0);
        REQUIRE(m.eraseEdge(mesh::EdgeId{9}));
        REQUIRE(m.faces().size() == 4);
        const auto r = m.pushPull(mesh::FaceId{2}, 0.5);
        REQUIRE(r);
        CHECK_FALSE(validateMesh(m, "open box"));
        for (const auto& [id, v] : m.vertices()) CHECK(v.position.z <= 1.5 + 1e-12);
        bool raised = false;
        for (const Vec3& p : m.loopPositions(m.face(mesh::FaceId{2})->loops[0])) raised = raised || p.z == doctest::Approx(1.5);
        CHECK(raised);
    }
    SUBCASE("a cap edge erased: the back wall pushes out with the top open") {
        mesh::Mesh m = boxMesh(2.0, 1.0, 1.0);
        REQUIRE(m.eraseEdge(mesh::EdgeId{5})); // cap edge over side 0 (-Y)
        const auto r = m.pushPull(mesh::FaceId{5}, 0.5); // side 2 faces +Y
        REQUIRE(r);
        CHECK_FALSE(validateMesh(m, "open top"));
    }
}

TEST_CASE("push/pull: a cylinder on a box keeps a round, smooth wall and a circle on its cap") {
    mesh::Mesh m = boxMesh(2.0, 2.0, 1.0);
    // A 24-sided circle on the cap, drawn as a curve.
    mesh::Curve c;
    c.kind = mesh::CurveKind::Circle;
    c.center = {1.0, 1.0, 1.0};
    c.radius = 0.5;
    c.sweep = 2.0 * std::numbers::pi;
    c.segments = 24;
    std::vector<Vec3> pts;
    for (int i = 0; i <= 24; ++i) {
        const double a = 2.0 * std::numbers::pi * i / 24.0;
        pts.push_back(c.center + Vec3{std::cos(a), std::sin(a), 0.0} * 0.5);
    }
    pts.back() = pts.front();
    const auto drawn = m.addCurve(c, pts);
    REQUIRE(drawn);
    std::optional<mesh::FaceId> disc;
    for (const auto f : drawn->createdFaces) {
        if (m.face(f) && m.face(f)->loops.size() == 1 && m.loopPositions(m.face(f)->loops[0]).size() == 24) disc = f;
    }
    REQUIRE(disc);
    const auto r = m.pushPull(*disc, 0.3);
    REQUIRE(r);
    CHECK_FALSE(validateMesh(m, "cylinder"));
    CHECK(m.curves().size() == 2);
    int smooth = 0;
    for (const auto& [id, e] : m.edges()) smooth += e.smooth ? 1 : 0;
    CHECK(smooth == 24);
    CHECK(solid(m).ok());
}

TEST_CASE("pushPullFace: a box's side face converts the box and stretches it; a typed recess is one undo entry") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = makeBox(editor, tools, "2000, 1000", "1000");
    REQUIRE_FALSE(editor.document().find(box)->isMeshGroup());
    mesh::FaceId pushed;
    REQUIRE(editor.commit("Push/pull", [&](const Document& d) {
        return pushPullFace(d, box, mesh::faceIdFor(FaceId::sideAt(1)), 0.5, &pushed); // the +X side
    }));
    const ProfileObject& o = *editor.document().find(box);
    CHECK(o.isMeshGroup());
    CHECK(objectBounds(o).size().x == doctest::Approx(2.5));
    CHECK(pushed == mesh::faceIdFor(FaceId::sideAt(1)));
    CHECK_FALSE(validateDocument(editor.document()));
    editor.undo();
    CHECK_FALSE(editor.document().find(box)->isMeshGroup());
}

TEST_CASE("Push/Pull tool: Accept: Ctrl-pull a box's cap twice, then push the top band back for a step") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = makeBox(editor, tools, "2000, 1000", "1000");
    tools.setTool(Tool::PushPull);
    CHECK(tools.hint().find("Ctrl makes a new starting face") != std::string::npos);
    tools.togglePushNewFace(); // tap Ctrl
    CHECK(tools.pushNewFace());
    editor.select(Selection{box, FaceId::cap()});
    REQUIRE_FALSE(tools.submitPushDistance("500").has_value());
    REQUIRE_FALSE(tools.submitPushDistance("500").has_value()); // the new cap stays selected
    const ProfileObject& o = *editor.document().find(box);
    REQUIRE(o.isMeshGroup());
    CHECK(o.mesh->faces().size() == 14); // three bands of four sides, base and cap
    CHECK(objectBounds(o).size().z == doctest::Approx(2.0));
    CHECK(solid(*o.mesh).ok());

    // Without Ctrl, the top band of the front pushed back 400 mm makes a step.
    tools.togglePushNewFace();
    CHECK_FALSE(tools.pushNewFace());
    PointerInput front;
    front.ray = Ray{{1.0, -10.0, 1.75}, {0, 1, 0}};
    front.pick = pickDocument(front.ray, editor.document());
    REQUIRE(front.pick);
    tools.pointerDown(front);
    REQUIRE(tools.active());
    REQUIRE_FALSE(tools.submitPushDistance("-400").has_value());
    const ProfileObject& stepped = *editor.document().find(box);
    const Solid s = solid(*stepped.mesh);
    CHECK(s.ok());
    CHECK(s.volume == doctest::Approx(2.0 * 1.0 * 2.0 - 2.0 * 0.4 * 0.5));
    CHECK_FALSE(validateDocument(editor.document()));
}

TEST_CASE("Push/Pull tool: holding Ctrl while clicking makes a new face for that push only") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = makeBox(editor, tools, "1000, 1000", "500");
    tools.setTool(Tool::PushPull);
    PointerInput cap = downAt(editor, 0.5, 0.5, {100, 100});
    cap.ctrl = true;
    tools.pointerDown(cap);
    REQUIRE(tools.active());
    REQUIRE_FALSE(tools.submitPushDistance("300").has_value());
    CHECK(editor.document().find(box)->mesh->faces().size() == 10);
    CHECK_FALSE(tools.pushNewFace());
}

TEST_CASE("Push/Pull tool: double-clicking a face repeats the last distance") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = makeBox(editor, tools, "1000, 1000", "500");
    tools.setTool(Tool::PushPull);
    editor.select(Selection{box, FaceId::sideAt(0)});
    REQUIRE_FALSE(tools.submitPushDistance("200").has_value()); // the front out 200 mm
    REQUIRE(editor.document().find(box)->isMeshGroup());
    const auto depth = editor.undoDepth();

    PointerInput first = downAt(editor, 0.5, 0.5, {100, 100});
    tools.pointerDown(first);
    tools.pointerUp(first);
    REQUIRE(tools.active());
    PointerInput second = downAt(editor, 0.5, 0.5, {100, 100});
    second.clicks = 2;
    tools.pointerDown(second);
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(objectBounds(*editor.document().find(box)).size().z == doctest::Approx(0.7));
}
