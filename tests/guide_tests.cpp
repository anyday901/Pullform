// Construction guides: the tape measure's guide lines and guide points, the
// protractor, snapping to guides, hiding and deleting them, and the project file.
#include "helpers.hpp"

#include "app/tools/protractor_tool.hpp"
#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/guide_snap.hpp"
#include "core/geometry/topology.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>

using namespace pf;

namespace {

constexpr double kPixelAngle = 1e-4; // 12 px snap radius = 0.06 m from 50 m above

bool nearVec(const Vec3& a, const Vec3& b, double eps = 1e-9) { return glm::length(a - b) <= eps; }

/// Looking straight down at (x, y) with point snapping on.
PointerInput snapDown(const Editor& editor, double x, double y, Vec2 screen = {100, 100}) {
    PointerInput in = test::downAt(editor, x, y, screen);
    in.pixelAngle = kPixelAngle;
    return in;
}

/// The pointer on the edge a-b of `id` at `at`, as the viewport's element
/// picker reports it.
PointerInput onEdge(const Editor& editor, ObjectId id, const Vec3& a, const Vec3& b, const Vec3& at, Vec2 screen = {100, 100}) {
    PointerInput in = test::downAt(editor, at.x, at.y, screen);
    const auto topo = buildTopology(*editor.document().find(id));
    for (const auto& e : topo.edges) {
        const Vec3 pa = topo.vertices[e.a].position;
        const Vec3 pb = topo.vertices[e.b].position;
        if ((nearVec(pa, a) && nearVec(pb, b)) || (nearVec(pa, b) && nearVec(pb, a))) {
            in.element = ElementHit{ElementRef{id, ElementKind::Edge, e.id}, at, 50.0 - at.z};
            return in;
        }
    }
    FAIL("no such edge");
    return in;
}

void click(ToolController& tools, const PointerInput& in) {
    tools.pointerDown(in);
    tools.pointerUp(in);
}

/// A wall 3000 x 200 x 2400 mm along X at the origin, and the guide 450 mm in
/// from its inner (y = 0.2) bottom edge, made with the tape measure.
struct Room {
    Editor editor;
    ToolController tools{editor};
    ObjectId wall;
    Room() {
        wall = test::makeBox(editor, tools, "3000, 200", "2400");
        tools.setTool(Tool::TapeMeasure);
        click(tools, onEdge(editor, wall, {0, 0.2, 0}, {3, 0.2, 0}, {1.5, 0.2, 0}));
        tools.pointerMove(test::downAt(editor, 1.5, 1.0, {100, 140}));
        const auto* m = std::get_if<MeasurePreview>(&editor.preview());
        REQUIRE(m);
        REQUIRE(m->guide); // the parallel guide follows the pointer
        CHECK(m->guide->kind == Guide::Kind::Line);
        REQUIRE_FALSE(tools.submitMeasure("450").has_value());
        REQUIRE(editor.document().guides.size() == 1);
    }
};

} // namespace

TEST_CASE("tape measure: a guide 450 mm in from a wall, and a rectangle corner snaps to it") {
    Room room;
    Editor& editor = room.editor;
    ToolController& tools = room.tools;
    const Guide& g = editor.document().guides.front();
    CHECK(g.kind == Guide::Kind::Line);
    CHECK(test::near(g.point.y, 0.65));
    CHECK(test::near(g.point.z, 0.0));
    CHECK(test::near(std::abs(g.direction.x), 1.0));
    CHECK(idString(g.id).starts_with("guide-"));
    CHECK(editor.undoLabel().find("Add guide 450 mm") != std::string::npos);
    CHECK_FALSE(tools.active()); // the tape is ready for the next guide

    // Rectangle: the first corner, put down 10 mm off the guide, lands on it.
    tools.setTool(Tool::Rectangle);
    tools.pointerMove(snapDown(editor, 1.0, 0.66));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::OnGuide);
    click(tools, snapDown(editor, 1.0, 0.66, {100, 100}));
    click(tools, snapDown(editor, 2.03, 1.53, {300, 300}));
    const auto* rect = editor.document().find(editor.selection()->objectId);
    REQUIRE(rect);
    CHECK(test::near(rect->transform.translation.y, 0.65));
    CHECK(test::near(rect->transform.translation.x, 1.0)); // grid snap along the guide

    // Undo takes the rectangle, then the guide.
    REQUIRE(editor.undo());
    REQUIRE(editor.undo());
    CHECK(editor.document().guides.empty());
}

