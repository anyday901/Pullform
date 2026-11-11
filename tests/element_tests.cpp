// Sub-object elements: topology of today's objects, vertex/edge/face picking
// with pixel tolerances and occlusion, profile edges, and click expansion.
#include "fake_renderer.hpp"

#include "app/tools/click_counter.hpp"
#include "core/geometry/element_pick.hpp"
#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <doctest/doctest.h>

#include <algorithm>

using namespace pf;
using pf::test::boxObject;

namespace {

constexpr double kPixel = 1e-3; // radians per pixel used by these tests

std::optional<ElementHit> pickOne(const ElementTopology& topo, const Ray& ray, ObjectId id = ObjectId{1}) {
    const PickTarget target{id, &topo};
    return pickElement(ray, std::span<const PickTarget>(&target, 1), ElementPickOptions{kPixel});
}

/// A ray from `eye` through `at`.
Ray through(const Vec3& eye, const Vec3& at) { return {eye, glm::normalize(at - eye)}; }

std::optional<std::uint32_t> edgeBetween(const ElementTopology& topo, const Vec3& a, const Vec3& b) {
    for (std::uint32_t e = 0; e < topo.edges.size(); ++e) {
        const Vec3 p = topo.vertices[topo.edges[e].a].position;
        const Vec3 q = topo.vertices[topo.edges[e].b].position;
        if ((glm::length(p - a) < 1e-9 && glm::length(q - b) < 1e-9) || (glm::length(p - b) < 1e-9 && glm::length(q - a) < 1e-9)) {
            return topo.edges[e].id;
        }
    }
    return std::nullopt;
}

std::size_t countKind(const std::vector<ElementRef>& refs, ElementKind kind) {
    return static_cast<std::size_t>(std::count_if(refs.begin(), refs.end(), [&](const ElementRef& r) { return r.kind == kind; }));
}

} // namespace

TEST_CASE("a box has 8 shared vertices, 12 shared edges and 6 faces") {
    const auto topo = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    CHECK(topo.vertices.size() == 8);
    CHECK(topo.edges.size() == 12);
    CHECK(topo.faces.size() == 6);
    CHECK(topo.triangles.size() == 12);
    for (const auto& e : topo.edges) CHECK(e.faces.size() == 2);
    for (const auto& v : topo.vertices) CHECK(v.edges.size() == 3);
    REQUIRE(topo.faceFor(FaceId::cap()));
    CHECK(topo.faces[*topo.faceFor(FaceId::cap())].normal.z == doctest::Approx(1.0));
    // Ids are the mesh's: corners 1..8, edges 1..12, faces 1..6 (base 1, cap 2, sides 3..6).
    CHECK(topo.faces[*topo.faceFor(FaceId::cap())].id == 2);
    for (const auto& f : topo.faces) CHECK_FALSE(f.twoSided);
}

TEST_CASE("a planar profile is one two-sided face with four border edges") {
    auto sheet = std::make_shared<ProfileObject>(*boxObject(1, 0, 0, 2, 1));
    sheet->extrusion.reset();
    const auto topo = buildTopology(*sheet);
    CHECK(topo.vertices.size() == 4);
    CHECK(topo.edges.size() == 4);
    REQUIRE(topo.faces.size() == 1);
    CHECK(topo.faces[0].twoSided);
    // Every border edge is a profile edge from any viewpoint.
    CHECK(profileEdges(topo, {1, 0.5, 5}).size() == 4);
    // Picking works from below too, since a sheet is two-sided.
    const auto hit = pickOne(topo, {{1.0, 0.5, -5.0}, {0, 0, 1}});
    REQUIRE(hit);
    CHECK(hit->element.kind == ElementKind::Face);
}

TEST_CASE("pick prefers a vertex, then an edge, then the face") {
    const auto topo = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    const Vec3 eye{0.5, 0.5, 11.0}; // 10 m above the cap: one pixel is ~1 cm there

    SUBCASE("on the face, away from edges") {
        const auto hit = pickOne(topo, through(eye, {0.5, 0.5, 1.0}));
        REQUIRE(hit);
        CHECK(hit->element.kind == ElementKind::Face);
        CHECK(topo.faces[*topo.indexOf(ElementKind::Face, hit->element.id)].logical == std::optional(FaceId::cap()));
        CHECK(hit->point.z == doctest::Approx(1.0));
    }
    SUBCASE("three pixels from an edge") {
        const auto hit = pickOne(topo, through(eye, {0.5, 0.97, 1.0}));
        REQUIRE(hit);
        CHECK(hit->element.kind == ElementKind::Edge);
        CHECK(hit->element.id == *edgeBetween(topo, {0, 1, 1}, {1, 1, 1}));
        CHECK(hit->point.y == doctest::Approx(1.0));
    }
    SUBCASE("four pixels from a corner, which beats both of its edges") {
        const auto hit = pickOne(topo, through(eye, {0.97, 0.97, 1.0}));
        REQUIRE(hit);
        CHECK(hit->element.kind == ElementKind::Vertex);
        CHECK(glm::length(topo.vertices[*topo.indexOf(ElementKind::Vertex, hit->element.id)].position - Vec3{1, 1, 1}) < 1e-12);
    }
    SUBCASE("nothing under the pointer") { CHECK_FALSE(pickOne(topo, through(eye, {3.0, 3.0, 1.0}))); }
}

