// Rotate and scale: core operations, the Rotate/Scale tools, and rotated
// objects through save/reopen (file format version 2) and picking.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>

using namespace pf;
using pf::test::downAt;
using pf::test::near;

namespace {

constexpr double kPi = std::numbers::pi;

double degrees(double d) { return d * kPi / 180.0; }

/// A document with one w x d x h box at `corner` (h = 0: planar rectangle).
std::pair<Document, ObjectId> boxDoc(double w, double d, double h, Vec3 corner = {0, 0, 0}) {
    Document doc;
    auto created = createRectangle(doc, RectanglePlacement{corner, w, d});
    REQUIRE(created.ok());
    const ObjectId id = *created->objectId;
    if (h <= 0.0) return {created->document, id};
    auto extruded = extrudeProfile(created->document, id, h);
    REQUIRE(extruded.ok());
    return {extruded->document, id};
}

/// World corners of the object's base, in boundary order.
std::vector<Vec3> baseCorners(const ProfileObject& o) {
    std::vector<Vec3> out;
    for (const auto& p : o.profile.boundary) out.push_back(profileToWorld(o, p, 0.0));
    return out;
}

/// A ray looking straight down at (x, y), as the pointer would on the ground.
PointerInput groundAt(const Editor& editor, double x, double y, Vec2 screen = {0, 0}) { return downAt(editor, x, y, screen); }

} // namespace

TEST_CASE("a quarter turn about a grid point keeps corners exact and the solid closed") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5);
    const auto r = rotateObject(doc, id, {0, 0, 0}, {0, 0, 1}, degrees(90));
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error()));
    const auto& o = *r->document.find(id);
    CHECK(o.id == id);
    CHECK(o.profile.boundary == doc.find(id)->profile.boundary); // the shape is unchanged
    const auto c = baseCorners(o);
    // (0,0) (2,0) (2,1) (0,1) turned 90 degrees counter-clockwise about the origin.
    CHECK(c[0] == Vec3{0, 0, 0});
    CHECK(c[1] == Vec3{0, 2, 0});
    CHECK(c[2] == Vec3{-1, 2, 0});
    CHECK(c[3] == Vec3{-1, 0, 0});
    const auto check = checkSolid(buildMesh(o));
    CHECK_MESSAGE(check.ok(), check.problem);
    CHECK(check.signedVolume == doctest::Approx(1.0));
    // Four quarter turns bring it back.
    Document d = r->document;
    for (int i = 0; i < 3; ++i) d = rotateObject(d, id, {0, 0, 0}, {0, 0, 1}, degrees(90))->document;
    const auto back = baseCorners(*d.find(id));
    CHECK(back[2] == Vec3{2, 1, 0});
}

TEST_CASE("rotation about a pivot moves the object around it") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5, {4, 0, 0});
    // Half a turn about (3, 0): the box from x 4..6 lands on x 0..2, y -1..0.
    const auto r = rotateObject(doc, id, {3, 0, 0}, {0, 0, 1}, degrees(180));
    REQUIRE(r.ok());
    const Bounds b = objectBounds(*r->document.find(id));
    CHECK(b.min.x == doctest::Approx(0.0));
    CHECK(b.max.x == doctest::Approx(2.0));
    CHECK(b.min.y == doctest::Approx(-1.0));
    CHECK(b.max.y == doctest::Approx(0.0));
    CHECK(b.max.z == doctest::Approx(0.5));
}

TEST_CASE("an object tipped over about X stays a valid outward solid") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5);
    const auto r = rotateObject(doc, id, {0, 0, 0}, {1, 0, 0}, degrees(90));
    REQUIRE(r.ok());
    const auto& o = *r->document.find(id);
    CHECK(checkSolid(buildMesh(o)).ok());
    // The profile normal (+Z) now points along -Y, so the cap faces -Y.
    const Vec3 n = profileNormalWorld(o);
    CHECK(n == Vec3{0, -1, 0});
    for (const auto& f : buildFaces(o)) {
        if (f.id == FaceId::cap()) CHECK(f.normal == Vec3{0, -1, 0});
    }
    CHECK_FALSE(validateObject(o, "o").has_value());
}