TEST_CASE("tape measure: clicking the parallel guide directly, and measuring along the edge") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId wall = test::makeBox(editor, tools, "3000, 200", nullptr); // a flat strip: its edges face up
    tools.setTool(Tool::TapeMeasure);
    const auto depth = editor.undoDepth();
    click(tools, onEdge(editor, wall, {0, 0.2, 0}, {3, 0.2, 0}, {1.0, 0.2, 0}));
    // Second click 1 m away: a guide through it, parallel to the edge.
    click(tools, test::downAt(editor, 2.0, 1.2, {200, 200}));
    REQUIRE(editor.document().guides.size() == 1);
    CHECK(test::near(editor.document().guides[0].point.y, 1.2));
    CHECK(editor.undoDepth() == depth + 1);

    // From the guide line itself: another parallel guide (here typed, -300 flips the side).
    tools.pointerMove(snapDown(editor, 0.5, 1.21));
    REQUIRE(tools.snapIndicator());
    REQUIRE(tools.snapIndicator()->kind == SnapKind::OnGuide);
    click(tools, snapDown(editor, 0.5, 1.21));
    tools.pointerMove(test::downAt(editor, 0.5, 2.0, {150, 150}));
    REQUIRE_FALSE(tools.submitMeasure("-300").has_value());
    REQUIRE(editor.document().guides.size() == 2);
    CHECK(test::near(editor.document().guides[1].point.y, 0.9));

    // A second point on the start line itself only measures.
    click(tools, onEdge(editor, wall, {0, 0.2, 0}, {3, 0.2, 0}, {1.0, 0.2, 0}));
    PointerInput along = onEdge(editor, wall, {0, 0.2, 0}, {3, 0.2, 0}, {2.5, 0.2, 0}, {300, 100});
    along.pixelAngle = kPixelAngle; // On Edge
    click(tools, along);
    CHECK(editor.document().guides.size() == 2);
}

TEST_CASE("tape measure: guide points from a point, and Ctrl turns guides off") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId box = test::makeBox(editor, tools, "1000, 1000", nullptr);
    tools.setTool(Tool::TapeMeasure);

    // From the corner to a free ground point: a guide point there.
    click(tools, snapDown(editor, 0.0, 0.0));
    click(tools, test::downAt(editor, -0.5, -0.5, {50, 50}));
    REQUIRE(editor.document().guides.size() == 1);
    CHECK(editor.document().guides[0].kind == Guide::Kind::Point);
    CHECK(nearVec(editor.document().guides[0].point, {-0.5, -0.5, 0.0}));

    // Typed: 1000 mm from the corner toward the pointer.
    click(tools, snapDown(editor, 1.0, 0.0));
    tools.pointerMove(test::downAt(editor, 2.0, -1.0, {150, 150}));
    REQUIRE_FALSE(tools.submitMeasure("1000").has_value());
    REQUIRE(editor.document().guides.size() == 2);
    const double h = std::sqrt(0.5);
    CHECK(nearVec(editor.document().guides[1].point, {1.0 + h, -h, 0.0}, 1e-12));

    // The guide point snaps like a corner.
    tools.pointerMove(snapDown(editor, -0.51, -0.49));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::GuidePoint);
    CHECK(nearVec(tools.snapIndicator()->point, {-0.5, -0.5, 0.0}));

    // Corner to corner adds nothing: there is a point there already.
    click(tools, snapDown(editor, 0.0, 0.0));
    click(tools, snapDown(editor, 1.0, 1.0, {200, 200}));
    CHECK(editor.document().guides.size() == 2);

    // Guides off: the tape only measures, from an edge too, and refuses typed values.
    CHECK(tools.tapeGuides());
    tools.toggleTapeGuides();
    CHECK_FALSE(tools.tapeGuides());
    CHECK(tools.hint().find("Measuring only") != std::string::npos);
    click(tools, onEdge(editor, box, {0, 0, 0}, {1, 0, 0}, {0.5, 0, 0}));
    click(tools, test::downAt(editor, 0.5, -1.0, {100, 300}));
    CHECK(editor.document().guides.size() == 2);
    click(tools, test::downAt(editor, 0.5, -1.0, {100, 300}));
    CHECK(tools.submitMeasure("100").has_value());
    tools.setTool(Tool::Select);
    tools.setTool(Tool::TapeMeasure);
    CHECK_FALSE(tools.tapeGuides()); // the setting stays across tool changes
}

