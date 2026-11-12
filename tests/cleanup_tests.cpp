// Edge and face cleanup (roadmap item 21): Soften/Smooth Edges with an angle,
// Reverse and Orient Faces, Hide/Unhide, Flip Along, and file format version 4.
#include "fake_renderer.hpp"
#include "helpers.hpp"

#include "app/viewport/mesh_cache.hpp"
#include "core/commands/cleanup.hpp"
#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/geometry/circle.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/snap.hpp"
#include "core/geometry/solid.hpp"
#include "core/geometry/topology.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>

using namespace pf;

namespace {

constexpr double kDeg = std::numbers::pi / 180.0;

/// A closed prism with `sides` hard side edges (a Polygon pushed up), radius 1 m, 2 m tall.
std::pair<Document, ObjectId> prism(int sides, Vec3 at = {0, 0, 0}) {
    auto made = createCircle(Document{}, *circleFromRadius(at, 1.0, 0.0, sides), ProfileKind::Polygon);
    REQUIRE(made);
    auto solid = extrudeProfile(made->document, *made->objectId, 2.0);
    REQUIRE(solid);
    return {solid->document, *made->objectId};
}

const mesh::Mesh& meshOf(const Document& doc, ObjectId id) {
    const auto* o = doc.find(id);
    REQUIRE(o);
    REQUIRE(o->isMeshGroup());
    return *o->mesh;
}

ElementRef face(ObjectId id, std::uint32_t f) { return {id, ElementKind::Face, f}; }
ElementRef edge(ObjectId id, std::uint32_t e) { return {id, ElementKind::Edge, e}; }

/// Every edge between two faces is walked in opposite directions (faces agree
/// about their front), and the enclosed volume is positive (they face out).
bool outwardShell(const mesh::Mesh& m) {
    for (const auto& [id, e] : m.edges()) {
        if (edgeFoldAngle(m, mesh::EdgeId{id}) < 0.0) return false;
        const auto faces = m.facesOfEdge(mesh::EdgeId{id});
        int forward = 0;
        for (const auto f : faces) {
            for (const auto& use : m.face(f)->loops[0]) forward += use.edge.value == id && !use.reversed ? 1 : 0;
        }
        if (forward != 1) return false;
    }
    double volume = 0.0;
    for (const auto& [id, f] : m.faces()) {
        for (const auto& t : mesh::triangulateFace(m, mesh::FaceId{id})) {
            const Vec3 a = m.vertex(t[0])->position;
            const Vec3 b = m.vertex(t[1])->position;
            const Vec3 c = m.vertex(t[2])->position;
            volume += glm::dot(a, glm::cross(b, c)) / 6.0;
        }
    }
    return volume > 0.0;
}

std::size_t drawnEdges(const ElementTopology& topo) {
    std::size_t n = 0;
    for (const auto& e : topo.edges) n += e.soft ? 0 : 1;
    return n;
}

} // namespace