TEST_CASE("hidden edges and vertices are not picked; the face in front is") {
    const auto topo = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    // A ray aimed a hair off the bottom corner (1, 1, 0) passes through the cap
    // first: the corner and its bottom edges are hidden, so the cap is picked.
    const Vec3 eye{0.6, 0.6, 11.0};
    const Ray ray = through(eye, {0.95, 0.95, 0.0}); // in front: the cap
    const auto hit = pickOne(topo, ray);
    REQUIRE(hit);
    CHECK(hit->element.kind == ElementKind::Face);
    CHECK(topo.faces[*topo.indexOf(ElementKind::Face, hit->element.id)].logical == std::optional(FaceId::cap()));
}

TEST_CASE("another object in front hides an edge") {
    // A slab floating above the box covers the box's top edges.
    const auto back = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    auto slab = std::make_shared<ProfileObject>(*boxObject(2, -1, -1, 3, 3, 0.2));
    slab->transform.translation.z = 2.0;
    const auto cover = buildTopology(*slab);
    const PickTarget targets[] = {{ObjectId{1}, &back}, {ObjectId{2}, &cover}};
    const Vec3 eye{0.5, 0.5, 11.0};
    const auto hit = pickElement(through(eye, {0.5, 0.995, 1.0}), targets, ElementPickOptions{kPixel});
    REQUIRE(hit);
    CHECK(hit->element.object == ObjectId{2});
    CHECK(hit->element.kind == ElementKind::Face);
}

TEST_CASE("profile edges of a box are its silhouette") {
    const auto topo = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    // From straight above, the outline is the cap's four edges (cap faces the
    // eye, the sides face sideways: exactly perpendicular counts as away).
    CHECK(profileEdges(topo, {0.5, 0.5, 10}).size() == 4);
    // From a corner direction, three faces are visible: the silhouette is a hexagon.
    CHECK(profileEdges(topo, {5, 6, 7}).size() == 6);
}

TEST_CASE("double-click expands a face to its edges and an edge to its faces") {
    const auto topo = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    const ObjectId id{1};
    const auto cap = topo.faces[*topo.faceFor(FaceId::cap())].id;
    const auto face = expandBounded(id, topo, ElementKind::Face, cap);
    CHECK(countKind(face, ElementKind::Face) == 1);
    CHECK(countKind(face, ElementKind::Edge) == 4);
    CHECK(face.front() == ElementRef{id, ElementKind::Face, cap});

    const auto edge = expandBounded(id, topo, ElementKind::Edge, *edgeBetween(topo, {0, 1, 1}, {1, 1, 1}));
    CHECK(countKind(edge, ElementKind::Edge) == 1);
    CHECK(countKind(edge, ElementKind::Face) == 2);
}

TEST_CASE("triple-click selects everything connected") {
    const auto topo = buildTopology(*boxObject(1, 0, 0, 1, 1, 1));
    const auto all = expandConnected(ObjectId{1}, topo, ElementKind::Edge, topo.edges[0].id);
    CHECK(countKind(all, ElementKind::Edge) == 12);
    CHECK(countKind(all, ElementKind::Face) == 6);
    CHECK(countKind(all, ElementKind::Vertex) == 0);
}

TEST_CASE("clicks at one spot in quick succession count up to three") {
    ClickCounter clicks;
    CHECK(clicks.click(0.00, {10, 10}) == 1);
    CHECK(clicks.click(0.20, {11, 10}) == 2);
    CHECK(clicks.click(0.40, {11, 11}) == 3);
    CHECK(clicks.click(0.50, {11, 11}) == 1); // a fourth starts a new series
    CHECK(clicks.click(1.50, {11, 11}) == 1); // too slow
    CHECK(clicks.click(1.60, {30, 11}) == 1); // moved too far
    CHECK(clicks.click(1.70, {30, 11}) == 2);
}

// ---- Select tool: the acceptance sequence -----------------------------------

#include "app/tools/tool_controller.hpp"
#include "helpers.hpp"