TEST_CASE("invalid rotations are rejected") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    CHECK_FALSE(rotateObject(doc, id, {0, 0, 0}, {0, 0, 0}, 1.0).ok());
    CHECK_FALSE(rotateObject(doc, id, {0, 0, 0}, {0, 0, 1}, std::nan("")).ok());
    CHECK_FALSE(rotateObject(doc, ObjectId{99}, {0, 0, 0}, {0, 0, 1}, 1.0).ok());
    // Huge but finite angles are reduced, not cast out of range; whole turns are exact.
    const auto huge = rotateObject(doc, id, {5, 0, 0}, {0, 0, 1}, degrees(1e22));
    REQUIRE(huge.ok());
    CHECK(checkSolid(buildMesh(*huge->document.find(id))).ok());
    const auto turns = rotateObject(doc, id, {5, 0, 0}, {0, 0, 1}, degrees(360.0 * 1000 + 90));
    REQUIRE(turns.ok());
    CHECK(baseCorners(*turns->document.find(id))[0] == Vec3{5, -5, 0});
    // Swinging an object about a far pivot would take it beyond 10 km.
    auto [far, farId] = boxDoc(1.0, 1.0, 1.0, {9000, 0, 0});
    const auto out = rotateObject(far, farId, {-1000, 0, 0}, {0, 0, 1}, degrees(180)); // lands at x = -11 km
    CHECK_FALSE(out.ok());
    // A non-unit quaternion never validates.
    ProfileObject bad = *doc.find(id);
    bad.transform.rotation = Quat{2.0, 0.0, 0.0, 0.0};
    CHECK(validateObject(bad, "o").has_value());
}

TEST_CASE("scale bakes into real dimensions about the base centre") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5, {1, 1, 0});
    const Vec3 centre = baseCenter(*doc.find(id));
    CHECK(centre == Vec3{2.0, 1.5, 0.0});

    const auto r = scaleObject(doc, id, Vec3{2.0});
    REQUIRE(r.ok());
    const auto& o = *r->document.find(id);
    const auto size = rectangleSize(o.profile.boundary);
    REQUIRE(size.ok());
    CHECK(size->width == 4.0);
    CHECK(size->depth == 2.0);
    CHECK(o.extrusion->height == 1.0);
    CHECK(o.transform.translation == Vec3{0.0, 0.5, 0.0});
    CHECK(baseCenter(o) == centre); // stays centred and on the ground
    CHECK(o.transform.rotation == doc.find(id)->transform.rotation);

    // Per-axis factors: width, depth, height.
    const auto r2 = scaleObject(doc, id, {1.0, 3.0, 0.5});
    REQUIRE(r2.ok());
    const auto& o2 = *r2->document.find(id);
    CHECK(rectangleSize(o2.profile.boundary)->width == 2.0);
    CHECK(rectangleSize(o2.profile.boundary)->depth == 3.0);
    CHECK(o2.extrusion->height == 0.25);

    // A planar rectangle ignores the height factor.
    auto [flat, flatId] = boxDoc(2.0, 1.0, 0.0);
    const auto r3 = scaleObject(flat, flatId, {0.5, 0.5, 7.0});
    REQUIRE(r3.ok());
    CHECK_FALSE(r3->document.find(flatId)->extrusion.has_value());
    CHECK(rectangleSize(r3->document.find(flatId)->profile.boundary)->width == 1.0);
}