TEST_CASE("Accept: a 48-sided cylinder can be shown smooth with no visible segment edges") {
    auto [doc, id] = prism(48);
    // Drawn as a polygon, every side edge is a hard line.
    CHECK(drawnEdges(buildTopology(*doc.find(id))) == 3u * 48u);
    CHECK(edgeFoldAngle(*mesh::objectMesh(*doc.find(id)), mesh::EdgeId{2 * 48 + 1}) == doctest::Approx(7.5 * kDeg));

    // Soften/Smooth Edges on the whole object at the default 20 degrees.
    const auto smoothed = softenEdges(doc, CleanupScope{{}, {id}}, SoftenSettings{});
    REQUIRE(smoothed);
    const ProfileObject& cylinder = *smoothed->document.find(id);
    const auto& m = meshOf(smoothed->document, id);
    int softSides = 0;
    for (const auto& [eid, e] : m.edges()) {
        const bool side = eid > 2 * 48; // vertical edges are 97..144
        CHECK(e.soft == side);
        CHECK(e.smooth == side);
        softSides += side && e.soft ? 1 : 0;
    }
    CHECK(softSides == 48);

    // What the viewport draws: only the two 48-segment circles, no vertical segment edges.
    test::CountingRenderer renderer;
    MeshCache cache(renderer);
    cache.sync(smoothed->document);
    const auto& topo = cache.find(id)->topology;
    CHECK(drawnEdges(topo) == 2u * 48u);
    for (const auto& e : topo.edges) {
        if (e.soft) continue;
        CHECK(topo.vertices[e.a].position.z == doctest::Approx(topo.vertices[e.b].position.z)); // horizontal: on a circle
    }
    // ...and shades the sides smoothly: every side normal is radial at its vertex.
    const MeshData data = buildMesh(cylinder);
    int radial = 0;
    for (std::size_t i = 0; i < data.positions.size(); ++i) {
        const Vec3& n = data.normals[i];
        if (std::abs(n.z) > 1e-9) continue; // base and cap
        const Vec3& p = data.positions[i];
        CHECK(std::abs(std::abs(glm::dot(n, glm::normalize(Vec3{p.x, p.y, 0.0}))) - 1.0) < 1e-9);
        ++radial;
    }
    CHECK(radial > 0);
    CHECK(outwardShell(m));

    // Moving the slider back below 7.5 degrees makes the sides hard again.
    const auto hard = softenEdges(smoothed->document, CleanupScope{{}, {id}}, SoftenSettings{5.0 * kDeg, true, true});
    REQUIRE(hard);
    CHECK(drawnEdges(buildTopology(*hard->document.find(id))) == 3u * 48u);
    // And again with no change is refused, so it never becomes an undo entry.
    CHECK_FALSE(softenEdges(hard->document, CleanupScope{{}, {id}}, SoftenSettings{5.0 * kDeg, true, true}));
}

TEST_CASE("Soften: the angle, smooth and coplanar settings; selected faces bring their edges") {
    auto [doc, id] = prism(6); // 60 degrees between sides, 90 to the base and cap
    SUBCASE("below the fold angle nothing softens") {
        CHECK_FALSE(softenEdges(doc, CleanupScope{{}, {id}}, SoftenSettings{59.0 * kDeg, true, true}));
    }
    SUBCASE("soft without smoothing") {
        const auto r = softenEdges(doc, CleanupScope{{}, {id}}, SoftenSettings{60.0 * kDeg, false, true});
        REQUIRE(r);
        const auto& m = meshOf(r->document, id);
        for (const auto& [eid, e] : m.edges()) {
            CHECK(e.soft == (eid > 12));
            CHECK_FALSE(e.smooth);
        }
    }
    SUBCASE("a selected side face softens only its own edges") {
        const auto r = softenEdges(doc, CleanupScope{{face(id, 3)}, {}}, SoftenSettings{60.0 * kDeg, true, true});
        REQUIRE(r);
        int soft = 0;
        for (const auto& [eid, e] : meshOf(r->document, id).edges()) soft += e.soft ? 1 : 0;
        CHECK(soft == 2); // its two vertical edges; the base and cap edges are at 90 degrees
    }
    SUBCASE("everything at 180 degrees") {
        const auto r = softenEdges(doc, CleanupScope{{}, {id}}, SoftenSettings{180.0 * kDeg, true, true});
        REQUIRE(r);
        for (const auto& [eid, e] : meshOf(r->document, id).edges()) CHECK(e.soft);
    }

    // Coplanar edges (two squares side by side, not merged) follow "Soften coplanar".
    Editor editor;
    for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{0, 0, 0}, {2, 0, 0}}, {{2, 0, 0}, {2, 1, 0}}, {{2, 1, 0}, {0, 1, 0}},
                                                                 {{0, 1, 0}, {0, 0, 0}}, {{1, 0, 0}, {1, 1, 0}}}) {
        REQUIRE(editor.commit("line", [&](const Document& d) { return drawEdge(d, d.objects.empty() ? std::nullopt : std::optional(d.objects[0]->id), a, b); }));
    }
    const ObjectId flat = editor.document().objects[0]->id;
    REQUIRE(meshOf(editor.document(), flat).faces().size() == 2);
    std::uint32_t inner = 0;
    for (const auto& [eid, e] : meshOf(editor.document(), flat).edges()) {
        if (edgeFoldAngle(meshOf(editor.document(), flat), mesh::EdgeId{eid}) >= 0.0) inner = eid;
    }
    REQUIRE(inner != 0);
    CHECK(edgeFoldAngle(meshOf(editor.document(), flat), mesh::EdgeId{inner}) == doctest::Approx(0.0));
    CHECK_FALSE(softenEdges(editor.document(), CleanupScope{{}, {flat}}, SoftenSettings{20.0 * kDeg, true, false}));
    const auto on = softenEdges(editor.document(), CleanupScope{{}, {flat}}, SoftenSettings{20.0 * kDeg, true, true});
    REQUIRE(on);
    CHECK(meshOf(on->document, flat).edge(mesh::EdgeId{inner})->soft);

    // Loose edges and an empty scope are reported, not silently ignored.
    CHECK_FALSE(softenEdges(doc, CleanupScope{}, SoftenSettings{}));
}