namespace {

/// A pointer looking from `eye` through `at`, with the element pick resolved
/// against the document as the viewport does it.
PointerInput lookAt(const Editor& editor, const Vec3& eye, const Vec3& at, int clicks = 1) {
    PointerInput in;
    in.ray = through(eye, at);
    in.pick = pickDocument(in.ray, editor.document());
    in.pixelAngle = kPixel;
    std::vector<ElementTopology> topos;
    for (const auto& o : editor.document().objects) topos.push_back(buildTopology(*o));
    std::vector<PickTarget> targets;
    for (std::size_t i = 0; i < topos.size(); ++i) targets.push_back({editor.document().objects[i]->id, &topos[i]});
    in.element = pickElement(in.ray, targets, ElementPickOptions{kPixel});
    in.clicks = clicks;
    return in;
}

} // namespace

TEST_CASE("Select: click an edge, then a face, then triple-click") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000,1000", "1000");
    tools.setTool(Tool::Select);
    const Vec3 eye{0.5, 0.5, 11.0};
    const auto topo = buildTopology(*editor.document().find(box));
    const auto capIndex = *topo.faceFor(FaceId::cap());

    // Hovering an edge highlights that edge.
    tools.pointerMove(lookAt(editor, eye, {0.5, 0.97, 1.0}));
    REQUIRE(editor.hoverElement());
    CHECK(editor.hoverElement()->kind == ElementKind::Edge);

    // Click the edge: just that edge is selected; its object counts for whole-object tools.
    auto in = lookAt(editor, eye, {0.5, 0.97, 1.0});
    tools.pointerDown(in);
    tools.pointerUp(in);
    REQUIRE(editor.selectionSet().elements().size() == 1);
    const ElementRef edge = editor.selectionSet().elements().front();
    CHECK(edge.kind == ElementKind::Edge);
    CHECK(edge.object == box);
    CHECK(editor.selectedObjects() == std::vector<ObjectId>{box});
    CHECK(editor.selectionSet().hasElements(box));

    // Click the face: the face replaces the edge, and stays the primary's face (push/pull).
    in = lookAt(editor, eye, {0.5, 0.5, 1.0});
    tools.pointerDown(in);
    tools.pointerUp(in);
    REQUIRE(editor.selectionSet().elements().size() == 1);
    CHECK(editor.selectionSet().elements().front() == ElementRef{box, ElementKind::Face, topo.faces[capIndex].id});
    REQUIRE(editor.selection());
    CHECK(editor.selection()->face == FaceId::cap());

    // Double-click the face: the face and its four edges.
    in = lookAt(editor, eye, {0.5, 0.5, 1.0}, 2);
    tools.pointerDown(in);
    tools.pointerUp(in);
    CHECK(countKind(editor.selectionSet().elements(), ElementKind::Face) == 1);
    CHECK(countKind(editor.selectionSet().elements(), ElementKind::Edge) == 4);

    // Triple-click: everything connected, all 12 edges and 6 faces.
    in = lookAt(editor, eye, {0.5, 0.5, 1.0}, 3);
    tools.pointerDown(in);
    tools.pointerUp(in);
    CHECK(countKind(editor.selectionSet().elements(), ElementKind::Face) == 6);
    CHECK(countKind(editor.selectionSet().elements(), ElementKind::Edge) == 12);

    // Clicking empty space clears it.
    in = lookAt(editor, eye, {5.0, 5.0, 0.0});
    tools.pointerDown(in);
    tools.pointerUp(in);
    CHECK(editor.selectionSet().empty());
    CHECK(editor.selectionSet().elements().empty());
}

TEST_CASE("element selection survives undo only while the elements still exist") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000,1000", "1000");
    SelectionSet set;
    set.addElements({{box, ElementKind::Edge, 1}, {box, ElementKind::Face, 99}}, std::nullopt);
    const auto clean = sanitizeSelection(editor.document(), set);
    CHECK(clean.elements() == std::vector<ElementRef>{{box, ElementKind::Edge, 1}});
    CHECK(clean.contains(box));

    SelectionSet gone;
    gone.addElements({{box, ElementKind::Face, 99}}, std::nullopt);
    CHECK(sanitizeSelection(editor.document(), gone).empty());
}

TEST_CASE("selecting an object whole drops its element entries; removing the last element drops the object") {
    const ObjectId a{1};
    SelectionSet set;
    set.addElements({{a, ElementKind::Edge, 3}}, std::nullopt);
    CHECK(set.hasElements(a));
    set.add(Selection{a, std::nullopt});
    CHECK_FALSE(set.hasElements(a));
    CHECK(set.contains(a));

    SelectionSet other;
    other.addElements({{a, ElementKind::Edge, 3}, {a, ElementKind::Face, 1}}, std::nullopt);
    other.toggleElements({{a, ElementKind::Edge, 3}}, std::nullopt);
    CHECK(other.contains(a));
    other.toggleElements({{a, ElementKind::Face, 1}}, std::nullopt);
    CHECK_FALSE(other.contains(a));
}

