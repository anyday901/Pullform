#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/tolerance.hpp"
#include "core/geometry/rectangle.hpp"
#include "core/geometry/solid.hpp"

#include <algorithm>
#include <set>

using namespace pf;
using pf::test::near;

namespace {

ProfileObject box(double w, double d, double h, Vec3 at = {0, 0, 0}) {
    Document doc;
    auto r = createRectangle(doc, *rectangleFromCorners(at, at + Vec3{w, d, 0}));
    REQUIRE(r.ok());
    if (h > 0) r = extrudeProfile(r->document, *r->objectId, h);
    REQUIRE(r.ok());
    return *r->document.objects.front();
}

} // namespace

TEST_CASE("scenario 1: 2000 x 1000 mm rectangle extruded to 750 mm has 2 x 1 x 0.75 m bounds") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "750");
    const auto* o = editor.document().find(id);
    REQUIRE(o);
    const Bounds b = objectBounds(*o);
    CHECK(near(b.size().x, 2.0));
    CHECK(near(b.size().y, 1.0));
    CHECK(near(b.size().z, 0.75));
    CHECK(near(b.min.z, 0.0));
}

TEST_CASE("scenario 2: the prism is a closed, outward-wound solid with six logical faces") {
    const auto o = box(2.0, 1.0, 0.75);
    const auto mesh = buildMesh(o);
    CHECK(mesh.closed);
    CHECK(mesh.faces.size() == 6);
    std::set<std::string> ids;
    for (const auto& f : mesh.faces) ids.insert(faceKey(f.id));
    CHECK(ids == std::set<std::string>{"base", "cap", "side-0", "side-1", "side-2", "side-3"});
    CHECK(mesh.triangleFaces.size() == 12);
    CHECK(mesh.edges.size() == 24); // 12 real edges, no triangulation diagonals

    const auto check = checkSolid(mesh);
    CHECK(check.ok());
    CHECK(check.closed);
    CHECK(check.consistentlyOriented);
    CHECK(check.normalsMatchWinding);
    CHECK(near(check.signedVolume, 1.5, 1e-9));

    // Each face's normal points away from the solid's center.
    const Vec3 center{1.0, 0.5, 0.375};
    for (const auto& f : buildFaces(o)) {
        CHECK(glm::dot(f.normal, f.loop[0] - center) > 0.0);
    }
}

TEST_CASE("scenario 2: rays through either triangle of the cap resolve to the same logical face") {
    const auto o = box(2.0, 1.0, 0.75);
    const auto mesh = buildMesh(o);
    // The cap diagonal runs from (0,0) to (2,1). These rays hit opposite triangles.
    const Ray belowDiagonal{{1.5, 0.2, 5.0}, {0, 0, -1}};
    const Ray aboveDiagonal{{0.3, 0.8, 5.0}, {0, 0, -1}};
    const auto hitA = pickObject(belowDiagonal, o, mesh);
    const auto hitB = pickObject(aboveDiagonal, o, mesh);
    REQUIRE(hitA);
    REQUIRE(hitB);
    CHECK(hitA->faceId == FaceId::cap());
    CHECK(hitB->faceId == FaceId::cap());
    CHECK(near(hitA->point.z, 0.75));

    // The two hits really are different triangles.
    std::vector<std::size_t> capTris;
    for (std::size_t t = 0; t < mesh.triangleFaces.size(); ++t) {
        if (mesh.triangleFaces[t] == FaceId::cap()) capTris.push_back(t);
    }
    REQUIRE(capTris.size() == 2);
    int hitsA = 0, hitsB = 0;
    for (auto t : capTris) {
        const auto& p = mesh.positions;
        const auto& ix = mesh.indices;
        if (intersectTriangle(belowDiagonal, p[ix[3 * t]], p[ix[3 * t + 1]], p[ix[3 * t + 2]], true)) hitsA += 1 << t;
        if (intersectTriangle(aboveDiagonal, p[ix[3 * t]], p[ix[3 * t + 1]], p[ix[3 * t + 2]], true)) hitsB += 1 << t;
    }
    CHECK(hitsA != 0);
    CHECK(hitsB != 0);
    CHECK(hitsA != hitsB);

    // A side face is hit from the side, the base from below; nothing is hit from inside.
    CHECK(pickObject(Ray{{1.0, -5.0, 0.3}, {0, 1, 0}}, o, mesh)->faceId == FaceId::sideAt(0));
    CHECK(pickObject(Ray{{1.0, 0.5, -5.0}, {0, 0, 1}}, o, mesh)->faceId == FaceId::base());
    CHECK_FALSE(pickObject(Ray{{1.0, 0.5, 0.3}, {0, 0, 1}}, o, mesh).has_value());
}

TEST_CASE("a flipped triangle is detected by the solid check") {
    auto mesh = buildMesh(box(1.0, 1.0, 1.0));
    std::swap(mesh.indices[0], mesh.indices[1]);
    CHECK_FALSE(checkSolid(mesh).ok());
}