TEST_CASE("Reverse Faces turns faces over; Orient Faces makes their neighbors agree") {
    auto [doc, id] = prism(6);
    REQUIRE(outwardShell(*mesh::objectMesh(*doc.find(id))));

    // Reverse the cap and two sides: the shell no longer agrees.
    const auto reversed = reverseFaces(doc, CleanupScope{{face(id, 2), face(id, 3), face(id, 5)}, {}});
    REQUIRE(reversed);
    const auto& m = meshOf(reversed->document, id);
    CHECK(m.face(mesh::FaceId{2})->normal.z == doctest::Approx(-1.0));
    CHECK_FALSE(mesh::validateMesh(m, "mesh").has_value());
    CHECK_FALSE(outwardShell(m));
    // Reversing twice is the original.
    const auto twice = reverseFaces(reversed->document, CleanupScope{{face(id, 2), face(id, 3), face(id, 5)}, {}});
    REQUIRE(twice);
    CHECK(outwardShell(meshOf(twice->document, id)));

    // Orient from the base (still outward): every face turns to match it.
    const auto oriented = orientFaces(reversed->document, CleanupScope{{face(id, 1)}, {}});
    REQUIRE(oriented);
    CHECK(outwardShell(meshOf(oriented->document, id)));
    CHECK(meshOf(oriented->document, id).face(mesh::FaceId{2})->normal.z == doctest::Approx(1.0));
    // Nothing left to turn, and no face to start from, are both refused.
    CHECK_FALSE(orientFaces(oriented->document, CleanupScope{{face(id, 1)}, {}}));
    CHECK_FALSE(orientFaces(oriented->document, CleanupScope{{}, {id}}));

    // Orienting from a reversed face turns the rest inward to match it.
    const auto inward = orientFaces(reversed->document, CleanupScope{{face(id, 2)}, {}});
    REQUIRE(inward);
    const auto& in = meshOf(inward->document, id);
    CHECK(in.face(mesh::FaceId{1})->normal.z == doctest::Approx(1.0)); // the base now faces up, into the solid
    // A whole object reverses every face: an inward shell comes out again.
    const auto out = reverseFaces(inward->document, CleanupScope{{}, {id}});
    REQUIRE(out);
    CHECK(outwardShell(meshOf(out->document, id)));
}