TEST_CASE("scaling a rotated object scales along its own axes") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5);
    const auto turned = rotateObject(doc, id, {0, 0, 0}, {0, 0, 1}, degrees(90));
    REQUIRE(turned.ok());
    const Vec3 centre = baseCenter(*turned->document.find(id));
    const auto r = scaleObject(turned->document, id, {2.0, 1.0, 1.0}); // width only
    REQUIRE(r.ok());
    const auto& o = *r->document.find(id);
    CHECK(baseCenter(o) == centre);
    const Bounds b = objectBounds(o);
    // The width runs along world Y after the quarter turn.
    CHECK(b.size().x == doctest::Approx(1.0));
    CHECK(b.size().y == doctest::Approx(4.0));
    CHECK(checkSolid(buildMesh(o)).ok());
}

TEST_CASE("invalid scales are rejected") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    CHECK_FALSE(scaleObject(doc, id, Vec3{0.0}).ok());
    CHECK(scaleObject(doc, id, {-1.0, 1.0, 1.0}).ok()); // mirroring is allowed (item 15)
    CHECK_FALSE(scaleObject(doc, id, Vec3{std::nan("")}).ok());
    CHECK_FALSE(scaleObject(doc, id, Vec3{1e-5}).ok());       // below 0.1 mm
    CHECK_FALSE(scaleObject(doc, id, Vec3{1e5}).ok());        // beyond 10 km
    CHECK_FALSE(scaleObject(doc, ObjectId{42}, Vec3{2.0}).ok());
}

TEST_CASE("roadmap check: a rotated object saves, reopens and picks correctly") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.75);
    const auto r = rotateObject(doc, id, {0, 0, 0}, {0, 0, 1}, degrees(30));
    REQUIRE(r.ok());
    const std::string text = serializeProject(r->document, std::nullopt);
    CHECK(text.find("\"version\": 3") != std::string::npos);
    const auto loaded = parseProject(text);
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
    CHECK(loaded->fileVersion == kProjectVersion);
    CHECK(sameContent(loaded->document, r->document)); // bit-exact round trip
    const auto& o = *loaded->document.find(id);

    // (1.6, 0.2) is inside the unrotated footprint but outside the rotated one;
    // (0.2, 1.2) is the reverse.
    CHECK_FALSE(pickDocument(Ray{{1.6, 0.2, 5.0}, {0, 0, -1}}, loaded->document).has_value());
    const auto hit = pickDocument(Ray{{0.2, 1.2, 5.0}, {0, 0, -1}}, loaded->document);
    REQUIRE(hit);
    CHECK(hit->objectId == id);
    CHECK(hit->faceId == FaceId::cap());
    CHECK(hit->point.z == doctest::Approx(0.75));
    // Side 0 (from boundary edge 0, along the rotated width) is hit from outside.
    const Vec3 edgeMid = (profileToWorld(o, {1.0, 0.0}, 0.3));
    const Vec3 outward = glm::normalize(rotationMatrix(o.transform) * Vec3{0, -1, 0});
    const auto side = pickDocument(Ray{edgeMid + outward * 5.0, -outward}, loaded->document);
    REQUIRE(side);
    CHECK(side->faceId == FaceId::sideAt(0));
}

