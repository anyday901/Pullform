#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/circle.hpp"
#include "core/geometry/follow_me.hpp"
#include "core/geometry/solid.hpp"

#include <doctest/doctest.h>

#include <map>
#include <numbers>

using namespace pf;

namespace {

/// Draws a closed polyline into a new group and returns the group and its face.
std::pair<ObjectId, std::uint32_t> drawProfile(Editor& editor, const std::vector<Vec3>& points) {
    REQUIRE(editor.commit("Profile", [&points](const Document& d) { return drawPolyline(d, std::nullopt, points, true); }));
    const ProfileObject& o = *editor.document().objects.back();
    REQUIRE(o.isMeshGroup());
    REQUIRE(o.mesh->faces().size() == 1);
    return {o.id, o.mesh->faces().begin()->first};
}

double polygonArea(const std::vector<Vec2>& p) {
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Vec2& u = p[i];
        const Vec2& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5;
}

std::size_t softEdges(const mesh::Mesh& m) {
    std::size_t n = 0;
    for (const auto& [id, e] : m.edges()) n += e.soft && e.smooth ? 1 : 0;
    return n;
}

/// A closed, outward-facing surface: every edge used once in each direction
/// by the faces' loops, and the volume (from the faces' own triangulations) positive.
struct Solid {
    bool closed = false;
    double volume = 0.0;
};
Solid meshSolid(const mesh::Mesh& m) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    Solid s;
    for (const auto& [id, f] : m.faces()) {
        for (const auto& loop : f.loops) {
            for (const auto& use : loop) ++directed[{m.useStart(use).value, m.useEnd(use).value}];
        }
        for (const auto& t : mesh::triangulateFace(m, mesh::FaceId{id})) {
            const Vec3 a = m.vertex(t[0])->position;
            const Vec3 b = m.vertex(t[1])->position;
            const Vec3 c = m.vertex(t[2])->position;
            s.volume += glm::dot(a, glm::cross(b, c)) / 6.0;
        }
    }
    s.closed = !directed.empty();
    for (const auto& [e, n] : directed) {
        const auto twin = directed.find({e.second, e.first});
        if (n != 1 || twin == directed.end() || twin->second != 1) s.closed = false;
    }
    return s;
}

} // namespace

TEST_CASE("paths chain in order, open or closed, and refuse branches and gaps") {
    mesh::Mesh m;
    const auto a = m.appendVertex({0, 0, 0});
    const auto b = m.appendVertex({1, 0, 0});
    const auto c = m.appendVertex({1, 1, 0});
    const auto d = m.appendVertex({0, 1, 0});
    const auto e = m.appendVertex({5, 5, 0});
    const auto ab = m.appendEdge(a, b);
    const auto bc = m.appendEdge(b, c);
    const auto cd = m.appendEdge(c, d);
    const auto da = m.appendEdge(d, a);
    const auto be = m.appendEdge(b, e);

    const auto open = mesh::chainPath(m, {cd, ab, bc}); // given out of order
    REQUIRE(open);
    CHECK_FALSE(open->closed);
    REQUIRE(open->points.size() == 4);
    CHECK((open->points.front() == Vec3{0, 0, 0} || open->points.front() == Vec3{0, 1, 0}));

    const auto loop = mesh::chainPath(m, {ab, bc, cd, da});
    REQUIRE(loop);
    CHECK(loop->closed);
    CHECK(loop->points.size() == 4);

    CHECK_FALSE(mesh::chainPath(m, {ab, bc, be}));   // branches at b
    CHECK_FALSE(mesh::chainPath(m, {ab, cd}));       // two pieces
    CHECK_FALSE(mesh::chainPath(m, {}));

    // A click on an edge of a loose loop picks the whole loop; at a branch, just the edge.
    CHECK(mesh::pathThrough(m, cd).size() == 4); // round to b both ways; b has three edges
    CHECK(mesh::pathThrough(m, be).size() == 1);
}

