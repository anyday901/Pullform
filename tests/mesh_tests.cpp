#include "helpers.hpp"

#include "core/geometry/mesh.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

// Inside pf::mesh so FaceId, Edge, ... are the mesh's (pf::FaceId is spelled out).
namespace pf::mesh {

namespace {

void drawSquare(Mesh& m, double x0, double y0, double size, double z = 0.0) {
    const Vec3 a{x0, y0, z};
    const Vec3 b{x0 + size, y0, z};
    const Vec3 c{x0 + size, y0 + size, z};
    const Vec3 d{x0, y0 + size, z};
    REQUIRE(m.addEdge(a, b));
    REQUIRE(m.addEdge(b, c));
    REQUIRE(m.addEdge(c, d));
    REQUIRE(m.addEdge(d, a));
}

double faceArea(const Mesh& m, const Face& f) {
    double area = 0.0;
    for (const auto& loop : f.loops) area += signedArea(m.loopPositions(loop), f.normal);
    return area;
}

double totalTriangleArea(const Mesh& m, FaceId id) {
    double area = 0.0;
    for (const auto& t : triangulateFace(m, id)) {
        const Vec3 a = m.vertex(t[0])->position;
        const Vec3 b = m.vertex(t[1])->position;
        const Vec3 c = m.vertex(t[2])->position;
        area += 0.5 * glm::dot(glm::cross(b - a, c - a), m.face(id)->normal);
    }
    return area;
}

void requireValid(const Mesh& m) {
    const auto problem = validateMesh(m, "mesh");
    REQUIRE_MESSAGE(!problem, problem.value_or(""));
}

} // namespace

TEST_CASE("mesh: four edges drawn in a loop make one face") {
    Mesh m;
    REQUIRE(m.addEdge({0, 0, 0}, {2, 0, 0}));
    REQUIRE(m.addEdge({2, 0, 0}, {2, 1, 0}));
    REQUIRE(m.addEdge({2, 1, 0}, {0, 1, 0}));
    CHECK(m.faces().empty()); // still open
    const auto closing = m.addEdge({0, 1, 0}, {0, 0, 0});
    REQUIRE(closing);
    REQUIRE(closing->createdFaces.size() == 1);
    CHECK(m.vertices().size() == 4); // endpoints welded
    CHECK(m.edges().size() == 4);
    REQUIRE(m.faces().size() == 1);
    const Face& f = m.faces().begin()->second;
    CHECK(f.loops.size() == 1);
    CHECK(f.normal.z == doctest::Approx(1.0)); // ground faces face up
    CHECK(faceArea(m, f) == doctest::Approx(2.0));
    CHECK(totalTriangleArea(m, FaceId{m.faces().begin()->first}) == doctest::Approx(2.0));
    requireValid(m);
}

TEST_CASE("mesh: endpoints weld within 1 micrometer, not beyond") {
    Mesh m;
    REQUIRE(m.addEdge({0, 0, 0}, {1, 0, 0}));
    REQUIRE(m.addEdge({1 + 5e-7, 0, 0}, {1, 1, 0}));
    CHECK(m.vertices().size() == 3);
    REQUIRE(m.addEdge({1, 1 + 5e-6, 0}, {0, 1, 0}));
    CHECK(m.vertices().size() == 5);
    CHECK_FALSE(m.addEdge({0, 0, 0}, {5e-7, 0, 0})); // collapses to a point
}

TEST_CASE("mesh: a line across a face splits it in two, erasing it merges them back") {
    Mesh m;
    drawSquare(m, 0, 0, 2);
    REQUIRE(m.faces().size() == 1);
    const FaceId original{m.faces().begin()->first};

    // From the middle of the bottom edge to the middle of the top edge: both
    // edges are split where the line lands.
    const auto split = m.addEdge({1, 0, 0}, {1, 2, 0});
    REQUIRE(split);
    REQUIRE(split->edges.size() == 1);
    CHECK(split->removedFaces == std::vector<FaceId>{original});
    REQUIRE(split->createdFaces.size() == 2);
    CHECK(m.faces().size() == 2);
    CHECK(m.edges().size() == 7);
    for (const auto& [id, f] : m.faces()) CHECK(faceArea(m, f) == doctest::Approx(2.0));
    requireValid(m);
    CHECK(m.facesOfEdge(split->edges[0]).size() == 2);

    const auto erased = m.eraseEdge(split->edges[0]);
    REQUIRE(erased);
    REQUIRE(erased->mergedFace);
    CHECK(m.faces().size() == 1);
    CHECK(faceArea(m, *m.face(*erased->mergedFace)) == doctest::Approx(4.0));
    requireValid(m);
    CHECK(m.face(original) == nullptr); // ids are never reused
    CHECK(erased->mergedFace->value > original.value);
}

TEST_CASE("mesh: a diagonal splits a square into two triangles") {
    Mesh m;
    drawSquare(m, 0, 0, 1);
    const auto d = m.addEdge({0, 0, 0}, {1, 1, 0});
    REQUIRE(d);
    CHECK(d->createdFaces.size() == 2);
    for (const auto& [id, f] : m.faces()) {
        CHECK(f.loops[0].size() == 3);
        CHECK(faceArea(m, f) == doctest::Approx(0.5));
    }
    requireValid(m);
}

TEST_CASE("mesh: crossing lines split each other and make four faces") {
    Mesh m;
    drawSquare(m, 0, 0, 2);
    REQUIRE(m.addEdge({1, 0, 0}, {1, 2, 0}));
    const auto cross = m.addEdge({0, 1, 0}, {2, 1, 0});
    REQUIRE(cross);
    CHECK(cross->edges.size() == 2); // split at the crossing
    CHECK(m.faces().size() == 4);
    for (const auto& [id, f] : m.faces()) CHECK(faceArea(m, f) == doctest::Approx(1.0));
    CHECK(m.findVertex({1, 1, 0}).has_value());
    requireValid(m);
}

TEST_CASE("mesh: a loose line inside a face does not split it") {
    Mesh m;
    drawSquare(m, 0, 0, 2);
    const auto spur = m.addEdge({0, 0, 0}, {1, 1, 0});
    REQUIRE(spur);
    CHECK(spur->createdFaces.empty());
    CHECK(m.faces().size() == 1);
    // Closing the spur into a triangle with a corner splits the face.
    const auto close = m.addEdge({1, 1, 0}, {2, 0, 0});
    REQUIRE(close);
    CHECK(close->createdFaces.size() == 2);
    CHECK(m.faces().size() == 2);
    requireValid(m);
}

TEST_CASE("mesh: a loop drawn inside a face becomes a hole plus an inner face") {
    Mesh m;
    drawSquare(m, 0, 0, 4);
    drawSquare(m, 1, 1, 1);
    REQUIRE(m.faces().size() == 2);
    const Face* outer = nullptr;
    const Face* inner = nullptr;
    FaceId outerId;
    for (const auto& [id, f] : m.faces()) {
        if (f.loops.size() == 2) {
            outer = &f;
            outerId = FaceId{id};
        } else {
            inner = &f;
        }
    }
    REQUIRE(outer);
    REQUIRE(inner);
    CHECK(faceArea(m, *outer) == doctest::Approx(15.0));
    CHECK(faceArea(m, *inner) == doctest::Approx(1.0));
    CHECK(totalTriangleArea(m, outerId) == doctest::Approx(15.0));
    requireValid(m);

    // Erasing an edge of the inner loop merges the inner face back into the outer one.
    const auto edge = m.findEdge(*m.findVertex({1, 1, 0}), *m.findVertex({2, 1, 0}));
    REQUIRE(edge);
    const auto erased = m.eraseEdge(*edge);
    REQUIRE(erased);
    CHECK(erased->mergedFace == outerId);
    REQUIRE(m.faces().size() == 1);
    CHECK(m.face(outerId)->loops.size() == 1);
    CHECK(faceArea(m, *m.face(outerId)) == doctest::Approx(16.0));
    requireValid(m);
}

TEST_CASE("mesh: a loop drawn around an existing face gets it as a hole") {
    Mesh m;
    drawSquare(m, 1, 1, 1);
    drawSquare(m, 0, 0, 4);
    REQUIRE(m.faces().size() == 2);
    double total = 0.0;
    for (const auto& [id, f] : m.faces()) total += faceArea(m, f);
    CHECK(total == doctest::Approx(16.0));
    requireValid(m);
}

TEST_CASE("mesh: erasing an edge between faces at an angle removes them") {
    Mesh m;
    // Two squares meeting at a right angle along the x axis.
    drawSquare(m, 0, 0, 1);
    REQUIRE(m.addEdge({0, 0, 0}, {0, 0, 1}));
    REQUIRE(m.addEdge({0, 0, 1}, {1, 0, 1}));
    REQUIRE(m.addEdge({1, 0, 1}, {1, 0, 0}));
    REQUIRE(m.faces().size() == 2);
    requireValid(m);
    const auto shared = m.findEdge(*m.findVertex({0, 0, 0}), *m.findVertex({1, 0, 0}));
    REQUIRE(shared);
    CHECK(m.facesOfEdge(*shared).size() == 2);
    const auto erased = m.eraseEdge(*shared);
    REQUIRE(erased);
    CHECK_FALSE(erased->mergedFace);
    CHECK(m.faces().empty());
    CHECK(m.edges().size() == 6);
    requireValid(m);
}

TEST_CASE("mesh: an L-shaped (non-convex) loop triangulates to its area") {
    Mesh m;
    const std::vector<Vec3> pts{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}};
    for (std::size_t i = 0; i < pts.size(); ++i) REQUIRE(m.addEdge(pts[i], pts[(i + 1) % pts.size()]));
    REQUIRE(m.faces().size() == 1);
    const FaceId id{m.faces().begin()->first};
    CHECK(faceArea(m, *m.face(id)) == doctest::Approx(3.0));
    CHECK(triangulateFace(m, id).size() == 4);
    CHECK(totalTriangleArea(m, id) == doctest::Approx(3.0));
}