TEST_CASE("Hide and Unhide: hidden edges and faces stay in the model but are not drawn or picked") {
    auto [doc, id] = prism(4);
    // Hide the cap face and one vertical edge.
    const auto hidden = setHidden(doc, CleanupScope{{face(id, 2), edge(id, 9)}, {}}, true);
    REQUIRE(hidden);
    const auto& m = meshOf(hidden->document, id);
    CHECK(m.face(mesh::FaceId{2})->hidden);
    CHECK(m.edge(mesh::EdgeId{9})->hidden);
    CHECK_FALSE(m.edge(mesh::EdgeId{9})->soft); // hiding is not softening
    CHECK(m.faces().size() == 6);               // still a closed solid
    CHECK(anyHidden(hidden->document));
    CHECK_FALSE(anyHidden(doc));
    const ProfileObject& o = *hidden->document.find(id);

    // Not drawn: no triangles for the cap, the edge skipped.
    CHECK(buildMesh(o).faces.size() == 5);
    CHECK(buildMesh(o, std::nullopt, true).faces.size() == 6);
    const auto topo = buildTopology(o);
    CHECK(drawnEdges(topo) == 11u);
    const auto capIndex = *topo.indexOf(ElementKind::Face, 2);
    CHECK(topo.faces[capIndex].hidden);
    CHECK(std::ranges::none_of(topo.triangles, [capIndex](const auto& t) { return t.face == capIndex; }));
    // Not picked: a ray down through where the cap was does not find it.
    std::vector<PickTarget> targets{{id, &topo}};
    const Ray down{{0.1, 0.1, 10.0}, {0, 0, -1}};
    const auto hit = pickElement(down, targets, ElementPickOptions{1e-3});
    CHECK((!hit || hit->element != face(id, 2)));

    // View > Hidden Geometry: drawn faint and picked, so it can be selected and unhidden.
    const auto shown = buildTopology(o, true);
    CHECK(drawnEdges(shown) == 12u);
    CHECK(shown.edges[*shown.indexOf(ElementKind::Edge, 9)].ghost);
    std::vector<PickTarget> shownTargets{{id, &shown}};
    const auto shownHit = pickElement(down, shownTargets, ElementPickOptions{1e-3});
    REQUIRE(shownHit);
    CHECK(shownHit->element == face(id, 2));
    test::CountingRenderer renderer;
    MeshCache cache(renderer);
    cache.sync(hidden->document);
    CHECK(cache.find(id)->mesh.faces.size() == 5);
    cache.setShowHidden(true);
    cache.sync(hidden->document);
    CHECK(cache.find(id)->mesh.faces.size() == 6);

    // Already hidden is refused; Unhide on the selection or Unhide All brings it back.
    CHECK_FALSE(setHidden(hidden->document, CleanupScope{{face(id, 2)}, {}}, true));
    const auto one = setHidden(hidden->document, CleanupScope{{face(id, 2)}, {}}, false);
    REQUIRE(one);
    CHECK_FALSE(meshOf(one->document, id).face(mesh::FaceId{2})->hidden);
    CHECK(meshOf(one->document, id).edge(mesh::EdgeId{9})->hidden);
    const auto all = unhideAll(hidden->document);
    REQUIRE(all);
    CHECK_FALSE(anyHidden(all->document));
    CHECK_FALSE(unhideAll(all->document));
    // Soft edges stay soft through Unhide All.
    const auto soft = softenEdges(all->document, CleanupScope{{}, {id}}, SoftenSettings{90.0 * kDeg, true, true});
    REQUIRE(soft);
    const auto softHidden = setHidden(soft->document, CleanupScope{{edge(id, 9)}, {}}, true);
    REQUIRE(softHidden);
    const auto back = unhideAll(softHidden->document);
    REQUIRE(back);
    CHECK(meshOf(back->document, id).edge(mesh::EdgeId{9})->soft);
}