TEST_CASE("a square profile swept along an open L path makes a mitred, closed solid") {
    Editor editor;
    // A 100 mm square in the plane x = 0, the path from the origin along +X then +Y.
    const auto [profile, face] = drawProfile(editor, {{0, -0.05, 0.0}, {0, 0.05, 0.0}, {0, 0.05, 0.1}, {0, -0.05, 0.1}});
    Document doc = editor.document();
    REQUIRE(editor.commit("Path", [](const Document& d) { return drawPolyline(d, std::nullopt, {{0, 0, -1}, {1, 0, -1}, {1, 1, -1}}, false); }));
    const ProfileObject& pathGroup = *editor.document().objects.back();
    REQUIRE(pathGroup.id != profile);
    std::vector<ElementRef> path;
    for (const auto& [id, e] : pathGroup.mesh->edges()) path.push_back({pathGroup.id, ElementKind::Edge, id});

    std::vector<mesh::FaceId> created;
    REQUIRE(editor.commit("Follow Me", [&](const Document& d) {
        return followMe(d, ElementRef{profile, ElementKind::Face, face}, path, &created);
    }));
    const ProfileObject& swept = *editor.document().find(profile);
    CHECK_FALSE(validateObject(swept, "swept").has_value());
    // Two runs of four sides each, plus the far cap; the profile stays as the start cap.
    CHECK(created.size() == 9);
    CHECK(swept.mesh->faces().size() == 10);
    const Solid solid = meshSolid(*swept.mesh);
    CHECK(solid.closed);
    // A path offset 1 m below the profile: the profile runs 1 m along X, then 1 m along Y
    // to a cut square to Y; with the mitre each run is 1 m long at the square's centre.
    CHECK(solid.volume == doctest::Approx(0.02).epsilon(1e-9));
    // The path group is untouched.
    CHECK(editor.document().find(pathGroup.id)->mesh->faces().empty());
    CHECK(editor.undoLabel() == "Follow Me");
}

TEST_CASE("follow me refuses a profile along the path, its own edges and a branching path") {
    Editor editor;
    const auto [profile, face] = drawProfile(editor, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
    const ElementRef prof{profile, ElementKind::Face, face};
    REQUIRE(editor.commit("Path", [](const Document& d) { return drawEdge(d, std::nullopt, {3, 0, 0}, {5, 0, 0}); }));
    const ObjectId line = editor.document().objects.back()->id;
    const auto lineEdge = editor.document().find(line)->mesh->edges().begin()->first;
    // The path runs in the profile's own plane.
    auto r = followMe(editor.document(), prof, {{line, ElementKind::Edge, lineEdge}});
    REQUIRE_FALSE(r);
    CHECK(r.error().find("along the path") != std::string::npos);
    // The profile's own edges.
    const auto ownEdge = editor.document().find(profile)->mesh->edges().begin()->first;
    r = followMe(editor.document(), prof, {{profile, ElementKind::Edge, ownEdge}});
    REQUIRE_FALSE(r);
    CHECK(r.error().find("own edges") != std::string::npos);
    // No path.
    CHECK_FALSE(followMe(editor.document(), prof, {}));
    CHECK_FALSE(followMe(editor.document(), prof, {prof}));
}

TEST_CASE("acceptance: a crown moulding profile swept around a rectangular room") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId room = test::makeBox(editor, tools, "4000, 3000", "2400");
    // The cove moulding's section at the corner of the room, on the end wall
    // (x = 0): 120 mm down the wall, 120 mm across the ceiling, with a concave
    // quarter-round between, drawn as one closed profile in the plane x = 0.
    const double h = 2.4;
    const double r = 0.12;
    std::vector<Vec3> section{{0, 0, h}, {0, 0, h - r}};
    const int steps = 8;
    for (int i = 1; i < steps; ++i) {
            // About (0, r, h - r), from the wall point to the ceiling point.
        const double t = (std::numbers::pi / 2.0) * i / steps;
        section.push_back({0, r - r * std::cos(t), h - r + r * std::sin(t)});
    }
    section.push_back({0, r, h});
    const auto [moulding, face] = drawProfile(editor, section);
    const mesh::Mesh before = *editor.document().find(moulding)->mesh;
    std::vector<Vec2> yz;
    for (const auto& p : section) yz.push_back({p.y, p.z});
    const double area = std::abs(polygonArea(yz));
    Vec2 c{0.0};
    {
        // Centroid of the polygon (distance from the end wall, height).
        double ax = 0.0;
        double ay = 0.0;
        double a2 = 0.0;
        for (std::size_t i = 0; i < yz.size(); ++i) {
            const Vec2& u = yz[i];
            const Vec2& v = yz[(i + 1) % yz.size()];
            const double cr = u.x * v.y - v.x * u.y;
            a2 += cr;
            ax += (u.x + v.x) * cr;
            ay += (u.y + v.y) * cr;
        }
        c = {ax / (3.0 * a2), ay / (3.0 * a2)};
    }

    // The path: the ceiling's edge, picked as the room's top face.
    const std::vector<ElementRef> path = followMePath(editor.document(), ElementRef{room, ElementKind::Face, 2});
    REQUIRE(path.size() == 1);
    REQUIRE(editor.commit("Follow Me", [&](const Document& d) {
        return followMe(d, ElementRef{moulding, ElementKind::Face, face}, path);
    }));

    const ProfileObject& swept = *editor.document().find(moulding);
    CHECK_FALSE(validateObject(swept, "moulding").has_value());
    const mesh::Mesh& m = *swept.mesh;
    // Round the loop the profile face itself is gone; four runs of every profile edge.
    CHECK_FALSE(m.face(mesh::FaceId{face}));
    CHECK(m.faces().size() == 4 * before.edges().size());
    const Solid solid = meshSolid(*swept.mesh);
    CHECK(solid.closed);
    // Pappus for a mitred sweep: area times the length of the loop the
    // section's centroid travels, inset from each wall by its distance c.x.
    const double loop = 2.0 * ((4.0 - 2.0 * c.x) + (3.0 - 2.0 * c.x));
    CHECK(solid.volume == doctest::Approx(area * loop).epsilon(1e-9));
    // It fits the room: inside the walls and against the ceiling.
    const Bounds b = objectBounds(swept);
    CHECK(b.min.x == doctest::Approx(0.0));
    CHECK(b.max.x == doctest::Approx(4.0));
    CHECK(b.min.y == doctest::Approx(0.0));
    CHECK(b.max.y == doctest::Approx(3.0));
    CHECK(b.max.z == doctest::Approx(h));
    CHECK(b.min.z == doctest::Approx(h - r));
    // The room is not changed; undo brings the profile back.
    CHECK(editor.document().find(room)->extrusion.has_value());
    REQUIRE(editor.undo());
    CHECK(*editor.document().find(moulding)->mesh == before);
}

