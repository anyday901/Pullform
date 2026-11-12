// Intersect Faces (roadmap item 19): With Model, With Selection, With Context.
#include "helpers.hpp"
#include "app/tools/tool_controller.hpp"

#include "core/commands/operations.hpp"
#include "core/commands/selection.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/intersect.hpp"
#include "core/geometry/mesh.hpp"

#include <glm/geometric.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <algorithm>
#include <numbers>

using namespace pf;

namespace {

/// A w x w x h box with its base corner at `corner` (a rectangle extruded up).
ObjectId box(Document& doc, const Vec3& corner, double w = 2.0, double h = 2.0) {
    auto rect = createRectangle(doc, RectanglePlacement{corner, w, w});
    REQUIRE(rect);
    auto solid = extrudeProfile(rect->document, *rect->objectId, h);
    REQUIRE(solid);
    doc = solid->document;
    return *rect->objectId;
}

std::vector<ElementRef> allFaces(const Document& doc, ObjectId id) {
    return selectedFaces(doc, SelectionSet::ofObjects({id}));
}

const mesh::Mesh& meshOf(const Document& doc, ObjectId id) {
    const auto* o = doc.find(id);
    REQUIRE(o);
    REQUIRE(o->isMeshGroup());
    return *o->mesh;
}

double area(const mesh::Mesh& m) {
    double total = 0.0;
    for (const auto& [fid, f] : m.faces()) {
        for (const auto& loop : f.loops) total += mesh::signedArea(m.loopPositions(loop), f.normal);
    }
    return total;
}

Vec3 toWorld(const Document& doc, ObjectId id, const Vec3& local) {
    const auto* o = doc.find(id);
    return o->transform.translation + rotationMatrix(o->transform) * local;
}

bool strictlyInside(const Vec3& p, const Vec3& lo, const Vec3& hi) {
    constexpr double e = 1e-6;
    return p.x > lo.x + e && p.y > lo.y + e && p.z > lo.z + e && p.x < hi.x - e && p.y < hi.y - e && p.z < hi.z - e;
}

/// Edges of `id` whose midpoint lies strictly inside the box [lo, hi].
std::vector<mesh::EdgeId> edgesInside(const Document& doc, ObjectId id, const Vec3& lo, const Vec3& hi) {
    const auto& m = meshOf(doc, id);
    std::vector<mesh::EdgeId> out;
    for (const auto& [eid, e] : m.edges()) {
        const Vec3 mid = (m.vertex(e.v0)->position + m.vertex(e.v1)->position) * 0.5;
        if (strictlyInside(toWorld(doc, id, mid), lo, hi)) out.push_back(mesh::EdgeId{eid});
    }
    return out;
}

bool hasEdge(const Document& doc, ObjectId id, const Vec3& a, const Vec3& b) {
    const auto& m = meshOf(doc, id);
    const auto at = [](const Vec3& p, const Vec3& q) { return glm::length(p - q) < 1e-6; };
    return std::ranges::any_of(m.edges(), [&](const auto& entry) {
        const auto& e = entry.second;
        const Vec3 p = toWorld(doc, id, m.vertex(e.v0)->position);
        const Vec3 q = toWorld(doc, id, m.vertex(e.v1)->position);
        return (at(p, a) && at(q, b)) || (at(p, b) && at(q, a));
    });
}

} // namespace

TEST_CASE("faceIntersection: a line through a face with a hole gives a segment on each side") {
    mesh::PlanarFace floor;
    floor.loops = {{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}, {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}};
    floor.normal = {0, 0, 1};
    mesh::PlanarFace wall;
    wall.loops = {{{-1, 2, -1}, {5, 2, -1}, {5, 2, 1}, {-1, 2, 1}}};
    wall.normal = {0, -1, 0};
    auto segs = mesh::faceIntersection(floor, wall);
    REQUIRE(segs.size() == 2);
    std::ranges::sort(segs, [](const auto& a, const auto& b) { return std::min(a.first.x, a.second.x) < std::min(b.first.x, b.second.x); });
    CHECK(test::near(std::min(segs[0].first.x, segs[0].second.x), 0.0, 1e-9));
    CHECK(test::near(std::max(segs[0].first.x, segs[0].second.x), 1.0, 1e-9));
    CHECK(test::near(std::min(segs[1].first.x, segs[1].second.x), 3.0, 1e-9));
    CHECK(test::near(std::max(segs[1].first.x, segs[1].second.x), 4.0, 1e-9));
    for (const auto& s : segs) {
        CHECK(test::near(s.first.y, 2.0, 1e-9));
        CHECK(test::near(s.first.z, 0.0, 1e-9));
    }
}