TEST_CASE("version 1 files migrate to version 2 and save as version 2") {
    // A version 1 file; its stray "rotation" key never meant anything and is ignored.
    const char* v1 = R"({
  "format": "pullform-project", "version": 1, "generator": "Pullform 0.2.0",
  "document": { "lengthUnit": "m", "displayUnit": "cm", "nextId": 3, "objects": [
    { "id": "obj-1", "type": "profile", "name": "Box 1",
      "profile": { "kind": "rectangle",
        "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
        "boundary": [[0,0],[2,0],[2,1],[0,1]] },
      "extrusion": { "height": 0.75 }, "transform": { "translation": [1, 2, 0], "rotation": "junk" } },
    { "id": "obj-2", "type": "profile", "name": "Rectangle 2",
      "profile": { "kind": "rectangle",
        "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
        "boundary": [[0,0],[0.5,0],[0.5,0.5],[0,0.5]] },
      "extrusion": null, "transform": { "translation": [-3, 0, 0] } } ] } })";
    const auto r = parseProject(v1);
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error()));
    CHECK(r->fileVersion == 1);
    REQUIRE(r->document.objects.size() == 2);
    for (const auto& o : r->document.objects) CHECK(o->transform.rotation == Quat{1.0, 0.0, 0.0, 0.0});
    CHECK(r->document.objects[0]->transform.translation == Vec3{1, 2, 0});
    CHECK(r->document.displayUnit == LengthUnit::Centimeter);
    // Geometry is exactly what version 1 meant.
    const auto c = baseCorners(*r->document.objects[0]);
    CHECK(c[2] == Vec3{3, 3, 0});

    const auto saved = serializeProject(r->document, std::nullopt);
    CHECK(saved.find("\"version\": 3") != std::string::npos);
    const auto again = parseProject(saved);
    REQUIRE(again.ok());
    CHECK(again->fileVersion == kProjectVersion);
    CHECK(sameContent(again->document, r->document));

    // A version 1 file that is broken in other ways is still rejected after migration.
    std::string broken = v1;
    broken.replace(broken.find("\"translation\": [1, 2, 0]"), 24, "\"translation\": [1, 2]");
    CHECK_FALSE(parseProject(broken).ok());
}

TEST_CASE("hand-edited rotations close to unit length are accepted and normalized") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    std::string text = serializeProject(doc, std::nullopt);
    const auto at = text.find("\"rotation\": [");
    REQUIRE(at != std::string::npos);
    const auto end = text.find(']', at);
    text.replace(at, end - at + 1, "\"rotation\": [0, 0, 0.7071, 0.7071]");
    const auto r = parseProject(text);
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error()));
    const Quat q = r->document.find(id)->transform.rotation;
    CHECK(std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z) == doctest::Approx(1.0).epsilon(1e-15));
    CHECK(q.z == doctest::Approx(std::sqrt(0.5)));
}

TEST_CASE("angle and scale factor input") {
    CHECK(*parseAngle("90") == doctest::Approx(kPi / 2));
    CHECK(*parseAngle("-45") == doctest::Approx(-kPi / 4));
    CHECK(*parseAngle(" 22.5 deg ") == doctest::Approx(kPi / 8));
    CHECK(*parseAngle("30°") == doctest::Approx(kPi / 6));
    CHECK_FALSE(parseAngle("").ok());
    CHECK_FALSE(parseAngle("ninety").ok());
    CHECK_FALSE(parseAngle("1e999").ok());
    CHECK_FALSE(parseAngle("90mm").ok());
    CHECK(formatAngle(kPi / 2) == "90°");
    CHECK(formatAngle(-kPi / 8) == "-22.5°");

    CHECK(*parseScaleFactors("2") == std::vector<double>{2, 2, 2});
    CHECK(*parseScaleFactors("150%") == std::vector<double>{1.5, 1.5, 1.5});
    CHECK(*parseScaleFactors("2, 1; 0.5") == std::vector<double>{2, 1, 0.5});
    CHECK_FALSE(parseScaleFactors("2, 1").ok());
    CHECK(*parseScaleFactors("-1") == std::vector<double>{-1, -1, -1}); // mirrors (item 15)
    CHECK_FALSE(parseScaleFactors("0").ok());
    CHECK_FALSE(parseScaleFactors("2m").ok());
    CHECK_FALSE(parseScaleFactors("").ok());
}