TEST_CASE("format v4: hidden flags are saved; a version 3 file opens and saves as version 4") {
    auto [doc, id] = prism(4);
    const auto hidden = setHidden(doc, CleanupScope{{face(id, 2), edge(id, 9)}, {}}, true);
    REQUIRE(hidden);
    const std::string text = serializeProject(hidden->document, std::nullopt);
    CHECK(text.find("\"version\": 4") != std::string::npos);
    const auto file = nlohmann::json::parse(text);
    const auto& faces = file["document"]["objects"][0]["mesh"]["faces"];
    int hiddenFaces = 0;
    for (const auto& f : faces) hiddenFaces += f.contains("hidden") ? 1 : 0;
    CHECK(hiddenFaces == 1); // written only for a hidden face
    const auto loaded = parseProject(text);
    REQUIRE(loaded);
    CHECK(sameContent(loaded->document, hidden->document));
    CHECK(meshOf(loaded->document, id).face(mesh::FaceId{2})->hidden);
    CHECK(meshOf(loaded->document, id).edge(mesh::EdgeId{9})->hidden);

    // Bad flags are rejected.
    std::string bad = text;
    const auto at = bad.find("\"hidden\": true");
    REQUIRE(at != std::string::npos);
    bad.replace(at, 14, "\"hidden\": 1");
    CHECK_FALSE(parseProject(bad));

    std::ifstream in(std::filesystem::path(PULLFORM_TEST_DATA) / "v3-mesh-group.pullform.json", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const auto v3 = parseProject(ss.str());
    REQUIRE_MESSAGE(v3.ok(), (v3.ok() ? "" : v3.error()));
    CHECK(v3->fileVersion == 3);
    const auto& m = meshOf(v3->document, ObjectId{1});
    CHECK(m.faces().size() == 2);
    CHECK(m.edge(mesh::EdgeId{2})->soft);
    CHECK(m.edge(mesh::EdgeId{2})->smooth);
    CHECK(m.edge(mesh::EdgeId{6})->soft);
    CHECK_FALSE(m.edge(mesh::EdgeId{6})->smooth);
    CHECK_FALSE(anyHidden(v3->document));
    const auto again = parseProject(serializeProject(v3->document, std::nullopt));
    REQUIRE(again);
    CHECK(again->fileVersion == 4);
    CHECK(sameContent(again->document, v3->document));
}

TEST_CASE("Flip Along mirrors in place across the selection's centre and keeps solids facing out") {
    auto [doc, id] = prism(5, {3, 0, 0}); // a pentagon: not symmetric about x
    const Bounds before = objectBounds(*doc.find(id));
    const Vec3 corner = *objectVertex(*doc.find(id), 0); // the corner at angle 0: the right-most point

    const auto flipped = flipAlong(doc, CleanupScope{{}, {id}}, 0);
    REQUIRE(flipped);
    const ProfileObject& o = *flipped->document.find(id);
    const Bounds after = objectBounds(o);
    CHECK(glm::length(after.min - before.min) < 1e-9);
    CHECK(glm::length(after.max - before.max) < 1e-9);
    const auto& m = meshOf(flipped->document, id);
    CHECK_FALSE(mesh::validateMesh(m, "mesh").has_value());
    CHECK(outwardShell(m));
    const double mirror = before.center().x;
    const Vec3 moved = localToWorld(o, m.vertex(mesh::VertexId{1})->position);
    CHECK(moved.x == doctest::Approx(2.0 * mirror - corner.x));
    CHECK(moved.y == doctest::Approx(corner.y));
    // The polygon records survive and still describe their edges.
    REQUIRE(m.curves().size() == 2);
    for (const auto& [cid, c] : m.curves()) {
        CHECK(localToWorld(o, c.center).x == doctest::Approx(2.0 * mirror - 3.0)); // the pentagon's centre was at x = 3
        for (std::size_t k = 0; k < c.edges.size(); ++k) {
            // Vertex k of the curve sits at start + k * sweep / segments about its normal.
            const double a = c.start + c.sweep * static_cast<double>(k) / c.segments;
            const Vec3 y = glm::cross(c.normal, c.xAxis);
            const Vec3 expected = c.center + c.radius * (std::cos(a) * c.xAxis + std::sin(a) * y);
            const auto* e = m.edge(c.edges[k]);
            const Vec3 p0 = m.vertex(e->v0)->position, p1 = m.vertex(e->v1)->position;
            CHECK(std::min(glm::length(p0 - expected), glm::length(p1 - expected)) < 1e-9);
        }
    }
    // Flipping twice is back where it started.
    const auto twice = flipAlong(flipped->document, CleanupScope{{}, {id}}, 0);
    REQUIRE(twice);
    CHECK(glm::length(localToWorld(*twice->document.find(id), meshOf(twice->document, id).vertex(mesh::VertexId{1})->position) - corner) < 1e-9);

    // Blue flips a box upside down in place; a rotated object flips along the world axis.
    auto rotated = rotateObject(doc, id, {3, 0, 0}, {0, 0, 1}, 0.3);
    REQUIRE(rotated);
    const auto up = flipAlong(rotated->document, CleanupScope{{}, {id}}, 2);
    REQUIRE(up);
    CHECK(outwardShell(meshOf(up->document, id)));
    const Bounds b0 = objectBounds(*rotated->document.find(id));
    const Bounds b1 = objectBounds(*up->document.find(id));
    CHECK(glm::length(b0.min - b1.min) < 1e-9);
    CHECK(glm::length(b0.max - b1.max) < 1e-9);

    // Part of a solid cannot flip without bending the faces around it.
    CHECK_FALSE(flipAlong(doc, CleanupScope{{face(id, 3)}, {}}, 2));
    CHECK_FALSE(flipAlong(doc, CleanupScope{}, 0));
    CHECK_FALSE(flipAlong(doc, CleanupScope{{}, {id}}, 3));
}

TEST_CASE("cleanup scope: elements as selected, objects selected whole stand for all of their geometry") {
    SelectionSet set = SelectionSet::ofObjects({ObjectId{1}, ObjectId{2}});
    set.addElements({face(ObjectId{3}, 4), ElementRef{ObjectId{3}, ElementKind::Vertex, 1}}, std::nullopt);
    const CleanupScope scope = cleanupScope(set);
    CHECK(scope.objects == std::vector<ObjectId>{ObjectId{1}, ObjectId{2}});
    CHECK(scope.elements == std::vector<ElementRef>{face(ObjectId{3}, 4)});
}

TEST_CASE("Hide: the ends of a hidden loose line are neither picked nor snapped to") {
    auto line = drawEdge(Document{}, std::nullopt, {0, 0, 0}, {1, 0, 0});
    REQUIRE(line);
    const ObjectId id = *line->objectId;
    const auto hidden = setHidden(line->document, CleanupScope{{}, {id}}, true);
    REQUIRE(hidden);
    const ProfileObject& o = *hidden->document.find(id);
    const Ray down{{0.0, 0.0, 5.0}, {0, 0, -1}}; // straight at an end point
    const auto topo = buildTopology(o);
    std::vector<PickTarget> targets{{id, &topo}};
    CHECK_FALSE(pickElement(down, targets, ElementPickOptions{1e-3}));
    CHECK(snapFeatures(o).vertices.empty());
    // Shown with View > Hidden Geometry, the end is picked again.
    const auto shown = buildTopology(o, true);
    std::vector<PickTarget> shownTargets{{id, &shown}};
    const auto hit = pickElement(down, shownTargets, ElementPickOptions{1e-3});
    REQUIRE(hit);
    CHECK(hit->element.kind == ElementKind::Vertex);
    // A vertex shared with a visible edge stays: unhiding brings the line back.
    const auto back = unhideAll(hidden->document);
    REQUIRE(back);
    CHECK(snapFeatures(*back->document.find(id)).vertices.size() == 2u);
}