TEST_CASE("faceIntersection: parallel, apart and edge-sharing faces give nothing") {
    mesh::PlanarFace a;
    a.loops = {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
    a.normal = {0, 0, 1};
    mesh::PlanarFace above = a;
    for (auto& p : above.loops[0]) p.z = 1.0;
    CHECK(mesh::faceIntersection(a, above).empty());
    CHECK(mesh::faceIntersection(a, a).empty()); // coplanar

    mesh::PlanarFace farWall;
    farWall.loops = {{{5, 0.5, -1}, {6, 0.5, -1}, {6, 0.5, 1}, {5, 0.5, 1}}};
    farWall.normal = {0, -1, 0};
    CHECK(mesh::faceIntersection(a, farWall).empty());

    // A wall standing on a's edge y = 0: they share that edge, nothing new.
    mesh::PlanarFace side;
    side.loops = {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}};
    side.normal = {0, -1, 0};
    CHECK(mesh::faceIntersection(a, side).empty());
}

TEST_CASE("Accept: intersecting two overlapping boxes adds the shared edges and erasing gives the union outline") {
    Editor editor;
    ToolController tools(editor);
    Document doc;
    const ObjectId a = box(doc, {0, 0, 0});
    const ObjectId b = box(doc, {1, 1, 1});
    editor.resetDocument(doc);
    editor.select(Selection{a, std::nullopt});

    REQUIRE_FALSE(tools.intersectSelection(IntersectMode::Model).has_value());
    CHECK(editor.undoLabel() == "Intersect with model");
    const Document& cut = editor.document();
    REQUIRE_FALSE(validateDocument(cut).has_value());

    // Each box: three faces crossed by the other box are split in two.
    CHECK(meshOf(cut, a).faces().size() == 9);
    CHECK(meshOf(cut, b).faces().size() == 9);
    CHECK(test::near(area(meshOf(cut, a)), 24.0, 1e-9));
    CHECK(test::near(area(meshOf(cut, b)), 24.0, 1e-9));
    // The shared edges: where B's walls cross A's top, and A's walls cross B's bottom.
    CHECK(hasEdge(cut, a, {1, 1, 2}, {1, 2, 2}));
    CHECK(hasEdge(cut, a, {1, 1, 2}, {2, 1, 2}));
    CHECK(hasEdge(cut, a, {2, 1, 1}, {2, 2, 1}));
    CHECK(hasEdge(cut, b, {2, 2, 1}, {1, 2, 1}));
    CHECK(hasEdge(cut, b, {1, 1, 2}, {1, 1, 1}));
    // Both objects stay selected, whole.
    CHECK(editor.isSelected(a));
    CHECK(editor.isSelected(b));

    // Erase what each box has inside the other: the union's outline is left.
    const auto insideB = edgesInside(cut, a, {1, 1, 1}, {3, 3, 3});
    const auto insideA = edgesInside(cut, b, {0, 0, 0}, {2, 2, 2});
    CHECK(insideB.size() == 3);
    CHECK(insideA.size() == 3);
    REQUIRE(editor.commit("Erase", [&](const Document& d) { return eraseEdges(d, a, insideB); }));
    REQUIRE(editor.commit("Erase", [&](const Document& d) { return eraseEdges(d, b, insideA); }));
    const Document& merged = editor.document();
    REQUIRE_FALSE(validateDocument(merged).has_value());
    CHECK(meshOf(merged, a).faces().size() == 6);
    CHECK(meshOf(merged, b).faces().size() == 6);
    // Two 2 m cubes overlapping in a 1 m cube: 24 + 24 - 2 x 3 m2 hidden inside.
    CHECK(test::near(area(meshOf(merged, a)) + area(meshOf(merged, b)), 42.0, 1e-9));
    CHECK(edgesInside(merged, a, {1, 1, 1}, {3, 3, 3}).empty());
    CHECK(edgesInside(merged, b, {0, 0, 0}, {2, 2, 2}).empty());
    // The outline still runs along the seam on A's top.
    CHECK(hasEdge(merged, a, {1, 1, 2}, {1, 2, 2}));

    // One undo step takes the intersection back.
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    CHECK(sameContent(editor.document(), doc));
}

