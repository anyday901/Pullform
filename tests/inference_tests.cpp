// The inference engine (item 7): on edge, intersection, center, parallel and
// perpendicular to an edge, along an axis from the start point, from a
// remembered point, the Shift lock and the tooltip names. Driven through the
// tape measure (the tool that infers from a start point today) with synthetic
// pointer events, and through the core engine directly.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/geometry/inference.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/snap.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

using namespace pf;
using pf::test::downAt;

namespace {

// Looking straight down from 50 m, the 12 px snap radius is 0.3 m on the ground.
constexpr double kPixelAngle = 0.0005;

PointerInput snapAt(const Editor& editor, double x, double y, Vec2 screen = {0, 0}, bool shift = false) {
    PointerInput in = downAt(editor, x, y, screen);
    in.pixelAngle = kPixelAngle;
    in.shift = shift;
    return in;
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

void line(Editor& editor, const Vec3& a, const Vec3& b) {
    REQUIRE(editor.commit("Line", [&](const Document& doc) { return drawEdge(doc, std::nullopt, a, b); }));
}

bool hasGuide(const SnapResult& s, GuideKind kind) {
    return std::ranges::any_of(s.guides, [kind](const SnapGuide& g) { return g.kind == kind; });
}

// An angled edge on the ground, (0,0) to (2,1): not along any axis.
const Vec3 kEdgeA{0.0, 0.0, 0.0};
const Vec3 kEdgeB{2.0, 1.0, 0.0};

} // namespace

TEST_CASE("inference: a line perpendicular to an angled edge, from the pointer alone") {
    Editor editor;
    ToolController tools(editor);
    line(editor, kEdgeA, kEdgeB);
    tools.setTool(Tool::TapeMeasure);

    // Start on the edge's end point.
    tools.pointerMove(snapAt(editor, 2.03, 1.02, {100, 100}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Endpoint);
    click(tools, snapAt(editor, 2.03, 1.02, {100, 100}));

    // Move roughly perpendicular to the edge, a little off the exact line.
    const Vec3 u = glm::normalize(kEdgeB - kEdgeA);
    const Vec3 perp{-u.y, u.x, 0.0};
    const Vec3 aim = kEdgeB + perp * 1.5 + u * 0.08;
    tools.pointerMove(snapAt(editor, aim.x, aim.y, {200, 40}));
    const auto& snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->kind == SnapKind::Perpendicular);
    CHECK(snapLabel(*snap) == "Perpendicular to Edge");
    CHECK(tools.hint().starts_with("[Perpendicular to Edge]"));
    // Exactly perpendicular: no component along the edge.
    CHECK(std::abs(glm::dot(snap->point - kEdgeB, u)) < 1e-12);
    CHECK(test::near(snap->point.z, 0.0, 1e-12));
    // The magenta guide from the start point and the reference edge highlighted.
    CHECK(hasGuide(*snap, GuideKind::Parallel));
    CHECK(hasGuide(*snap, GuideKind::Reference));
    REQUIRE(snap->edge);
    CHECK(snap->edge->direction() == u);

    // Clicking there measures along the perpendicular.
    click(tools, snapAt(editor, aim.x, aim.y, {200, 40}));
    const auto* m = std::get_if<MeasurePreview>(&editor.preview());
    REQUIRE(m);
    REQUIRE(m->end);
    CHECK(std::abs(glm::dot(*m->end - kEdgeB, u)) < 1e-12);
}

TEST_CASE("inference: parallel to an edge the pointer visited, and on edge") {
    Editor editor;
    ToolController tools(editor);
    line(editor, kEdgeA, kEdgeB);
    tools.setTool(Tool::TapeMeasure);
    const Vec3 u = glm::normalize(kEdgeB - kEdgeA);

    // Hover the edge away from its ends and middle: On Edge, exactly on it.
    const Vec3 onEdge = kEdgeA + u * 0.5 + Vec3{-u.y, u.x, 0.0} * 0.05;
    tools.pointerMove(snapAt(editor, onEdge.x, onEdge.y, {50, 50}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::OnEdge);
    CHECK(snapLabel(*tools.snapIndicator()) == "On Edge");
    CHECK(glm::length(glm::cross(tools.snapIndicator()->point - kEdgeA, u)) < 1e-12);

    // Start somewhere else on the ground and move along the edge's direction.
    const Vec3 start{4.0, -3.0, 0.0};
    click(tools, snapAt(editor, start.x, start.y, {300, 300}));
    const Vec3 aim = start + u * 2.0 + Vec3{-u.y, u.x, 0.0} * 0.1;
    tools.pointerMove(snapAt(editor, aim.x, aim.y, {400, 250}));
    const auto& snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->kind == SnapKind::Parallel);
    CHECK(snapLabel(*snap) == "Parallel to Edge");
    CHECK(glm::length(glm::cross(snap->point - start, u)) < 1e-12);
    CHECK(hasGuide(*snap, GuideKind::Parallel));
}

TEST_CASE("inference: along an axis from the start point, and a From Point crossing") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false; // raw ground points, so only inference moves them
    test::makeBox(editor, tools, "1000, 1000", nullptr); // planar (0,0)-(1,1)
    tools.setTool(Tool::TapeMeasure);

