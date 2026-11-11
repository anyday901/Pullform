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

// ---- The Arc tools, driven with synthetic pointer events -------------------------

namespace pf {

namespace {

using test::downAt;

PointerInput at(const Editor& editor, double x, double y, int clicks = 1, double pixelAngle = 0.0) {
    PointerInput in = downAt(editor, x, y, {x * 100.0, y * 100.0});
    in.clicks = clicks;
    in.pixelAngle = pixelAngle;
    return in;
}

void click(ToolController& tools, const Editor& editor, double x, double y, int clicks = 1, double pixelAngle = 0.0) {
    tools.pointerMove(at(editor, x, y, 1, pixelAngle));
    tools.pointerDown(at(editor, x, y, clicks, pixelAngle));
    tools.pointerUp(at(editor, x, y, clicks, pixelAngle));
}

void line(Editor& editor, const Vec3& a, const Vec3& b) {
    REQUIRE(editor.commit("Draw line", [&](const Document& d) { return drawEdge(d, std::nullopt, a, b); }));
}

const mesh::Mesh& onlyMesh(const Editor& editor) {
    REQUIRE(editor.document().objects.size() == 1);
    REQUIRE(editor.document().objects[0]->isMeshGroup());
    return *editor.document().objects[0]->mesh;
}

} // namespace

TEST_CASE("Arc tool: start, end, bulge") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Arc);
    CHECK(toolName(Tool::Arc) == "Arc");
    click(tools, editor, 0, 0);
    CHECK(tools.active());
    click(tools, editor, 2, 0);
    tools.pointerMove(at(editor, 1, 0.5));
    const auto* pv = std::get_if<ArcPreview>(&editor.preview());
    REQUIRE(pv);
    CHECK(pv->value == ArcPreview::Value::Bulge);
    CHECK(test::near(pv->amount, 0.5, 1e-9));
    CHECK(editor.undoDepth() == 0); // moving only previews
    click(tools, editor, 1, 1);
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 1);
    CHECK(editor.undoLabel() == "Draw arc");
    const auto& m = onlyMesh(editor);
    REQUIRE(m.curves().size() == 1);
    CHECK(m.curves().begin()->second.segments == 12); // a half circle by default
    CHECK(test::near(m.curves().begin()->second.radius, 1.0));
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
}

TEST_CASE("Arc tool: typed length, bulge and segment count") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Arc);
    CHECK_FALSE(tools.submit("8s")); // before the first point: applies to the next arcs
    click(tools, editor, 0, 0);
    tools.pointerMove(at(editor, 1, 0));
    CHECK_FALSE(tools.submit("3000")); // millimetres: the end is 3 m along the pointer's direction
    tools.pointerMove(at(editor, 1.5, -0.4)); // right of the chord
    CHECK_FALSE(tools.submit("500"));
    CHECK_FALSE(tools.active());
    const auto& m = onlyMesh(editor);
    REQUIRE(m.curves().size() == 1);
    const auto& c = m.curves().begin()->second;
    CHECK(c.segments == 8);
    const ArcGeometry arc = arcOfCurve(c);
    const auto& o = *editor.document().objects[0];
    CHECK(nearVec(localToWorld(o, arcEndPoint(arc)), {3, 0, 0}, 1e-9));
    CHECK(nearVec(localToWorld(o, arcMidPoint(arc)), {1.5, -0.5, 0}, 1e-9));

    CHECK(tools.submit("1s"));    // too few
    CHECK(tools.submit("abc"));   // not a length before the first point either
}

TEST_CASE("Arc tool: tangent arc from a line end (cyan inference and double-click)") {
    Editor editor;
    ToolController tools(editor);
    line(editor, {0, 0, 0}, {1, 0, 0});
    tools.setTool(Tool::Arc);

    // Moving near the tangent bulge snaps to it.
    click(tools, editor, 1, 0);
    CHECK(tools.active());
    click(tools, editor, 2, 1);
    tools.pointerMove(at(editor, 1.7, 0.3, 1, 0.0005));
    const auto* pv = std::get_if<ArcPreview>(&editor.preview());
    REQUIRE(pv);
    CHECK(pv->tangent);
    click(tools, editor, 1.7, 0.3, 1, 0.0005);
    {
        const auto& m = onlyMesh(editor); // the arc joined the line's group
        REQUIRE(m.curves().size() == 1);
        const ArcGeometry arc = arcOfCurve(m.curves().begin()->second);
        CHECK(nearVec(arcStartTangent(arc), {1, 0, 0}, 1e-9));
        CHECK(test::near(arc.radius, 1.0, 1e-9));
    }
    REQUIRE(editor.undo());

    // Double-clicking the end point takes the tangent arc right away.
    click(tools, editor, 1, 0);
    click(tools, editor, 2, 1);
    tools.pointerDown(at(editor, 2, 1, 2));
    tools.pointerUp(at(editor, 2, 1, 2));
    CHECK_FALSE(tools.active());
    const auto& m = onlyMesh(editor);
    REQUIRE(m.curves().size() == 1);
    CHECK(nearVec(arcStartTangent(arcOfCurve(m.curves().begin()->second)), {1, 0, 0}, 1e-9));
}