TEST_CASE("a planar rectangle is a two-sided sheet that picks as one face from both sides") {
    const auto o = box(2.0, 1.0, 0.0);
    const auto mesh = buildMesh(o);
    CHECK_FALSE(mesh.closed);
    REQUIRE(mesh.faces.size() == 1);
    CHECK(mesh.faces[0].id == FaceId::profile());
    CHECK(mesh.edges.size() == 8);
    CHECK(pickObject(Ray{{0.5, 0.5, 1.0}, {0, 0, -1}}, o, mesh)->faceId == FaceId::profile());
    CHECK(pickObject(Ray{{1.5, 0.2, 1.0}, {0, 0, -1}}, o, mesh)->faceId == FaceId::profile());
    CHECK(pickObject(Ray{{0.5, 0.5, -1.0}, {0, 0, 1}}, o, mesh)->faceId == FaceId::profile());
}

TEST_CASE("rectangles drawn in any direction normalize to positive size and CCW winding") {
    const Vec3 a{1.0, 1.0, 0.0};
    for (const Vec3 b : {Vec3{3, 2, 0}, Vec3{-1, 2, 0}, Vec3{-1, -0.5, 0}, Vec3{3, -0.5, 0}}) {
        const auto r = rectangleFromCorners(a, b);
        REQUIRE(r.ok());
        CHECK(r->width > 0);
        CHECK(r->depth > 0);
        Document doc;
        const auto created = createRectangle(doc, *r);
        REQUIRE(created.ok());
        const auto faces = buildFaces(*created->document.objects[0]);
        CHECK(faces[0].normal.z > 0.99);
        auto extruded = extrudeProfile(created->document, *created->objectId, 0.5);
        REQUIRE(extruded.ok());
        CHECK(checkSolid(buildMesh(*extruded->document.objects[0])).ok());
    }
}

TEST_CASE("degenerate and out-of-range rectangles are rejected") {
    CHECK_FALSE(rectangleFromCorners({0, 0, 0}, {0, 1, 0}).ok());
    CHECK_FALSE(rectangleFromCorners({0, 0, 0}, {1, 0.00005, 0}).ok());
    CHECK_FALSE(rectangleFromCorners({0, 0, 0}, {std::nan(""), 1, 0}).ok());
    CHECK_FALSE(rectangleFromCorners({0, 0, 0}, {INFINITY, 1, 0}).ok());
    CHECK_FALSE(rectangleFromCorners({0, 0, 0}, {20000, 1, 0}).ok());
    CHECK_FALSE(rectangleFromSignedSize({0, 0, 0}, 0.0, 1.0).ok());
    CHECK(rectangleFromSignedSize({0, 0, 0}, -2.0, 1.0)->corner.x == doctest::Approx(-2.0));
}

TEST_CASE("heights must be positive and finite") {
    CHECK(checkHeight(0.75) == std::nullopt);
    CHECK(checkHeight(0.0).has_value());
    CHECK(checkHeight(-0.5).has_value());
    CHECK(checkHeight(0.00001).has_value());
    CHECK(checkHeight(std::nan("")).has_value());
    CHECK(checkHeight(INFINITY).has_value());
}

TEST_CASE("touching and overlapping objects stay independent") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "1000, 1000", "500");
    const ObjectId b = test::makeBox(editor, tools, "1000, 1000", "800"); // same footprint, overlapping
    CHECK(a != b);
    CHECK(editor.document().objects.size() == 2);
    CHECK(editor.document().find(a)->extrusion->height == doctest::Approx(0.5));
    CHECK(editor.document().find(b)->extrusion->height == doctest::Approx(0.8));
}

TEST_CASE("typed rectangle sizes are stored exactly, even from an off-grid first corner") {
    // 0.3 + 2.0 - 0.3 is 1.9999999999999998 in doubles; the typed size must not go through it.
    const auto r = rectangleFromSignedSize({0.3, 0.7, 0.0}, 2.0, 0.1);
    REQUIRE(r.ok());
    CHECK(r->width == 2.0);
    CHECK(r->depth == 0.1);
    CHECK(r->corner.x == 0.3);

    // Drawn toward -X/-Y: the corner moves, the size stays exact.
    const auto flipped = rectangleFromSignedSize({0.3, 0.7, 0.0}, -2.0, -0.1);
    REQUIRE(flipped.ok());
    CHECK(flipped->width == 2.0);
    CHECK(flipped->depth == 0.1);
    CHECK(flipped->corner.x == doctest::Approx(-1.7));
    CHECK(flipped->corner.y == doctest::Approx(0.6));

    // The 0.1 mm minimum is accepted when typed exactly, wherever the first corner is.
    const auto tiny = rectangleFromSignedSize({1234.5678, -0.3, 0.0}, tol::kMinDimension, tol::kMinDimension);
    REQUIRE(tiny.ok());
    CHECK(tiny->width == tol::kMinDimension);
    Document doc;
    CHECK(createRectangle(doc, *tiny).ok());

    // The far corner must still be inside the model range.
    CHECK_FALSE(rectangleFromSignedSize({9999.0, 0.0, 0.0}, 2.0, 1.0).ok());
    CHECK_FALSE(rectangleFromSignedSize({20000.0, 0.0, 0.0}, 1.0, 1.0).ok());
}

TEST_CASE("rectangle corners outside the 10 km range are refused") {
    CHECK_FALSE(checkRectangleCorner({0.0, 0.0, 0.0}).has_value());
    CHECK_FALSE(checkRectangleCorner({-10000.0, 10000.0, 0.0}).has_value());
    CHECK(checkRectangleCorner({10000.5, 0.0, 0.0}).has_value());
    CHECK(checkRectangleCorner({0.0, std::nan(""), 0.0}).has_value());
}