    // Remember the corner (1, 1).
    tools.pointerMove(snapAt(editor, 1.02, 1.03));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Endpoint);

    const Vec3 start{-2.0, 5.0, 0.0};
    click(tools, snapAt(editor, start.x, start.y, {10, 10}));

    // Along X from the start, far from the corner: On X Axis.
    tools.pointerMove(snapAt(editor, 4.0, 5.07, {300, 10}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Axis);
    CHECK(snapLabel(*tools.snapIndicator()) == "On X Axis");
    CHECK(tools.snapIndicator()->point.y == 5.0);

    // Where the X line from the start meets the corner's Y line: both guides.
    tools.pointerMove(snapAt(editor, 1.04, 5.05, {200, 10}));
    const auto& snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->kind == SnapKind::Axis);
    CHECK(snap->point == Vec3{1.0, 5.0, 0.0});
    CHECK(hasGuide(*snap, GuideKind::Axis));
    CHECK(hasGuide(*snap, GuideKind::Point));
}

TEST_CASE("inference: an edge crossing an axis line from the start point is an intersection") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    test::makeBox(editor, tools, "2000, 3000", nullptr); // planar (0,0)-(2,3)
    tools.setTool(Tool::TapeMeasure);
    click(tools, snapAt(editor, 3.0, 0.3, {10, 10}));
    // Near the x = 2 edge, a little above where the X line from the start crosses it.
    tools.pointerMove(snapAt(editor, 2.02, 0.36, {50, 10}));
    const auto& snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->kind == SnapKind::Intersection);
    CHECK(snapLabel(*snap) == "Intersection");
    CHECK(test::near(snap->point.x, 2.0, 1e-12));
    CHECK(test::near(snap->point.y, 0.3, 1e-12));
}