TEST_CASE("guides: crossings snap, hidden guides do not, Delete Guides is one undo entry") {
    Room room;
    Editor& editor = room.editor;
    ToolController& tools = room.tools;
    // A second guide across the first, along Y through x = 1.
    REQUIRE(editor.commit("g", [](const Document& d) { return addGuide(d, Guide::line({1.0, 5.0, 0.0}, {0.0, 2.0, 0.0})); }));
    CHECK(test::near(editor.document().guides[1].direction.y, 1.0)); // normalized

    tools.setTool(Tool::Rectangle);
    tools.pointerMove(snapDown(editor, 1.01, 0.66));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Intersection);
    CHECK(nearVec(tools.snapIndicator()->point, {1.0, 0.65, 0.0}, 1e-9));
    // Where a guide crosses the wall's edge.
    tools.pointerMove(snapDown(editor, 1.01, 0.21));
    REQUIRE(tools.snapIndicator());
    CHECK(tools.snapIndicator()->kind == SnapKind::Intersection);
    // The guide (z = 0) crosses the wall's bottom edge; the rectangle uses the ground point.
    CHECK(nearVec(tools.snapIndicator()->point, {1.0, 0.2, 0.0}, 1e-9));

    // Hidden (View > Guides off): no guide snapping.
    tools.snap().guides = false;
    tools.pointerMove(snapDown(editor, 1.01, 0.66));
    const bool unsnapped = !tools.snapIndicator() || tools.snapIndicator()->kind < SnapKind::OnGuide;
    CHECK(unsnapped);
    tools.snap().guides = true;

    const auto depth = editor.undoDepth();
    CHECK_FALSE(tools.deleteGuides().has_value());
    CHECK(editor.document().guides.empty());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(tools.deleteGuides().has_value()); // nothing left: no empty undo entry
    CHECK(editor.undoDepth() == depth + 1);
    REQUIRE(editor.undo());
    CHECK(editor.document().guides.size() == 2);
}

TEST_CASE("protractor: an angled guide through the centre, in 15 degree steps or typed") {
    Editor editor;
    ToolController tools(editor);
    tools.setTool(Tool::Protractor);
    CHECK(toolName(Tool::Protractor) == "Protractor");
    click(tools, test::downAt(editor, 0.0, 0.0));
    CHECK(tools.active());
    click(tools, test::downAt(editor, 2.0, 0.0, {200, 100}));
    // 31 degrees on screen; grid snap rounds it to 30.
    const double a = 31.0 * std::numbers::pi / 180.0;
    tools.pointerMove(test::downAt(editor, 2.0 * std::cos(a), 2.0 * std::sin(a), {180, 60}));
    const auto* p = std::get_if<ProtractorPreview>(&editor.preview());
    REQUIRE(p);
    REQUIRE(p->guide);
    CHECK(test::near(p->angle, std::numbers::pi / 6.0, 1e-12));
    click(tools, test::downAt(editor, 2.0 * std::cos(a), 2.0 * std::sin(a), {180, 60}));
    REQUIRE(editor.document().guides.size() == 1);
    const Guide& g = editor.document().guides[0];
    CHECK(nearVec(g.point, {0, 0, 0}));
    CHECK(nearVec(g.direction, {std::cos(std::numbers::pi / 6.0), std::sin(std::numbers::pi / 6.0), 0.0}, 1e-12));
    CHECK_FALSE(tools.active());

    // Typed: the pointer is below the baseline, so 45 turns clockwise.
    click(tools, test::downAt(editor, 1.0, 1.0));
    click(tools, test::downAt(editor, 3.0, 1.0, {200, 100}));
    tools.pointerMove(test::downAt(editor, 2.0, 0.0, {150, 150}));
    REQUIRE_FALSE(tools.submit("45").has_value());
    REQUIRE(editor.document().guides.size() == 2);
    const double h = std::sqrt(0.5);
    CHECK(nearVec(editor.document().guides[1].direction, {h, -h, 0.0}, 1e-12));
    CHECK(nearVec(editor.document().guides[1].point, {1, 1, 0}));

    // Right arrow: the protractor stands square to X (in the YZ plane).
    CHECK(tools.key(ToolKey::ArrowRight));
    click(tools, test::downAt(editor, 0.0, 0.0));
    const auto* q = std::get_if<ProtractorPreview>(&editor.preview());
    REQUIRE(q);
    CHECK(nearVec(q->normal, {1, 0, 0}));
    CHECK(tools.submit("30").has_value()); // no baseline yet
    CHECK(tools.cancel());
    CHECK(editor.document().guides.size() == 2);
}

TEST_CASE("protractor: the plane follows the face under the pointer") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", "1000");
    tools.setTool(Tool::Protractor);
    // A ray from -Y onto the box's front side (y = 0).
    PointerInput in;
    in.ray = Ray{{0.5, -10.0, 0.5}, {0.0, 1.0, 0.0}};
    in.pick = pickDocument(in.ray, editor.document());
    REQUIRE(in.pick);
    click(tools, in);
    const auto* p = std::get_if<ProtractorPreview>(&editor.preview());
    REQUIRE(p);
    CHECK(nearVec(p->normal, {0, -1, 0}));
    CHECK(protractorDirection({1, 0, 0}, {0, -1, 0}, std::numbers::pi / 2).z == doctest::Approx(1.0));
}