TEST_CASE("mesh: a vertical loop makes a vertical face") {
    Mesh m;
    REQUIRE(m.addEdge({0, 0, 0}, {1, 0, 0}));
    REQUIRE(m.addEdge({1, 0, 0}, {1, 0, 1}));
    REQUIRE(m.addEdge({1, 0, 1}, {0, 0, 1}));
    REQUIRE(m.addEdge({0, 0, 1}, {0, 0, 0}));
    REQUIRE(m.faces().size() == 1);
    CHECK(std::abs(m.faces().begin()->second.normal.y) == doctest::Approx(1.0));
    requireValid(m);
}

TEST_CASE("mesh: connected elements follow shared vertices") {
    Mesh m;
    drawSquare(m, 0, 0, 1);
    drawSquare(m, 5, 5, 1);
    const auto c = connectedElements(m, *m.findVertex({0, 0, 0}));
    CHECK(c.vertices.size() == 4);
    CHECK(c.edges.size() == 4);
    CHECK(c.faces.size() == 1);
}

TEST_CASE("mesh: validation rejects broken meshes") {
    Mesh m;
    drawSquare(m, 0, 0, 1);
    requireValid(m);

    Mesh flipped = m;
    Face f = flipped.faces().begin()->second;
    f.normal = -f.normal;
    flipped.insertFace(FaceId{flipped.faces().begin()->first}, f);
    CHECK(validateMesh(flipped, "m").has_value());

    Mesh dup = m;
    dup.insertEdge(EdgeId{dup.nextEdgeId()}, *dup.edge(EdgeId{1}));
    CHECK(validateMesh(dup, "m").has_value());

    Mesh missing;
    missing.insertEdge(EdgeId{1}, Edge{VertexId{1}, VertexId{2}, {}, false, false});
    CHECK(validateMesh(missing, "m").has_value());
}