TEST_CASE("With Selection cuts only the selected objects against each other") {
    Document doc;
    const ObjectId a = box(doc, {0, 0, 0});
    const ObjectId b = box(doc, {1, 1, 1});
    const ObjectId c = box(doc, {-1, -1, 1}); // crosses A too, not selected
    std::vector<ElementRef> faces = allFaces(doc, a);
    for (const auto& r : allFaces(doc, b)) faces.push_back(r);
    const auto r = intersectFaces(doc, faces, IntersectMode::Selection);
    REQUIRE(r);
    CHECK(r->objectIds.size() == 2);
    CHECK(meshOf(r->document, a).faces().size() == 9);
    CHECK(meshOf(r->document, b).faces().size() == 9);
    CHECK(r->document.find(c) == doc.find(c)); // untouched (same shared object)

    // With Model from A alone, C is cut too.
    const auto m = intersectFaces(doc, allFaces(doc, a), IntersectMode::Model);
    REQUIRE(m);
    CHECK(m->objectIds.size() == 3);
    CHECK(meshOf(m->document, c).faces().size() == 9);
    CHECK(meshOf(m->document, a).faces().size() == 13); // the top is cut by both into four
    CHECK_FALSE(validateDocument(m->document).has_value());
}

TEST_CASE("A selected face is cut on its own; the other object is cut where it crosses that face") {
    Document doc;
    const ObjectId a = box(doc, {0, 0, 0});
    const ObjectId b = box(doc, {1, 1, 1});
    // A's top: the face whose plane is z = 2.
    const auto meshA = mesh::objectMesh(*doc.find(a));
    const auto& ma = *meshA;
    std::optional<ElementRef> top;
    for (const auto& [fid, f] : ma.faces()) {
        if (f.normal.z > 0.5) top = ElementRef{a, ElementKind::Face, fid};
    }
    REQUIRE(top);
    const auto r = intersectFaces(doc, {*top}, IntersectMode::Model);
    REQUIRE(r);
    CHECK(meshOf(r->document, a).faces().size() == 7); // only the top split
    // B's two walls get the line where the top crosses them, but it stops at
    // A's edge, short of their far side, so they do not split.
    CHECK(meshOf(r->document, b).faces().size() == 6);
    CHECK(hasEdge(r->document, b, {1, 1, 2}, {1, 2, 2}));
    CHECK(hasEdge(r->document, b, {1, 1, 2}, {2, 1, 2}));
    CHECK_FALSE(validateDocument(r->document).has_value());
}