TEST_CASE("inference: Shift locks the shown inference until it is released") {
    Editor editor;
    ToolController tools(editor);
    line(editor, kEdgeA, kEdgeB);
    line(editor, {6.0, 6.0, 0.0}, {7.0, 6.0, 0.0}); // a far edge, for an endpoint to project
    tools.setTool(Tool::TapeMeasure);
    click(tools, snapAt(editor, 2.02, 1.01));
    const Vec3 u = glm::normalize(kEdgeB - kEdgeA);
    const Vec3 perp{-u.y, u.x, 0.0};
    const Vec3 aim = kEdgeB + perp * 1.5 + u * 0.05;
    tools.pointerMove(snapAt(editor, aim.x, aim.y, {100, 50}));
    REQUIRE(tools.snapIndicator());
    REQUIRE(tools.snapIndicator()->kind == SnapKind::Perpendicular);

    // Hold Shift and move well off the line: the point stays on it.
    const Vec3 off = kEdgeB + perp * 3.0 + u * 2.0;
    tools.pointerMove(snapAt(editor, off.x, off.y, {150, 20}, true));
    auto snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->locked);
    CHECK(snap->kind == SnapKind::Perpendicular);
    CHECK(snapLabel(*snap) == "Perpendicular to Edge (locked)");
    CHECK(std::abs(glm::dot(snap->point - kEdgeB, u)) < 1e-9);

    // Still locked over another edge's end point: its projection onto the line.
    tools.pointerMove(snapAt(editor, 7.02, 6.01, {300, 10}, true));
    snap = tools.snapIndicator();
    REQUIRE(snap);
    CHECK(snap->locked);
    const Vec3 expected = kEdgeB + perp * glm::dot(Vec3{7.0, 6.0, 0.0} - kEdgeB, perp);
    CHECK(glm::length(snap->point - expected) < 1e-9);
    CHECK(hasGuide(*snap, GuideKind::Point));

    // Released: the end point itself.
    tools.pointerMove(snapAt(editor, 7.02, 6.01, {300, 10}, false));
    REQUIRE(tools.snapIndicator());
    CHECK_FALSE(tools.snapIndicator()->locked);
    CHECK(tools.snapIndicator()->kind == SnapKind::Endpoint);
    CHECK(tools.snapIndicator()->point == Vec3{7.0, 6.0, 0.0});
}

TEST_CASE("inference: Shift on a face locks its plane") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "2000, 2000", "500");
    tools.setTool(Tool::TapeMeasure);
    tools.pointerMove(snapAt(editor, 0.7, 0.6));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::OnFace);
    CHECK(snapLabel(*tools.snapIndicator()) == "On Face");
    // Off the box with Shift held: still on the cap's plane, not the ground.
    tools.pointerMove(snapAt(editor, 6.0, 6.0, {50, 50}, true));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->locked);
    CHECK(test::near(tools.snapIndicator()->point.z, 0.5, 1e-12));
}

TEST_CASE("inference: lines drawn into a mesh group snap at their ends, middles and along them") {
    Editor editor;
    ToolController tools(editor);
    line(editor, {1.0, 1.0, 0.0}, {3.0, 1.0, 0.0});
    const auto f = snapFeatures(*editor.document().objects[0]);
    CHECK(f.vertices.size() == 2);
    CHECK(f.edges.size() == 1);
    CHECK(f.midpoint(0) == Vec3{2.0, 1.0, 0.0});
    tools.setTool(Tool::TapeMeasure);
    tools.pointerMove(snapAt(editor, 2.02, 1.03));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Midpoint);
    tools.pointerMove(snapAt(editor, 2.6, 1.04));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::OnEdge);
    CHECK(tools.snapIndicator()->point == Vec3{2.6, 1.0, 0.0});
}

TEST_CASE("inference: the center of a true circle snaps") {
    // A 0.3 m circle of 12 segments around (5, 5), kept as a mesh curve.
    auto m = std::make_shared<mesh::Mesh>();
    constexpr int n = 12;
    const Vec3 c{5.0, 5.0, 0.0};
    std::vector<mesh::VertexId> vs;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n;
        vs.push_back(m->appendVertex(c + Vec3{std::cos(a), std::sin(a), 0.0} * 0.3));
    }
    mesh::Curve curve;
    curve.kind = mesh::CurveKind::Circle;
    curve.center = c;
    curve.radius = 0.3;
    curve.sweep = 2.0 * std::numbers::pi;
    curve.segments = n;
    for (int i = 0; i < n; ++i) curve.edges.push_back(m->appendEdge(vs[std::size_t(i)], vs[std::size_t((i + 1) % n)]));
    m->insertCurve(mesh::CurveId{1}, curve);
    m->appendFace({vs}, {0.0, 0.0, 1.0});

    Document doc;
    auto o = std::make_shared<ProfileObject>();
    o->id = ObjectId{1};
    o->mesh = m;
    doc.objects.push_back(o);
    doc.nextId = 2;

    const auto f = snapFeatures(*o);
    REQUIRE(f.curveCenters.size() == 1);
    CHECK(f.curveCenters[0] == c);
    CHECK(*resolveSnapFeature(doc, o->id, SnapKind::Center, 0) == c);

    SnapIndex index;
    index.sync(doc);
    const double r = kPixelAngle * 12.0;
    const auto hit = index.nearest(Ray{{5.05, 4.97, 50.0}, {0.0, 0.0, -1.0}}, r);
    REQUIRE(hit);
    CHECK(hit->kind == SnapKind::Center);
    CHECK(hit->point == c);
    CHECK(snapLabel(*hit) == "Center");
    // A corner of the circle still wins when the pointer is on it.
    const auto corner = index.nearest(Ray{{5.31, 5.01, 50.0}, {0.0, 0.0, -1.0}}, r);
    REQUIRE(corner);
    CHECK(corner->kind == SnapKind::Endpoint);
}