TEST_CASE("mesh group: a square split by a line has two pickable faces and a shared edge") {
    mesh::Mesh m;
    m.addEdge({0, 0, 0}, {2, 0, 0});
    m.addEdge({2, 0, 0}, {2, 2, 0});
    m.addEdge({2, 2, 0}, {0, 2, 0});
    m.addEdge({0, 2, 0}, {0, 0, 0});
    REQUIRE(m.faces().size() == 1);
    m.addEdge({1, 0, 0}, {1, 2, 0}); // splits the square in two
    REQUIRE(m.faces().size() == 2);

    ProfileObject group;
    group.id = ObjectId{7};
    group.mesh = std::make_shared<const mesh::Mesh>(m);
    group.transform.translation = {10, 0, 0}; // topology is in world space
    const auto topo = buildTopology(group);
    CHECK(topo.faces.size() == 2);
    CHECK(topo.edges.size() == m.edges().size());
    for (const auto& f : topo.faces) CHECK(f.twoSided); // a loose sheet: both sides
    // A mesh group's faces are FaceKind::Mesh faces (the ids buildMesh and picking use).
    CHECK(topo.faces[0].logical == FaceId::meshFace(topo.faces[0].id));

    const Vec3 eye{11.0, 1.0, 11.0};
    // The splitting edge, at x = 1 (world 11), borders both faces.
    const auto onEdge = pickOne(topo, through(eye, {11.0, 1.4, 0.0}), group.id);
    REQUIRE(onEdge);
    REQUIRE(onEdge->element.kind == ElementKind::Edge);
    const auto edgeIndex = *topo.indexOf(ElementKind::Edge, onEdge->element.id);
    CHECK(topo.edges[edgeIndex].faces.size() == 2);
    CHECK(countKind(expandBounded(group.id, topo, ElementKind::Edge, onEdge->element.id), ElementKind::Face) == 2);

    // Each half is its own face; the left one's double-click has 4 edges.
    const auto left = pickOne(topo, through(eye, {10.5, 1.0, 0.0}), group.id);
    const auto right = pickOne(topo, through(eye, {11.5, 1.0, 0.0}), group.id);
    REQUIRE(left);
    REQUIRE(right);
    CHECK(left->element.kind == ElementKind::Face);
    CHECK(right->element.kind == ElementKind::Face);
    CHECK(left->element.id != right->element.id);
    CHECK(countKind(expandBounded(group.id, topo, ElementKind::Face, left->element.id), ElementKind::Edge) == 4);

    // Triple-click: both faces and all edges.
    const auto all = expandConnected(group.id, topo, ElementKind::Face, left->element.id);
    CHECK(countKind(all, ElementKind::Face) == 2);
    CHECK(countKind(all, ElementKind::Edge) == m.edges().size());
}

TEST_CASE("toggling an element of an object selected whole removes the object") {
    const ObjectId a{1}, b{2};
    SelectionSet set;
    set.add(Selection{a, std::nullopt}); // as a box drag or Select All leaves it
    set.add(Selection{b, std::nullopt});
    set.toggleElements({{a, ElementKind::Face, 1}}, std::nullopt); // Shift-click a face of a
    CHECK_FALSE(set.contains(a));
    CHECK(set.contains(b));
}

TEST_CASE("scaling a selection keeps a mesh group in place relative to the others") {
    mesh::Mesh m;
    m.addEdge({0, 0, 0}, {2, 0, 0});
    m.addEdge({2, 0, 0}, {2, 2, 0});
    m.addEdge({2, 2, 0}, {0, 2, 0});
    m.addEdge({0, 2, 0}, {0, 0, 0});
    ProfileObject group;
    group.id = ObjectId{1};
    group.name = "Group 1";
    group.mesh = std::make_shared<const mesh::Mesh>(m);
    Document doc;
    doc.objects.push_back(std::make_shared<const ProfileObject>(group));
    doc.nextId = 2;
    // Scale by 2 about (3, 1, 0): local x 0..2 (centre 1) becomes -3..1.
    const auto r = scaleObjects(doc, {group.id}, Vec3{2.0}, Vec3{3, 1, 0});
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error()));
    const Bounds bounds = objectBounds(*r->document.find(group.id));
    CHECK(bounds.min.x == doctest::Approx(-3.0));
    CHECK(bounds.max.x == doctest::Approx(1.0));
    CHECK(bounds.min.y == doctest::Approx(-1.0));
    CHECK(bounds.max.y == doctest::Approx(3.0));
}