TEST_CASE("Rotate tool: select, centre, reference, angle; one undo entry") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "500");
    const Document before = editor.document();
    editor.select(std::nullopt);
    const auto depth = editor.undoDepth();

    tools.setTool(Tool::Rotate);
    // With nothing selected, the first click only picks the object.
    tools.pointerDown(groundAt(editor, 1.0, 0.5));
    REQUIRE(editor.selection());
    CHECK(editor.selection()->objectId == id);
    CHECK_FALSE(tools.active());

    // Centre on the ground at the origin (grid-snapped), reference along +X.
    tools.pointerDown(groundAt(editor, 0.02, -0.03));
    CHECK(tools.active());
    tools.pointerMove(groundAt(editor, 3.0, -0.5));
    tools.pointerDown(groundAt(editor, 3.0, 0.0));
    // Swing to roughly +Y: snaps to exactly 90 degrees.
    tools.pointerMove(groundAt(editor, 0.1, 3.0));
    const auto* preview = std::get_if<RotatePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->angle == doctest::Approx(kPi / 2));
    REQUIRE(preview->result);
    CHECK(editor.undoDepth() == depth); // previews never enter history
    tools.pointerDown(groundAt(editor, 0.1, 3.0));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth + 1);
    CHECK(editor.undoLabel() == "Rotate 90°");
    const auto c = baseCorners(*editor.document().find(id));
    CHECK(near(c[1].x, 0.0));
    CHECK(near(c[1].y, 2.0));

    editor.undo();
    CHECK(sameContent(editor.document(), before));
}

TEST_CASE("Rotate tool: typed angles, axis keys and Escape") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "500");
    tools.setTool(Tool::Rotate);

    // Typed with only a selection: about the base centre and the Z axis.
    const Vec3 centre = baseCenter(*editor.document().find(id));
    REQUIRE_FALSE(tools.submitRotateAngle("180").has_value());
    CHECK(baseCenter(*editor.document().find(id)) == centre);
    CHECK(editor.undoDepth() == 3);
    CHECK(tools.submitRotateAngle("0").has_value());
    CHECK(tools.submitRotateAngle("abc").has_value());
    CHECK(editor.undoDepth() == 3);

    // Escape mid-gesture changes nothing.
    tools.pointerDown(groundAt(editor, 1.0, 0.5));
    CHECK(tools.active());
    CHECK(tools.cancel());
    CHECK(std::holds_alternative<std::monostate>(editor.preview()));
    CHECK(editor.undoDepth() == 3);

    // Axis X with a typed angle tips the box over about its base centre line.
    tools.setRotateAxis(MoveAxis::X);
    REQUIRE_FALSE(tools.submitRotateAngle("90").has_value());
    const auto& o = *editor.document().find(id);
    CHECK(near(glm::length(profileNormalWorld(o) - Vec3{0, -1, 0}), 0.0, 1e-12));
    CHECK(checkSolid(buildMesh(o)).ok());

    // Changing the axis mid-gesture keeps the centre and drops the reference.
    tools.setRotateAxis(MoveAxis::Z);
    tools.pointerDown(groundAt(editor, 5.0, 5.0));
    tools.pointerDown(groundAt(editor, 7.0, 5.0));
    tools.setRotateAxis(MoveAxis::Y);
    const auto* p = std::get_if<RotatePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(p->axis == Vec3{0, 1, 0});
    CHECK_FALSE(p->reference.has_value());
    CHECK(p->pivot == Vec3{5.0, 5.0, 0.0});
}

TEST_CASE("Rotate tool: typed angle follows the pointer's turning direction") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = false;
    const ObjectId id = test::makeBox(editor, tools, "1000, 1000", "500", {3.0, 3.0});
    tools.setTool(Tool::Rotate);
    tools.pointerDown(groundAt(editor, 0.0, 0.0));
    tools.pointerDown(groundAt(editor, 0.0, 2.0));  // reference +Y
    tools.pointerMove(groundAt(editor, 2.0, 2.0));  // turning clockwise
    const auto* p = std::get_if<RotatePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(p->angle < 0.0);
    REQUIRE_FALSE(tools.submitRotateAngle("90").has_value());
    // Clockwise quarter turn about the origin: (3,3) -> (3,-3).
    const auto c = baseCorners(*editor.document().find(id));
    CHECK(near(c[0].x, 3.0));
    CHECK(near(c[0].y, -3.0));
}