TEST_CASE("acceptance: a lathe-turned vase from a profile swept around a circle") {
    Editor editor;
    // The circle on the ground, 24 sides, its first corner on +X.
    const auto placement = circleFromRadius({0, 0, 0}, 0.1, 0.0, 24);
    REQUIRE(placement);
    REQUIRE(editor.commit("Circle", [&](const Document& d) { return createCircle(d, *placement); }));
    const ObjectId circle = editor.document().objects.back()->id;
    // Half the vase's outline in the plane y = 0, from the axis out to the
    // circle, up the belly and neck, and back to the axis at the lip.
    const std::vector<Vec3> outline{{0, 0, 0},       {0.1, 0, 0},     {0.14, 0, 0.08}, {0.13, 0, 0.16},
                                    {0.08, 0, 0.24}, {0.05, 0, 0.3},  {0.07, 0, 0.34}, {0, 0, 0.34}};
    const auto [vase, face] = drawProfile(editor, outline);

    // A click on one edge of the circle picks the whole circle.
    const auto path = followMePath(editor.document(), ElementRef{circle, ElementKind::Edge, 1});
    REQUIRE(path.size() == 24);
    REQUIRE(editor.commit("Follow Me", [&](const Document& d) {
        return followMe(d, ElementRef{vase, ElementKind::Face, face}, path);
    }));

    const ProfileObject& turned = *editor.document().find(vase);
    CHECK_FALSE(validateObject(turned, "vase").has_value());
    const mesh::Mesh& m = *turned.mesh;
    // Base and lip are one face each (not fans of triangles); the axis edge is gone.
    CHECK(m.faces().size() == 2 + 24 * 5); // five belly and neck edges
    std::size_t big = 0;
    for (const auto& [id, f] : m.faces()) big += f.loops[0].size() == 24 ? 1 : 0;
    CHECK(big == 2);
    CHECK_FALSE(m.findVertex({0, 0, 0}));
    CHECK_FALSE(m.findVertex({0, 0, 0.34}));
    const Solid solid = meshSolid(m);
    CHECK(solid.closed);
    CHECK(solid.volume > 0.0);
    const Bounds b = objectBounds(turned);
    CHECK(b.max.z == doctest::Approx(0.34));
    CHECK(b.min.z == doctest::Approx(0.0));
    CHECK(b.max.x == doctest::Approx(0.14));
    CHECK(b.min.x == doctest::Approx(-0.14));
    // Round the circle the edges between the segments are smooth.
    CHECK(softEdges(m) == 24 * 5);
    // The circle is left as it was.
    CHECK(editor.document().find(circle)->profile.kind == ProfileKind::Circle);
}