TEST_CASE("With Context cuts faces against the rest of their own object only") {
    Document doc;
    // One group: a 2 m square on the ground and a wall through it (y = 1), drawn
    // into the same mesh. The wall's sides pass through the square without
    // touching its edges, so nothing was cut while drawing.
    auto drawn = drawPolyline(doc, std::nullopt, {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, true);
    REQUIRE(drawn);
    const ObjectId g = *drawn->objectId;
    drawn = drawPolyline(drawn->document, g, {{0.5, 1, -1}, {1.5, 1, -1}, {1.5, 1, 1}, {0.5, 1, 1}}, true);
    REQUIRE(drawn);
    doc = drawn->document;
    REQUIRE(meshOf(doc, g).faces().size() == 2);
    const ObjectId other = box(doc, {1, 1, -0.5}, 2.0, 1.0); // a separate box crossing both

    const auto r = intersectFaces(doc, allFaces(doc, g), IntersectMode::Context);
    REQUIRE(r);
    CHECK(r->objectIds == std::vector<ObjectId>{g});
    CHECK(r->document.find(other) == doc.find(other));
    // Selecting the crossing box too still cuts each object only against itself.
    std::vector<ElementRef> both = allFaces(doc, g);
    for (const auto& f : allFaces(doc, other)) both.push_back(f);
    const auto r2 = intersectFaces(doc, both, IntersectMode::Context);
    REQUIRE(r2);
    CHECK(r2->objectIds == std::vector<ObjectId>{g});
    CHECK(r2->document.find(other) == doc.find(other));
    CHECK_FALSE(intersectFaces(doc, allFaces(doc, other), IntersectMode::Context));

    const auto& m = meshOf(r->document, g);
    // The wall splits at the ground; the square gets the crossing line (loose
    // inside it: its ends do not reach the square's edges).
    CHECK(m.faces().size() == 3);
    CHECK(hasEdge(r->document, g, {0.5, 1, 0}, {1.5, 1, 0}));
    CHECK_FALSE(validateDocument(r->document).has_value());
}

TEST_CASE("Intersecting faces that cross nothing fails without an undo entry") {
    Editor editor;
    ToolController tools(editor);
    Document doc;
    const ObjectId a = box(doc, {0, 0, 0});
    box(doc, {5, 0, 0});
    editor.resetDocument(doc);
    editor.select(Selection{a, std::nullopt});
    CHECK(tools.intersectSelection(IntersectMode::Model).has_value());
    CHECK(editor.undoDepth() == 0);
    CHECK(sameContent(editor.document(), doc));

    // Touching boxes (one on top of the other, same footprint) share only edges.
    Document stacked;
    const ObjectId lower = box(stacked, {0, 0, 0});
    box(stacked, {0, 0, 2});
    CHECK_FALSE(intersectFaces(stacked, allFaces(stacked, lower), IntersectMode::Model));

    editor.select(std::nullopt);
    CHECK(tools.intersectSelection(IntersectMode::Model).has_value());
}

TEST_CASE("A box standing on a bigger box imprints its footprint on the top") {
    Document doc;
    const ObjectId base = box(doc, {0, 0, 0}, 4.0, 1.0);
    const ObjectId top = box(doc, {1, 1, 1}, 1.0, 1.0);
    const auto r = intersectFaces(doc, allFaces(doc, top), IntersectMode::Model);
    REQUIRE(r);
    CHECK(r->objectIds == std::vector<ObjectId>{base});
    // The base's top gets the square footprint: a hole plus the inner face.
    CHECK(meshOf(r->document, base).faces().size() == 7);
    CHECK(hasEdge(r->document, base, {1, 1, 1}, {2, 1, 1}));
    CHECK(test::near(area(meshOf(r->document, base)), 2 * 16.0 + 4 * 4.0, 1e-9));
    CHECK_FALSE(validateDocument(r->document).has_value());
}

TEST_CASE("Intersect works on rotated objects") {
    Document doc;
    const ObjectId a = box(doc, {0, 0, 0});
    const ObjectId b = box(doc, {1, 1, 0.5});
    auto turned = rotateObject(doc, b, {2, 2, 0}, {0, 0, 1}, std::numbers::pi / 6.0);
    REQUIRE(turned);
    doc = turned->document;
    const auto r = intersectFaces(doc, allFaces(doc, a), IntersectMode::Model);
    REQUIRE(r);
    CHECK(r->objectIds.size() == 2);
    CHECK_FALSE(validateDocument(r->document).has_value());
    CHECK(meshOf(r->document, a).faces().size() > 6);
    CHECK(meshOf(r->document, b).faces().size() > 6);
    CHECK(test::near(area(meshOf(r->document, a)), 24.0, 1e-9));
    CHECK(test::near(area(meshOf(r->document, b)), 24.0, 1e-9));
}

TEST_CASE("faceIntersection: nearly parallel faces still meet where they cross") {
    // Planes 1e-8 rad apart, crossing along the y axis: 1 km out they are 10 um apart.
    const double angle = 1e-8;
    mesh::PlanarFace flat;
    flat.loops = {{{-1000, -1, 0}, {1000, -1, 0}, {1000, 1, 0}, {-1000, 1, 0}}};
    flat.normal = {0, 0, 1};
    mesh::PlanarFace tilted;
    const double z = 1000.0 * std::tan(angle);
    tilted.loops = {{{-1000, -1, z}, {1000, -1, -z}, {1000, 1, -z}, {-1000, 1, z}}};
    tilted.normal = glm::normalize(Vec3{std::sin(angle), 0.0, std::cos(angle)});
    const auto segs = mesh::faceIntersection(flat, tilted);
    REQUIRE_FALSE(segs.empty());
    for (const auto& s : segs) {
        CHECK(std::isfinite(s.first.x));
        CHECK(std::abs(s.first.x) < 1e-3);
        CHECK(std::abs(s.second.x) < 1e-3);
    }
}