TEST_CASE("inference engine: drawing plane, edges through the start, nothing far away") {
    Editor editor;
    ToolController tools(editor);
    line(editor, kEdgeA, kEdgeB);
    SnapIndex index;
    index.sync(editor.document());
    const auto down = [](double x, double y) { return Ray{{x, y, 50.0}, {0.0, 0.0, -1.0}}; };
    const Vec3 u = glm::normalize(kEdgeB - kEdgeA);

    InferenceQuery q;
    q.radius = kPixelAngle * 12.0;
    q.from = kEdgeA; // the edge passes through the start: it is a reference without visiting it
    q.plane = InferencePlane{{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    const Vec3 perp{-u.y, u.x, 0.0};
    const Vec3 aim = perp * -2.0 + u * 0.1;
    q.ray = down(aim.x, aim.y);
    auto hit = inferPoint(index, q);
    REQUIRE(hit);
    CHECK(hit->kind == SnapKind::Perpendicular);
    CHECK(std::abs(glm::dot(hit->point, u)) < 1e-12);
    REQUIRE(hit->constraint);
    CHECK(hit->constraint->type == SnapConstraint::Type::Line);

    // The vertical axis is not in the ground plane: never offered there.
    q.ray = Ray{{-5.0, 0.01, 0.5}, {1.0, 0.0, 0.0}};
    hit = inferPoint(index, q);
    CHECK((!hit || hit->kind != SnapKind::Axis || hit->constraint->axis != 2));

    // Far from everything: nothing (the caller uses the face or the grid).
    q.ray = down(-7.3, 8.9);
    CHECK_FALSE(inferPoint(index, q).has_value());

    // Inference off.
    q.radius = 0.0;
    q.ray = down(2.0, 1.0);
    CHECK_FALSE(inferPoint(index, q).has_value());
}

TEST_CASE("inference: tooltip names") {
    SnapResult s{Vec3{0.0}, SnapKind::Axis};
    s.constraint = SnapConstraint{SnapConstraint::Type::Line, Vec3{0.0}, Vec3{0.0, 1.0, 0.0}, SnapKind::Axis, 1, std::nullopt};
    CHECK(snapLabel(s) == "On Y Axis");
    s.locked = true;
    CHECK(snapLabel(s) == "On Y Axis (locked)");
    CHECK(snapLabel(SnapResult{Vec3{0.0}, SnapKind::Inference}) == "From Point");
    CHECK(snapLabel(SnapResult{Vec3{0.0}, SnapKind::OnFace}) == "On Face");
    CHECK(snapLabel(SnapResult{Vec3{0.0}, SnapKind::None}).empty());
}

TEST_CASE("inference: Move puts the picked corner exactly on another object's corner") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().step = 0.1;
    test::makeBox(editor, tools, "1000, 1000", nullptr);                            // (0,0)-(1,1)
    const ObjectId b = test::makeBox(editor, tools, "500, 500", nullptr, {3.03, 0.01}); // off the grid
    const Vec3 corner = editor.document().find(b)->transform.translation;
    tools.setTool(Tool::Move);
    // Press near B's corner: the move starts from the corner itself.
    tools.pointerDown(snapAt(editor, corner.x + 0.03, corner.y + 0.02, {300, 300}));
    tools.pointerUp(snapAt(editor, corner.x + 0.03, corner.y + 0.02, {300, 300}));
    REQUIRE(tools.active());
    tools.pointerMove(snapAt(editor, 1.03, 1.02, {100, 100}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Endpoint);
    click(tools, snapAt(editor, 1.03, 1.02, {100, 100}));
    CHECK_FALSE(tools.active());
    CHECK(editor.document().find(b)->transform.translation == Vec3{1.0, 1.0, 0.0});
}

TEST_CASE("inference: New / Open forgets remembered edges and the lock") {
    Editor editor;
    ToolController tools(editor);
    line(editor, kEdgeA, kEdgeB);
    tools.setTool(Tool::TapeMeasure);
    const Vec3 u = glm::normalize(kEdgeB - kEdgeA);
    const Vec3 onEdge = kEdgeA + u * 0.5 + Vec3{-u.y, u.x, 0.0} * 0.05;
    tools.pointerMove(snapAt(editor, onEdge.x, onEdge.y));
    REQUIRE(tools.snapIndicator());
    REQUIRE(tools.snapIndicator()->kind == SnapKind::OnEdge);
    editor.resetDocument(Document{});
    // A start point and a pointer along the old edge's direction: no parallel inference.
    const Vec3 start{4.0, -3.0, 0.0};
    click(tools, snapAt(editor, start.x, start.y, {300, 300}));
    const Vec3 aim = start + u * 2.0 + Vec3{-u.y, u.x, 0.0} * 0.1;
    tools.pointerMove(snapAt(editor, aim.x, aim.y, {400, 250}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind != SnapKind::Parallel);
    CHECK_FALSE(tools.snapIndicator()->edge.has_value());
}

TEST_CASE("inference: Move never snaps to the moving object's own corners on the ground") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000, 1000", "1000");
    tools.setTool(Tool::Move);
    // Grab the top corner (1,1,1) and hover the opposite top corner (0,0,1).
    tools.pointerDown(snapAt(editor, 0.98, 0.97, {300, 300}));
    tools.pointerUp(snapAt(editor, 0.98, 0.97, {300, 300}));
    REQUIRE(tools.active());
    tools.pointerMove(snapAt(editor, 0.03, 0.02, {200, 200}));
    const auto* preview = std::get_if<MovePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->offset.z == 0.0); // the free move keeps the height
    if (tools.snapIndicator()) CHECK(tools.snapIndicator()->objectId != std::optional<ObjectId>(box));
}

TEST_CASE("inference: the Line tool draws perpendicular to an angled edge with no typing (item 7 acceptance)") {
    Editor editor;
    ToolController tools(editor);
    line(editor, kEdgeA, kEdgeB);
    tools.setTool(Tool::Line);
    // Start on the edge's end, then click roughly perpendicular to the edge.
    click(tools, snapAt(editor, 2.02, 1.01, {100, 100}));
    REQUIRE(tools.active());
    const Vec3 u = glm::normalize(kEdgeB - kEdgeA);
    const Vec3 perp{-u.y, u.x, 0.0};
    const Vec3 aim = kEdgeB + perp * 1.5 + u * 0.08;
    tools.pointerMove(snapAt(editor, aim.x, aim.y, {200, 40}));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Perpendicular);
    click(tools, snapAt(editor, aim.x, aim.y, {200, 40}));
    // The new edge runs from the end of the angled edge, exactly perpendicular to it.
    const auto& group = *editor.document().objects[0];
    REQUIRE(group.mesh);
    bool found = false;
    for (const auto& [id, e] : group.mesh->edges()) {
        const Vec3 a = localToWorld(group, group.mesh->vertex(e.v0)->position);
        const Vec3 b = localToWorld(group, group.mesh->vertex(e.v1)->position);
        const Vec3 d = b - a;
        if (glm::length(d) > 1.0 && std::abs(glm::dot(glm::normalize(d), u)) < 1e-12) found = true;
    }
    CHECK(found);
}