TEST_CASE("Arc tool: half circle inference") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Arc);
    click(tools, editor, 0, 0);
    click(tools, editor, 2, 0);
    tools.pointerMove(at(editor, 1, 0.9, 1, 0.0005));
    const auto* pv = std::get_if<ArcPreview>(&editor.preview());
    REQUIRE(pv);
    CHECK(pv->halfCircle);
    CHECK(test::near(pv->radius, 1.0, 1e-9));
}

TEST_CASE("3 Point Arc tool") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::ThreePointArc);
    click(tools, editor, 1, 0);
    click(tools, editor, 0, 1);
    click(tools, editor, -1, 0);
    const auto& m = onlyMesh(editor);
    REQUIRE(m.curves().size() == 1);
    const auto& c = m.curves().begin()->second;
    CHECK(test::near(c.radius, 1.0, 1e-9));
    CHECK(test::near(std::abs(c.sweep), kPi, 1e-9));
    CHECK(m.faces().empty());
}

TEST_CASE("Pie tool: centre, radius, angle; the pie is a face") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Pie);
    click(tools, editor, 0, 0);
    click(tools, editor, 1, 0);
    // Sweep round past half a turn, the way the pointer circles.
    for (int deg = 15; deg <= 270; deg += 15) {
        const double a = deg * kPi / 180.0;
        tools.pointerMove(at(editor, std::cos(a), std::sin(a)));
    }
    const auto* pv = std::get_if<ArcPreview>(&editor.preview());
    REQUIRE(pv);
    CHECK(pv->value == ArcPreview::Value::Angle);
    CHECK(test::near(pv->amount, 1.5 * kPi, 1e-6));
    CHECK_FALSE(tools.submit("90")); // typed angle, in the direction of the sweep
    const auto& m = onlyMesh(editor);
    CHECK(m.faces().size() == 1);
    REQUIRE(m.curves().size() == 1);
    CHECK(test::near(m.curves().begin()->second.sweep, 0.5 * kPi, 1e-12));
    CHECK(editor.undoLabel() == "Draw pie");

    // Typed radius.
    click(tools, editor, 5, 5);
    tools.pointerMove(at(editor, 6, 5));
    CHECK_FALSE(tools.submit("2000"));
    tools.pointerMove(at(editor, 5, 7));
    click(tools, editor, 5, 7);
    CHECK(editor.document().objects.size() == 2);
    const auto& pie = *editor.document().objects[1]->mesh;
    REQUIRE(pie.curves().size() == 1);
    CHECK(test::near(pie.curves().begin()->second.radius, 2.0, 1e-9));
}

TEST_CASE("Arc tool: Escape cancels without an undo entry") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Arc);
    click(tools, editor, 0, 0);
    click(tools, editor, 1, 0);
    CHECK(tools.cancel());
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == 0);
    CHECK(editor.document().objects.empty());
}

TEST_CASE("acceptance: rounded rectangle from lines and tangent arcs is one face") {
    // Lines drawn as edges, each corner a tangent arc taken with a double-click
    // on its end point, exactly as the Arc tool is used after the Line tool.
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Arc);
    const auto corner = [&](double x0, double y0, double x1, double y1) {
        click(tools, editor, x0, y0);
        REQUIRE(tools.active());
        click(tools, editor, x1, y1);
        tools.pointerDown(at(editor, x1, y1, 2));
        tools.pointerUp(at(editor, x1, y1, 2));
        REQUIRE_FALSE(tools.active());
    };
    line(editor, {0.5, 0, 0}, {3.5, 0, 0});
    corner(3.5, 0, 4, 0.5);
    line(editor, {4, 0.5, 0}, {4, 1.5, 0});
    corner(4, 1.5, 3.5, 2);
    line(editor, {3.5, 2, 0}, {0.5, 2, 0});
    corner(0.5, 2, 0, 1.5);
    line(editor, {0, 1.5, 0}, {0, 0.5, 0});
    corner(0, 0.5, 0.5, 0);

    const auto& m = onlyMesh(editor);
    CHECK(m.faces().size() == 1);
    CHECK(m.curves().size() == 4);
    for (const auto& [id, c] : m.curves()) CHECK(test::near(c.radius, 0.5, 1e-9));
    CHECK_FALSE(validateDocument(editor.document()));
}

} // namespace pf
