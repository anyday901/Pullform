#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/arc.hpp"
#include "core/geometry/mesh.hpp"

#include <glm/geometric.hpp>

#include <numbers>

namespace pf {

namespace {

constexpr double kPi = std::numbers::pi;
const Vec3 kUp{0.0, 0.0, 1.0};

bool nearVec(const Vec3& a, const Vec3& b, double eps = 1e-9) { return glm::length(a - b) <= eps; }

/// Every arc point is on the circle.
void checkOnCircle(const ArcGeometry& arc) {
    for (const Vec3& p : arcPoints(arc, 16)) CHECK(test::near(glm::length(p - arc.center), arc.radius, 1e-9));
}

} // namespace

TEST_CASE("arc through three points") {
    const auto arc = arcThroughPoints({1, 0, 0}, {0, 1, 0}, {-1, 0, 0});
    REQUIRE(arc);
    CHECK(nearVec(arc->center, Vec3{0.0}));
    CHECK(test::near(arc->radius, 1.0));
    CHECK(nearVec(arc->normal, kUp));
    CHECK(test::near(arc->sweep, kPi));
    CHECK(nearVec(arcStartPoint(*arc), {1, 0, 0}));
    CHECK(nearVec(arcEndPoint(*arc), {-1, 0, 0}));
    CHECK(nearVec(arcMidPoint(*arc), {0, 1, 0}));
    checkOnCircle(*arc);

    // The same ends through the lower point turn the other way.
    const auto lower = arcThroughPoints({1, 0, 0}, {0, -1, 0}, {-1, 0, 0});
    REQUIRE(lower);
    CHECK(test::near(lower->sweep, -kPi));
    CHECK(nearVec(arcMidPoint(*lower), {0, -1, 0}));

    // More than half a turn: through a point beyond the chord's far side.
    const auto big = arcThroughPoints({1, 0, 0}, {0, -1, 0}, {0, 1, 0});
    REQUIRE(big);
    CHECK(test::near(std::abs(big->sweep), 1.5 * kPi));

    // Collinear and coincident points are refused with a message.
    CHECK_FALSE(arcThroughPoints({0, 0, 0}, {1, 0, 0}, {2, 0, 0}));
    CHECK_FALSE(arcThroughPoints({0, 0, 0}, {0, 0, 0}, {2, 0, 0}));
}

TEST_CASE("arc on a vertical plane keeps a canonical normal") {
    const auto arc = arcThroughPoints({0, 0, 0}, {1, 0, 1}, {2, 0, 0});
    REQUIRE(arc);
    CHECK(test::near(std::abs(arc->normal.y), 1.0));
    CHECK(arc->normal.y > 0.0);
    CHECK(nearVec(arcMidPoint(*arc), {1, 0, 1}));
}

TEST_CASE("two-point arc with bulge") {
    // A semicircle: bulge of half the chord.
    const auto semi = arcFromBulge({0, 0, 0}, {2, 0, 0}, 1.0, kUp);
    REQUIRE(semi);
    CHECK(test::near(semi->radius, 1.0));
    CHECK(nearVec(arcMidPoint(*semi), {1, 1, 0}));
    CHECK(test::near(arcBulge(*semi), 1.0));
    CHECK(nearVec(arcStartPoint(*semi), {0, 0, 0}));
    CHECK(nearVec(arcEndPoint(*semi), {2, 0, 0}));

    // Negative bulge goes to the right; a shallow one has a large radius.
    const auto shallow = arcFromBulge({0, 0, 0}, {2, 0, 0}, -0.25, kUp);
    REQUIRE(shallow);
    CHECK(test::near(arcBulge(*shallow), -0.25));
    CHECK(test::near(shallow->radius, (1.0 + 0.0625) / 0.5));
    CHECK(nearVec(arcMidPoint(*shallow), {1, -0.25, 0}));

    // The plane's orientation is kept: drawn on a wall facing -Y.
    const auto wall = arcFromBulge({0, 0, 0}, {2, 0, 0}, 0.5, {0, -1, 0});
    REQUIRE(wall);
    CHECK(wall->normal.y < 0.0);
    CHECK(nearVec(arcMidPoint(*wall), {1, 0, 0.5}));

    CHECK_FALSE(arcFromBulge({0, 0, 0}, {2, 0, 0}, 0.0, kUp));
    CHECK_FALSE(arcFromBulge({0, 0, 0}, {0, 0, 0}, 1.0, kUp));
}

TEST_CASE("pie arc from a centre") {
    const auto arc = arcFromCenter({0, 0, 0}, {2, 0, 0}, 0.5 * kPi, kUp);
    REQUIRE(arc);
    CHECK(test::near(arc->radius, 2.0));
    CHECK(nearVec(arcEndPoint(*arc), {0, 2, 0}));
    CHECK_FALSE(arcFromCenter({0, 0, 0}, {2, 0, 0}, 2.0 * kPi, kUp)); // a full circle is the Circle tool's
    CHECK_FALSE(arcFromCenter({0, 0, 0}, {0, 0, 0}, 1.0, kUp));
}

TEST_CASE("tangent arc continues a direction") {
    // Heading +X from the origin, ending at (1, 1): a quarter circle about (0, 1).
    const auto arc = tangentArc({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, kUp);
    REQUIRE(arc);
    CHECK(nearVec(arc->center, {0, 1, 0}));
    CHECK(test::near(arc->sweep, 0.5 * kPi));
    CHECK(nearVec(arcStartTangent(*arc), {1, 0, 0}));
    CHECK(nearVec(arcEndTangent(*arc), {0, 1, 0}));
    checkOnCircle(*arc);

    // Turning right.
    const auto right = tangentArc({0, 0, 0}, {1, 0, 0}, {1, -1, 0}, kUp);
    REQUIRE(right);
    CHECK(test::near(right->sweep, -0.5 * kPi));
    CHECK(nearVec(arcStartTangent(*right), {1, 0, 0}));

    // Ending behind the start: more than half a turn, still tangent.
    const auto back = tangentArc({0, 0, 0}, {1, 0, 0}, {-1, 1, 0}, kUp);
    REQUIRE(back);
    CHECK(back->sweep > kPi);
    CHECK(nearVec(arcStartTangent(*back), {1, 0, 0}));

    CHECK_FALSE(tangentArc({0, 0, 0}, {1, 0, 0}, {3, 0, 0}, kUp)); // straight ahead
}

TEST_CASE("sweep unwrapping follows the pointer past half a turn") {
    double sweep = 0.0;
    for (int deg = 10; deg <= 300; deg += 10) {
        const double a = deg * kPi / 180.0;
        sweep = unwrapSweep(sweep, std::atan2(std::sin(a), std::cos(a)));
    }
    CHECK(test::near(sweep, 300.0 * kPi / 180.0, 1e-12));
    CHECK(test::near(signedAngle({1, 0, 0}, {0, 1, 0}, kUp), 0.5 * kPi));
}

TEST_CASE("segment counts") {
    CHECK(defaultArcSegments(2.0 * kPi) == 24);
    CHECK(defaultArcSegments(0.5 * kPi) == 6);
    CHECK(defaultArcSegments(-kPi) == 12);
    CHECK(defaultArcSegments(0.01) == 2);
    CHECK_FALSE(checkArcSegments(12));
    CHECK(checkArcSegments(1));
    CHECK(checkArcSegments(kMaxCurveSegments + 1));
    const auto arc = arcFromBulge({0, 0, 0}, {2, 0, 0}, 1.0, kUp);
    REQUIRE(arc);
    const auto pts = arcPoints(*arc, 12);
    CHECK(pts.size() == 13);
    CHECK(pts.front() == arcStartPoint(*arc));
}

TEST_CASE("an arc in the mesh keeps its curve record") {
    const auto arc = arcFromBulge({0, 0, 0}, {2, 0, 0}, 1.0, kUp);
    REQUIRE(arc);
    const auto r = drawArc(Document{}, std::nullopt, *arc, 12);
    REQUIRE(r);
    const auto& o = *r->document.objects.at(0);
    REQUIRE(o.isMeshGroup());
    const auto& m = *o.mesh;
    CHECK(m.edges().size() == 12);
    REQUIRE(m.curves().size() == 1);
    const auto& c = m.curves().begin()->second;
    CHECK(c.kind == mesh::CurveKind::Arc);
    CHECK(c.segments == 12);
    CHECK(c.edges.size() == 12);
    CHECK(test::near(c.radius, 1.0));
    for (const auto e : c.edges) CHECK(m.edge(e)->curve == mesh::CurveId{m.curves().begin()->first});
    CHECK(m.faces().empty()); // an open arc makes no face
    CHECK_FALSE(validateDocument(r->document));
    // The record is in object-local space: the group sits at the arc's start.
    CHECK(nearVec(localToWorld(o, c.center), arc->center));

    CHECK_FALSE(drawArc(Document{}, std::nullopt, *arc, 1));
}

TEST_CASE("a pie closes into a face") {
    const auto arc = arcFromCenter({0, 0, 0}, {1, 0, 0}, 0.5 * kPi, kUp);
    REQUIRE(arc);
    const auto r = drawArc(Document{}, std::nullopt, *arc, 6, true);
    REQUIRE(r);
    const auto& m = *r->document.objects.at(0)->mesh;
    CHECK(m.edges().size() == 8);
    CHECK(m.faces().size() == 1);
    CHECK(m.curves().size() == 1);
    CHECK_FALSE(validateDocument(r->document));
}

TEST_CASE("an arc closing a line loop makes one face (D shape)") {
    auto r = drawEdge(Document{}, std::nullopt, {0, 0, 0}, {2, 0, 0});
    REQUIRE(r);
    const auto arc = arcFromBulge({2, 0, 0}, {0, 0, 0}, 1.0, kUp);
    REQUIRE(arc);
    r = drawArc(r->document, std::nullopt, *arc, 12);
    REQUIRE(r);
    REQUIRE(r->document.objects.size() == 1);
    const auto& m = *r->document.objects.at(0)->mesh;
    CHECK(m.faces().size() == 1);
    CHECK(m.edges().size() == 13);
    CHECK_FALSE(validateDocument(r->document));
}

TEST_CASE("rounded rectangle from lines and tangent arcs is one face") {
    // A 4 x 2 m rectangle with 0.5 m corner radii, drawn as a chain: line,
    // tangent arc, line, tangent arc, ... each arc continuing the line before it.
    const double w = 4.0;
    const double d = 2.0;
    const double r = 0.5;
    Document doc;
    Vec3 at{r, 0, 0};
    const auto line = [&](const Vec3& to) {
        auto res = drawEdge(doc, std::nullopt, at, to);
        REQUIRE(res);
        doc = res->document;
        const Vec3 dir = glm::normalize(to - at);
        at = to;
        return dir;
    };
    const auto turn = [&](const Vec3& dir, const Vec3& to) {
        const auto arc = tangentArc(at, dir, to, kUp);
        REQUIRE(arc);
        CHECK(test::near(arc->radius, r));
        auto res = drawArc(doc, std::nullopt, *arc, defaultArcSegments(arc->sweep));
        REQUIRE(res);
        doc = res->document;
        at = to;
    };
    turn(line({w - r, 0, 0}), {w, r, 0});
    turn(line({w, d - r, 0}), {w - r, d, 0});
    turn(line({r, d, 0}), {0, d - r, 0});
    turn(line({0, r, 0}), {r, 0, 0});

    REQUIRE(doc.objects.size() == 1);
    const auto& m = *doc.objects.at(0)->mesh;
    CHECK(m.faces().size() == 1);
    CHECK(m.curves().size() == 4);
    CHECK(m.edges().size() == 4 + 4 * 6);
    const auto& face = m.faces().begin()->second;
    CHECK(face.loops.size() == 1);
    CHECK(test::near(mesh::signedArea(m.loopPositions(face.loops[0]), face.normal),
                     w * d - (4.0 - kPi) * r * r, 0.05)); // segments cut a little off each corner
    CHECK_FALSE(validateDocument(doc));
}

} // namespace pf