namespace {

/// A 100 mm square profile in the plane x = 0 and an L-shaped path of loose
/// edges 1 m below it, in two groups.
struct LScene {
    ObjectId profile;
    std::uint32_t face = 0;
    ObjectId path;
};

LScene makeL(Editor& editor) {
    const auto [profile, face] = drawProfile(editor, {{0, -0.05, 0.0}, {0, 0.05, 0.0}, {0, 0.05, 0.1}, {0, -0.05, 0.1}});
    REQUIRE(editor.commit("Path", [](const Document& d) { return drawPolyline(d, std::nullopt, {{0, 0, -1}, {1, 0, -1}, {1, 1, -1}}, false); }));
    return {profile, face, editor.document().objects.back()->id};
}

/// Pointer on the profile face, looking along +X.
PointerInput atProfile(const Editor& editor) {
    PointerInput in;
    in.ray = Ray{{-5.0, 0.01, 0.05}, {1.0, 0.0, 0.0}};
    in.pick = pickDocument(in.ray, editor.document());
    in.screen = {100, 100};
    return in;
}

/// Pointer on an edge of the path, as the viewport's element pick gives it.
PointerInput atPathEdge(const Editor& editor, ObjectId path, std::uint32_t edge) {
    PointerInput in;
    in.ray = Ray{{0.5, 0.0, 5.0}, {0.0, 0.0, -1.0}};
    in.element = ElementHit{ElementRef{path, ElementKind::Edge, edge}, {0.5, 0.0, -1.0}, 6.0};
    in.pick = pickDocument(in.ray, editor.document());
    in.screen = {200, 300};
    return in;
}

} // namespace

TEST_CASE("follow me tool: click the profile, hover the path for a preview, click to sweep") {
    Editor editor;
    ToolController tools(editor);
    const LScene scene = makeL(editor);
    tools.setTool(Tool::FollowMe);
    CHECK(tools.hint().find("click the profile face") != std::string::npos);

    tools.pointerMove(atProfile(editor));
    CHECK(editor.hoverElement() == ElementRef{scene.profile, ElementKind::Face, scene.face});
    tools.pointerUp(atProfile(editor));
    REQUIRE(tools.active());
    const auto* picked = std::get_if<FollowMePreview>(&editor.preview());
    REQUIRE(picked);
    CHECK_FALSE(picked->result.has_value());

    // Hovering one edge of the path picks the whole run, and shows the sweep in place of the profile.
    const std::uint32_t first = editor.document().find(scene.path)->mesh->edges().begin()->first;
    tools.pointerMove(atPathEdge(editor, scene.path, first));
    const auto* shown = std::get_if<FollowMePreview>(&editor.preview());
    REQUIRE(shown);
    CHECK(shown->path.size() == 2);
    REQUIRE(shown->result.has_value());
    CHECK(shown->result->mesh->faces().size() == 10);
    CHECK(replacedObject(editor.preview()) == scene.profile);
    CHECK(editor.undoDepth() == 2); // a preview is not history

    tools.pointerUp(atPathEdge(editor, scene.path, first));
    CHECK_FALSE(tools.active());
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    CHECK(editor.undoLabel() == "Follow Me");
    CHECK(editor.document().find(scene.profile)->mesh->faces().size() == 10);
    REQUIRE(editor.selection());
    CHECK(editor.selection()->objectId == scene.profile);
}