TEST_CASE("mesh: profile objects map to meshes with deterministic ids") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "2000, 1000", "750");
    const ObjectId sheet = test::makeBox(editor, tools, "300, 400", nullptr, {4.0, 4.0});
    const auto& doc = editor.document();

    const ProfileObject& b = *doc.find(box);
    const Mesh mb = meshFromProfile(b);
    requireValid(mb);
    CHECK(mb.vertices().size() == 8);
    CHECK(mb.edges().size() == 12);
    CHECK(mb.faces().size() == 6);
    // Vertex id = corner index + 1, in the same place as objectVertex.
    for (int i = 0; i < vertexCount(b); ++i) {
        const Vec3 world = localToWorld(b, mb.vertex(VertexId{static_cast<std::uint32_t>(i + 1)})->position);
        CHECK(glm::length(world - *objectVertex(b, i)) < 1e-12);
    }
    // Face ids follow the semantic faces; normals agree with buildFaces.
    for (const auto& poly : buildFaces(b)) {
        const FaceId id = faceIdFor(poly.id);
        REQUIRE(mb.face(id));
        CHECK(semanticFace(b, id) == poly.id);
        CHECK(glm::length(mb.face(id)->normal - poly.normal) < 1e-12);
        CHECK(totalTriangleArea(mb, id) > 0.0);
    }
    // Every edge has exactly two faces: a closed solid.
    for (const auto& [id, e] : mb.edges()) CHECK(mb.facesOfEdge(EdgeId{id}).size() == 2);

    const Mesh ms = meshFromProfile(*doc.find(sheet));
    requireValid(ms);
    CHECK(ms.faces().size() == 1);
    CHECK(semanticFace(*doc.find(sheet), FaceId{1}) == pf::FaceId::profile());
    CHECK(objectMesh(*doc.find(sheet))->faces().size() == 1);
}

} // namespace pf::mesh