TEST_CASE("guides save and reopen exactly; broken guides are rejected") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "1000, 1000", nullptr);
    CHECK(serializeProject(editor.document(), std::nullopt).find("guides") == std::string::npos);
    REQUIRE(editor.commit("g", [](const Document& d) { return addGuide(d, Guide::line({0.1, 0.65, 0.0}, {1, 0, 0})); }));
    REQUIRE(editor.commit("g", [](const Document& d) { return addGuide(d, Guide::at({-0.5, 0.25, 1.125})); }));
    const std::string text = serializeProject(editor.document(), std::nullopt);
    CHECK(text.find("\"guides\"") != std::string::npos);
    CHECK(text.find("\"kind\": \"point\"") != std::string::npos);
    const auto loaded = parseProject(text);
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
    CHECK(sameContent(loaded->document, editor.document()));
    CHECK(serializeProject(loaded->document, std::nullopt) == text);

    const auto broken = [&](const std::string& from, const std::string& to) {
        std::string t = text;
        const auto pos = t.find(from);
        REQUIRE(pos != std::string::npos);
        t.replace(pos, from.size(), to);
        const auto r = parseProject(t);
        REQUIRE_FALSE(r.ok());
        return r.error();
    };
    CHECK(broken("\"guide-2\"", "\"guide-x\"").find("document.guides[0].id") != std::string::npos);
    CHECK(broken("\"kind\": \"line\"", "\"kind\": \"ray\"").find("document.guides[0].kind") != std::string::npos);
    CHECK(broken("\"guide-2\"", "\"guide-1\"").find("already used") != std::string::npos); // obj-1 has that number
    CHECK(broken("\"guide-3\"", "\"guide-99\"").find("nextId") != std::string::npos);
    {
        std::string t = text;
        const auto at = t.find("1.0", t.find("\"direction\""));
        REQUIRE(at != std::string::npos);
        t.replace(at, 3, "2.0");
        const auto r = parseProject(t);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().find("unit vector") != std::string::npos);
    }

    // Out of range or directionless guides are refused before they enter the document.
    CHECK_FALSE(addGuide(editor.document(), Guide::at({2e4, 0, 0})).ok());
    CHECK_FALSE(addGuide(editor.document(), Guide::line({0, 0, 0}, {0, 0, 0})).ok());
    CHECK_FALSE(deleteGuides(Document{}).ok());
}

TEST_CASE("guide snapping: closest points of lines and the guide candidates") {
    const auto st = closestLineParameters({0, 0, 0}, {1, 0, 0}, {2, -1, 1}, {0, 1, 0});
    REQUIRE(st);
    CHECK(test::near(st->first, 2.0));
    CHECK(test::near(st->second, 1.0));
    CHECK_FALSE(closestLineParameters({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}));

    const std::vector<Guide> guides{Guide::line({0, 1, 0}, {1, 0, 0}), Guide::at({5, 5, 0})};
    const Ray down{{3.0, 1.02, 10.0}, {0, 0, -1}};
    const auto on = nearestOnGuide(guides, down, 0.005);
    REQUIRE(on);
    CHECK(on->kind == SnapKind::OnGuide);
    CHECK(on->feature == 0);
    CHECK(nearVec(on->point, {3.0, 1.0, 0.0}));
    CHECK_FALSE(nearestOnGuide(guides, down, 0.001)); // 2 cm off at 10 m is 0.002 rad
    // Hidden behind a face 5 m along the ray.
    CHECK_FALSE(nearestOnGuide(guides, down, 0.005, 5.0));

    SnapIndex none;
    const auto pt = nearestGuidePoint(guides, none, Ray{{5.01, 5.0, 10.0}, {0, 0, -1}}, 0.005);
    REQUIRE(pt);
    CHECK(pt->snap.kind == SnapKind::GuidePoint);
    CHECK(pt->snap.feature == 1);
}

TEST_CASE("guide ids share the document allocator") {
    Document d;
    auto a = addGuide(d, Guide::at({0, 0, 0}));
    REQUIRE(a.ok());
    CHECK(a->document.guides[0].id.value == 1);
    CHECK(a->document.nextId == 2);
    CHECK(parseGuideId("guide-7")->value == 7);
    CHECK_FALSE(parseGuideId("obj-7"));
    Document bad = a->document;
    bad.guides[0].direction = {0, 0, 0}; // ignored for a point
    CHECK_FALSE(validateDocument(bad).has_value());
    bad.guides.push_back(bad.guides[0]);
    CHECK(validateDocument(bad).has_value()); // duplicate id
}