TEST_CASE("follow me tool: a selected path sweeps the clicked face; Escape drops a picked profile") {
    Editor editor;
    ToolController tools(editor);
    const LScene scene = makeL(editor);
    SelectionSet selection;
    std::vector<ElementRef> path;
    for (const auto& [id, e] : editor.document().find(scene.path)->mesh->edges()) path.push_back({scene.path, ElementKind::Edge, id});
    selection.addElements(path, std::nullopt);
    editor.setSelection(selection);

    tools.setTool(Tool::FollowMe);
    CHECK(tools.hint().find("selected path") != std::string::npos);
    tools.pointerMove(atProfile(editor));
    const auto* shown = std::get_if<FollowMePreview>(&editor.preview());
    REQUIRE(shown);
    CHECK(shown->result.has_value());
    tools.pointerUp(atProfile(editor));
    CHECK(editor.undoLabel() == "Follow Me");
    CHECK(editor.document().find(scene.profile)->mesh->faces().size() == 10);

    // Undo, clear the selection, pick the profile, then Escape: nothing changes.
    REQUIRE(editor.undo());
    editor.setSelection(SelectionSet{});
    const auto depth = editor.undoDepth();
    tools.pointerUp(atProfile(editor));
    REQUIRE(tools.active());
    CHECK(tools.cancel());
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth);
    CHECK(tools.submit("10").has_value());
}

TEST_CASE("follow me tool: a sweep that cannot be made says why and commits nothing") {
    Editor editor;
    ToolController tools(editor);
    // A flat profile on the ground and a line in its plane.
    drawProfile(editor, {{0, 0, 0}, {0.1, 0, 0}, {0.1, 0.1, 0}, {0, 0.1, 0}});
    REQUIRE(editor.commit("Path", [](const Document& d) { return drawEdge(d, std::nullopt, {1, 0, 0}, {2, 0, 0}); }));
    const ObjectId line = editor.document().objects.back()->id;
    const std::uint32_t edge = editor.document().find(line)->mesh->edges().begin()->first;
    tools.setTool(Tool::FollowMe);
    tools.pointerUp(test::downAt(editor, 0.05, 0.05, {10, 10}));
    REQUIRE(tools.active());
    PointerInput at;
    at.ray = Ray{{1.5, 0.0, 5.0}, {0.0, 0.0, -1.0}};
    at.element = ElementHit{ElementRef{line, ElementKind::Edge, edge}, {1.5, 0.0, 0.0}, 5.0};
    tools.pointerMove(at);
    const auto* shown = std::get_if<FollowMePreview>(&editor.preview());
    REQUIRE(shown);
    CHECK(shown->error.has_value());
    CHECK(tools.hint().find("along the path") != std::string::npos);
    const auto depth = editor.undoDepth();
    tools.pointerUp(at);
    CHECK(editor.undoDepth() == depth);
    REQUIRE(editor.message());
    CHECK(editor.message()->kind == Message::Kind::Error);
}

TEST_CASE("a profile drawn on the corner of the face it goes round joins that group and still sweeps") {
    Editor editor;
    const double h = 2.4;
    REQUIRE(editor.commit("Ceiling", [h](const Document& d) { return drawPolyline(d, std::nullopt, {{0, 0, h}, {2, 0, h}, {2, 1.5, h}, {0, 1.5, h}}, true); }));
    const ObjectId room = editor.document().objects.back()->id;
    // A triangle at the corner, in the plane x = 0: it touches the ceiling, so it joins its group
    // and splits the ceiling's edge along y.
    REQUIRE(editor.commit("Profile", [h](const Document& d) { return drawPolyline(d, std::nullopt, {{0, 0, h}, {0, 0, h - 0.1}, {0, 0.1, h}}, true); }));
    REQUIRE(editor.document().objects.size() == 1);
    const mesh::Mesh& m = *editor.document().find(room)->mesh;
    REQUIRE(m.faces().size() == 2);
    std::uint32_t profile = 0;
    std::uint32_t ceiling = 0;
    for (const auto& [id, f] : m.faces()) (std::abs(f.normal.z) > 0.5 ? ceiling : profile) = id;
    REQUIRE(editor.commit("Follow Me", [&](const Document& d) {
        return followMe(d, ElementRef{room, ElementKind::Face, profile}, followMePath(d, ElementRef{room, ElementKind::Face, ceiling}));
    }));
    const mesh::Mesh& swept = *editor.document().find(room)->mesh;
    // The ceiling stays; the profile shares its edge, so it stays too; three sides on each of four runs.
    CHECK(swept.face(mesh::FaceId{ceiling}));
    CHECK(swept.face(mesh::FaceId{profile}));
    CHECK(swept.faces().size() == 2 + 4 * 3);
}