TEST_CASE("Scale tool: mouse gesture and typed factors") {
    Editor editor;
    ToolController tools(editor);
    tools.snap().enabled = true;
    const ObjectId id = test::makeBox(editor, tools, "2000, 2000", "1000");
    tools.setTool(Tool::Scale);
    const auto depth = editor.undoDepth();

    // Grip 1 m from the base centre (1, 1), drag to 2 m: factor 2.
    tools.pointerDown(groundAt(editor, 2.0, 1.0));
    CHECK(tools.active());
    tools.pointerMove(groundAt(editor, 3.02, 1.0));
    const auto* preview = std::get_if<ScalePreview>(&editor.preview());
    REQUIRE(preview);
    CHECK(preview->factor == doctest::Approx(2.0));
    CHECK(editor.undoDepth() == depth);
    tools.pointerDown(groundAt(editor, 3.02, 1.0));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth + 1);
    const auto* o = editor.document().find(id);
    CHECK(rectangleSize(o->profile.boundary)->width == doctest::Approx(4.0));
    CHECK(o->extrusion->height == doctest::Approx(2.0));
    CHECK(baseCenter(*o) == Vec3{1.0, 1.0, 0.0});

    // Typed: 50 % uniform, then width-only.
    REQUIRE_FALSE(tools.submitScaleFactors("50%").has_value());
    o = editor.document().find(id);
    CHECK(rectangleSize(o->profile.boundary)->width == doctest::Approx(2.0));
    REQUIRE_FALSE(tools.submitScaleFactors("3, 1, 1").has_value());
    o = editor.document().find(id);
    CHECK(rectangleSize(o->profile.boundary)->width == doctest::Approx(6.0));
    CHECK(rectangleSize(o->profile.boundary)->depth == doctest::Approx(2.0));
    CHECK(editor.undoDepth() == depth + 3);

    // No-ops and invalid factors add no history.
    CHECK(tools.submitScaleFactors("1").has_value());
    CHECK(tools.submitScaleFactors("0").has_value());
    CHECK(tools.submitScaleFactors("0.00001").has_value());
    CHECK(editor.undoDepth() == depth + 3);

    // A gesture that ends where it started (factor 1) is cancelled, not committed.
    tools.pointerDown(groundAt(editor, 2.0, 1.0));
    tools.pointerMove(groundAt(editor, 2.0, 1.0));
    tools.pointerDown(groundAt(editor, 2.0, 1.0));
    CHECK_FALSE(tools.active());
    CHECK(editor.undoDepth() == depth + 3);
}

TEST_CASE("push/pull follows a rotated object's normal") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId id = test::makeBox(editor, tools, "2000, 1000", "500");
    // Tip the box onto its side about the line y = 0: the cap now faces -Y at y = -0.5.
    REQUIRE(editor.commit("tip", [&](const Document& d) { return rotateObject(d, id, {0, 0, 0}, {1, 0, 0}, degrees(90)); }));
    tools.setTool(Tool::PushPull);
    // Click the cap from the -Y side, drag along -Y to y = -1.2: height 1.2.
    PointerInput press;
    press.ray = Ray{{1.0, -10.0, 0.5}, {0, 1, 0}};
    press.pick = pickDocument(press.ray, editor.document());
    REQUIRE(press.pick);
    CHECK(press.pick->faceId == FaceId::cap());
    press.screen = {100, 100};
    tools.pointerDown(press);
    PointerInput drag;
    drag.ray = Ray{{1.0 + 20.0, -1.2, 0.5 + 0.3}, glm::normalize(Vec3{-20.0, 0.0, -0.3})};
    drag.screen = {100, 40};
    tools.snap().enabled = false;
    tools.pointerMove(drag);
    tools.pointerUp(drag);
    REQUIRE(editor.document().find(id)->extrusion);
    CHECK(editor.document().find(id)->extrusion->height == doctest::Approx(1.2));
}

TEST_CASE("moving a rotated object keeps its rotation and survives save/reopen") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5);
    auto turned = rotateObject(doc, id, {0, 0, 0}, {0, 0, 1}, degrees(90));
    REQUIRE(turned.ok());
    auto moved = moveObject(turned->document, id, {1.0, 0.0, 0.25});
    REQUIRE_MESSAGE(moved.ok(), (moved.ok() ? "" : moved.error()));
    const auto& o = *moved->document.find(id);
    CHECK(o.transform.rotation == turned->document.find(id)->transform.rotation);
    const auto c = baseCorners(o);
    CHECK(c[0] == Vec3{1, 0, 0.25});
    CHECK(c[1] == Vec3{1, 2, 0.25});
    CHECK_FALSE(validateDocument(moved->document).has_value());
    const auto reopened = parseProject(serializeProject(moved->document, std::nullopt));
    REQUIRE(reopened.ok());
    CHECK(reopened->document.find(id)->transform == o.transform);
}

TEST_CASE("dimension anchors follow a rotated object and survive version 2 save/reopen") {
    auto [doc, id] = boxDoc(2.0, 1.0, 0.5);
    auto turned = rotateObject(doc, id, {0, 0, 0}, {0, 0, 1}, degrees(90));
    REQUIRE(turned.ok());
    Document d = turned->document;
    const auto& o = *d.find(id);
    // Every anchorable corner is the corner the mesh and picking use.
    const auto c = baseCorners(o);
    for (int i = 0; i < 4; ++i) CHECK(*objectVertex(o, i) == c[static_cast<std::size_t>(i)]);
    CHECK(*objectVertex(o, 6) == profileToWorld(o, o.profile.boundary[2], 0.5));
    CHECK(*objectVertex(o, 1) == Vec3{0, 2, 0});

    d.dimensions.push_back(Dimension{DimensionId{d.nextId}, DimensionAnchor{{}, id, {0}}, DimensionAnchor{{}, id, {1}}, {-0.4, 0, 0}});
    d.nextId += 1;
    REQUIRE_FALSE(validateDocument(d).has_value());
    CHECK(*resolveAnchor(d, d.dimensions[0].b) == Vec3{0, 2, 0});

    const auto loaded = parseProject(serializeProject(d, std::nullopt));
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
    CHECK(loaded->fileVersion == kProjectVersion);
    REQUIRE(loaded->document.dimensions.size() == 1);
    CHECK(loaded->document.dimensions[0] == d.dimensions[0]);
    CHECK(*resolveAnchor(loaded->document, loaded->document.dimensions[0].b) == Vec3{0, 2, 0});
}

TEST_CASE("a version 1 file with dimensions migrates and keeps them") {
    const char* v1 = R"({
  "format": "pullform-project", "version": 1, "generator": "Pullform 0.2.0",
  "document": { "lengthUnit": "m", "displayUnit": "m", "nextId": 3, "objects": [
    { "id": "obj-1", "type": "profile", "name": "Box 1",
      "profile": { "kind": "rectangle",
        "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
        "boundary": [[0,0],[2,0],[2,1],[0,1]] },
      "extrusion": { "height": 0.75 }, "transform": { "translation": [0, 0, 0] } } ],
    "dimensions": [{ "id": "dim-2", "a": { "object": "obj-1", "vertices": [0] },
      "b": { "point": [3, 0, 0] }, "offset": [0, -0.4, 0] }] } })";
    const auto r = parseProject(v1);
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error()));
    CHECK(r->fileVersion == 1);
    REQUIRE(r->document.dimensions.size() == 1);
    CHECK(*resolveAnchor(r->document, r->document.dimensions[0].b) == Vec3{3, 0, 0});
    const auto saved = serializeProject(r->document, std::nullopt);
    CHECK(saved.find("\"version\": 3") != std::string::npos);
    CHECK(saved.find("\"dimensions\"") != std::string::npos);
}
