// Move, Rotate and Scale on vertices, edges and faces (roadmap item 15):
// stretching, auto-fold, mirroring, and the tools acting on an element selection.
#include "helpers.hpp"

#include "core/commands/operations.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>

using namespace pf;
using pf::test::near;

namespace {

constexpr double kPi = std::numbers::pi;

/// A document with one w x d x h box at the origin (a profile group). Its mesh
/// ids follow meshFromProfile: base corners 1-4, cap corners 5-8 (corner 0 at
/// the origin, then +x, then +x+y, then +y), base edges 1-4, cap edges 5-8
/// (edge 5 runs from vertex 5 to 6, along +x at y = 0), vertical edges 9-12;
/// faces Base 1, Cap 2, Side i 3 + i (side 0, face 3, is the y = 0 wall).
std::pair<Document, ObjectId> boxDoc(double w, double d, double h) {
    Document doc;
    auto created = createRectangle(doc, RectanglePlacement{Vec3{0.0}, w, d});
    REQUIRE(created.ok());
    const ObjectId id = *created->objectId;
    auto extruded = extrudeProfile(created->document, id, h);
    REQUIRE(extruded.ok());
    return {extruded->document, id};
}

/// An L-shaped prism (a mesh group): 2 x 2 footprint with the +x+y quarter cut
/// away, 1 m tall.
std::pair<Document, ObjectId> lDoc() {
    Document doc;
    const std::vector<Vec3> pts{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}};
    auto drawn = drawPolyline(doc, std::nullopt, pts, true);
    REQUIRE(drawn.ok());
    const ObjectId id = *drawn->objectId;
    const auto& m = *drawn->document.find(id)->mesh;
    REQUIRE(m.faces().size() == 1);
    const mesh::FaceId face{m.faces().begin()->first};
    // The face is drawn facing down (seen from below it is counter-clockwise) or up; pull it to +z.
    const double dir = m.face(face)->normal.z > 0.0 ? 1.0 : -1.0;
    auto pulled = pushPullFace(drawn->document, id, face, dir * 1.0);
    REQUIRE_MESSAGE(pulled.ok(), (pulled.ok() ? "" : pulled.error()));
    return {pulled->document, id};
}

ElementRef edgeRef(ObjectId id, std::uint32_t e) { return ElementRef{id, ElementKind::Edge, e}; }
ElementRef faceRef(ObjectId id, std::uint32_t f) { return ElementRef{id, ElementKind::Face, f}; }
ElementRef vertexRef(ObjectId id, std::uint32_t v) { return ElementRef{id, ElementKind::Vertex, v}; }

Vec3 worldVertex(const Document& doc, ObjectId id, std::uint32_t v) {
    const auto& o = *doc.find(id);
    return localToWorld(o, mesh::objectMesh(o)->vertex(mesh::VertexId{v})->position);
}

bool nearVec(const Vec3& a, const Vec3& b, double eps = 1e-9) { return glm::length(a - b) <= eps; }

/// Whether the object has a vertex at world point `p`.
bool hasVertexAt(const Document& doc, ObjectId id, const Vec3& p) {
    const auto& o = *doc.find(id);
    const auto m = mesh::objectMesh(o);
    for (const auto& [vid, v] : m->vertices()) {
        if (nearVec(localToWorld(o, v.position), p, 1e-9)) return true;
    }
    return false;
}

/// The group's faces as one-sided triangles (the render mesh of a group is
/// two-sided), for checkSolid.
MeshData oneSided(const ProfileObject& o) {
    const mesh::Triangulation t = mesh::triangulate(*o.mesh);
    MeshData data;
    for (std::size_t i = 0; i < t.indices.size(); ++i) {
        const Vec3 n = rotationMatrix(o.transform) * o.mesh->face(t.triangleFaces[i / 3])->normal;
        data.positions.push_back(localToWorld(o, t.positions[t.indices[i]]));
        data.normals.push_back(n);
        data.indices.push_back(static_cast<std::uint32_t>(i));
    }
    return data;
}

/// The object is valid, closed and faces outwards; returns its volume.
double solidVolume(const Document& doc, ObjectId id) {
    const auto& o = *doc.find(id);
    REQUIRE_FALSE(validateObject(o, "object").has_value());
    const auto check = checkSolid(o.isMeshGroup() ? oneSided(o) : buildMesh(o));
    INFO(check.problem);
    CHECK(check.ok());
    return check.signedVolume;
}

} // namespace

TEST_CASE("roadmap check: moving a box's top edge makes a sloped roof") {
    auto [doc, id] = boxDoc(2.0, 1.0, 1.0);
    SUBCASE("sideways: the front wall leans and every face stays planar") {
        const auto moved = moveElements(doc, {edgeRef(id, 5)}, Vec3{0.0, 0.4, 0.0});
        REQUIRE_MESSAGE(moved.ok(), (moved.ok() ? "" : moved.error()));
        const Document& d = moved->document;
        const auto& o = *d.find(id);
        CHECK(o.isMeshGroup());
        CHECK(nearVec(worldVertex(d, id, 5), {0.0, 0.4, 1.0}));
        CHECK(nearVec(worldVertex(d, id, 6), {2.0, 0.4, 1.0}));
        CHECK(nearVec(worldVertex(d, id, 1), {0.0, 0.0, 0.0})); // the base stays
        // No face had to fold: the same 6 faces, the front one now sloping.
        CHECK(o.mesh->faces().size() == 6);
        const Vec3 n = o.mesh->face(mesh::FaceId{3})->normal;
        CHECK(n.z > 0.1);
        CHECK(n.y < -0.5);
        // A prism with a sheared front: volume = (1 + 0.6) / 2 * 1 * 2 = 1.6.
        CHECK(near(solidVolume(d, id), 1.6, 1e-9));
    }
    SUBCASE("up: the top face becomes a shed roof") {
        const auto moved = moveElements(doc, {edgeRef(id, 5)}, Vec3{0.0, 0.0, 0.5});
        REQUIRE(moved.ok());
        const auto& o = *moved->document.find(id);
        CHECK(o.mesh->faces().size() == 6);
        const Vec3 roof = o.mesh->face(mesh::FaceId{2})->normal;
        CHECK(near(roof.x, 0.0));
        CHECK(roof.y > 0.4); // slopes down towards the back
        CHECK(near(solidVolume(moved->document, id), 2.0 * 1.25, 1e-9));
    }
    SUBCASE("the selection's ids survive, and undo restores the box") {
        Editor editor;
        editor.resetDocument(doc);
        SelectionSet sel;
        sel.addElements({edgeRef(id, 5)}, std::nullopt);
        editor.setSelection(sel);
        REQUIRE(editor.commit("Move", [&](const Document& d) { return moveElements(d, {edgeRef(id, 5)}, Vec3{0, 0.4, 0}); }));
        CHECK(editor.selectionSet().containsElement(edgeRef(id, 5)));
        REQUIRE(editor.undo());
        CHECK_FALSE(editor.document().find(id)->isMeshGroup());
    }
}

TEST_CASE("moving one corner up folds the faces that would bend") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    // Out of all three of its faces' planes (straight up would stay in both walls).
    const auto moved = moveElements(doc, {vertexRef(id, 7)}, Vec3{0.2, 0.2, 0.3});
    REQUIRE_MESSAGE(moved.ok(), (moved.ok() ? "" : moved.error()));
    const auto& m = *moved->document.find(id)->mesh;
    // The cap and the two walls at that corner each split in two along a soft edge.
    CHECK(m.faces().size() == 9);
    int soft = 0;
    for (const auto& [eid, e] : m.edges()) soft += e.soft ? 1 : 0;
    CHECK(soft == 3);
    CHECK(solidVolume(moved->document, id) > 1.0);

    SUBCASE("a face with holes that would bend is refused") {
        // A square hole in a free face, then move one outer corner out of plane.
        Document d;
        auto outer = drawPolyline(d, std::nullopt, {{0, 0, 0}, {3, 0, 0}, {3, 3, 0}, {0, 3, 0}}, true);
        REQUIRE(outer.ok());
        const ObjectId g = *outer->objectId;
        auto hole = drawPolyline(outer->document, g, {{1, 1, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}}, true);
        REQUIRE(hole.ok());
        const auto& hm = *hole->document.find(g)->mesh;
        const auto corner = hm.findVertex(Vec3{3, 3, 0});
        REQUIRE(corner);
        const auto bent = moveElements(hole->document, {vertexRef(g, corner->value)}, Vec3{0, 0, 0.5});
        CHECK_FALSE(bent.ok());
    }
}

TEST_CASE("invalid element moves are refused with a reason") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    // Pushing the top edge down onto the bottom edge collapses the front wall's side edges... 
    // Moving the top face through the base turns the walls inside out.
    CHECK_FALSE(moveElements(doc, {faceRef(id, 2)}, Vec3{0.0, 0.0, -1.0}).ok());
    CHECK_FALSE(moveElements(doc, {faceRef(id, 2)}, Vec3{0.0, 0.0, -2.0}).ok());
    CHECK_FALSE(moveElements(doc, {}, Vec3{1.0, 0.0, 0.0}).ok());
    CHECK_FALSE(moveElements(doc, {edgeRef(id, 99)}, Vec3{1.0, 0.0, 0.0}).ok());
    CHECK_FALSE(moveElements(doc, {edgeRef(id, 5)}, Vec3{0.0}).ok());
    // A move of the whole box's vertices is just a move.
    std::vector<ElementRef> all;
    for (std::uint32_t f = 1; f <= 6; ++f) all.push_back(faceRef(id, f));
    const auto whole = moveElements(doc, all, Vec3{1.0, 0.0, 0.0});
    REQUIRE(whole.ok());
    CHECK(near(solidVolume(whole->document, id), 1.0));
}

TEST_CASE("rotating a face about one of its edges") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    // Tilt the cap about its back edge (y = 1, z = 1, along x) by 30 degrees.
    const auto turned = rotateElements(doc, {faceRef(id, 2)}, Vec3{0.0, 1.0, 1.0}, Vec3{1.0, 0.0, 0.0}, -kPi / 6.0);
    REQUIRE_MESSAGE(turned.ok(), (turned.ok() ? "" : turned.error()));
    const Document& d = turned->document;
    CHECK(nearVec(worldVertex(d, id, 7), {1.0, 1.0, 1.0})); // on the hinge
    CHECK(nearVec(worldVertex(d, id, 5), {0.0, 1.0 - std::cos(kPi / 6.0), 1.0 + std::sin(kPi / 6.0)}, 1e-12));
    const Vec3 n = d.find(id)->mesh->face(mesh::FaceId{2})->normal;
    CHECK(near(n.y, std::sin(kPi / 6.0), 1e-12)); // the front lifts, so the cap faces back
    solidVolume(d, id);
}

TEST_CASE("roadmap check: scaling by -1 mirrors") {
    SUBCASE("a whole L-shaped group mirrored across its width stays a closed, outward solid") {
        auto [doc, id] = lDoc();
        const double before = solidVolume(doc, id);
        const auto mirrored = scaleObject(doc, id, Vec3{-1.0, 1.0, 1.0});
        REQUIRE_MESSAGE(mirrored.ok(), (mirrored.ok() ? "" : mirrored.error()));
        CHECK(near(solidVolume(mirrored->document, id), before, 1e-9));
        // The notch moves from +x+y to -x+y of the base centre (1, 1).
        const auto& m = *mirrored->document.find(id)->mesh;
        CHECK(m.findVertex(Vec3{1.0, 1.0, 0.0}));
        CHECK(m.findVertex(Vec3{0.0, 1.0, 0.0}));
        CHECK(m.findVertex(Vec3{1.0, 2.0, 0.0}));
        CHECK(m.findVertex(Vec3{2.0, 2.0, 0.0}));
        CHECK_FALSE(m.findVertex(Vec3{0.0, 2.0, 0.0}));
    }
    SUBCASE("a uniform -1 is a point mirror, still outward") {
        auto [doc, id] = lDoc();
        const auto mirrored = scaleObject(doc, id, Vec3{-1.0});
        REQUIRE(mirrored.ok());
        CHECK(near(solidVolume(mirrored->document, id), 3.0, 1e-9));
    }
    SUBCASE("a box mirrored in height flips below its base") {
        Document doc;
        auto created = createRectangle(doc, RectanglePlacement{Vec3{0.0}, 1.0, 1.0});
        auto box = extrudeProfile(created->document, *created->objectId, 2.0);
        const ObjectId id = *created->objectId;
        const auto flipped = scaleObject(box->document, id, Vec3{1.0, 1.0, -1.0});
        REQUIRE(flipped.ok());
        const Bounds b = objectBounds(*flipped->document.find(id));
        CHECK(near(b.min.z, -2.0));
        CHECK(near(b.max.z, 0.0));
    }
    SUBCASE("a selected free face mirrored across its width still faces up") {
        Document doc;
        const std::vector<Vec3> pts{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}};
        auto drawn = drawPolyline(doc, std::nullopt, pts, true);
        REQUIRE(drawn.ok());
        const ObjectId id = *drawn->objectId;
        const auto& m = *drawn->document.find(id)->mesh;
        const std::uint32_t f = m.faces().begin()->first;
        const double nz = m.face(mesh::FaceId{f})->normal.z;
        const auto mirrored = scaleElements(drawn->document, {faceRef(id, f)}, Vec3{-1.0, 1.0, 1.0}, Vec3{1.0, 1.0, 0.0});
        REQUIRE_MESSAGE(mirrored.ok(), (mirrored.ok() ? "" : mirrored.error()));
        const auto& mm = *mirrored->document.find(id)->mesh;
        CHECK(near(mm.face(mesh::FaceId{f})->normal.z, nz));
        CHECK(mm.findVertex(Vec3{0.0, 1.0, 0.0}));
        CHECK_FALSE(mm.findVertex(Vec3{2.0, 1.0, 0.0}));
    }
    SUBCASE("a cap mirrored below the base would turn the box inside out") {
        auto [doc, id] = boxDoc(2.0, 1.0, 1.0);
        CHECK_FALSE(scaleElements(doc, {faceRef(id, 2)}, Vec3{1.0, 1.0, -1.0}, Vec3{0.0}).ok());
    }
    SUBCASE("zero factors are refused") {
        auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
        CHECK_FALSE(scaleElements(doc, {faceRef(id, 2)}, Vec3{0.0, 1.0, 1.0}, Vec3{0.0}).ok());
        CHECK_FALSE(scaleObject(doc, id, Vec3{1.0, 0.0, 1.0}).ok());
    }
}

TEST_CASE("curves follow rigid and uniform transforms and are dropped otherwise") {
    Document doc;
    auto circle = createCircle(doc, CirclePlacement{Vec3{0.0}, 1.0, 0.0, 24});
    REQUIRE(circle.ok());
    const ObjectId id = *circle->objectId;
    std::vector<ElementRef> face{faceRef(id, 1)};
    const auto moved = moveElements(circle->document, face, Vec3{1.0, 0.0, 0.0});
    REQUIRE(moved.ok());
    const auto& m = *moved->document.find(id)->mesh;
    REQUIRE(m.curves().size() == 1);
    CHECK(nearVec(m.curves().begin()->second.center, {1.0, 0.0, 0.0}));
    const auto stretched = scaleElements(circle->document, face, Vec3{2.0, 1.0, 1.0}, Vec3{0.0});
    REQUIRE(stretched.ok());
    CHECK(stretched->document.find(id)->mesh->curves().empty());
    const auto mirrored = scaleElements(circle->document, face, Vec3{-2.0, 2.0, 2.0}, Vec3{0.0});
    REQUIRE(mirrored.ok());
    const auto& mm = *mirrored->document.find(id)->mesh;
    REQUIRE(mm.curves().size() == 1);
    CHECK(near(mm.curves().begin()->second.radius, 2.0));
}

// ---- The tools on an element selection ------------------------------------------------

namespace {

/// An editor holding `doc` with `elements` selected (as the Select tool leaves them).
void selectElements(Editor& editor, const std::vector<ElementRef>& elements) {
    SelectionSet set;
    set.addElements(elements, std::nullopt);
    editor.setSelection(set);
}

/// A pointer looking at `at` from `eye`, at `screen`, resolved like the viewport does.
PointerInput look(const Editor& editor, const Vec3& eye, const Vec3& at, Vec2 screen = {0, 0}) {
    PointerInput in = test::lookAtElements(editor, eye, at, 0.001);
    in.screen = screen;
    return in;
}

} // namespace

TEST_CASE("Move tool: a selected edge moves and the box stretches (sloped roof)") {
    auto [doc, id] = boxDoc(2.0, 1.0, 1.0);
    Editor editor;
    editor.resetDocument(doc);
    ToolController tools(editor);
    tools.snap().points = false;
    selectElements(editor, {edgeRef(id, 5)});
    tools.setTool(Tool::Move);

    SUBCASE("click, move, click") {
        tools.pointerDown(test::downAt(editor, 1.0, 0.5, {10, 10}));
        REQUIRE(tools.active());
        tools.pointerMove(test::downAt(editor, 1.0, 0.9, {40, 10}));
        const auto* p = std::get_if<MovePreview>(&editor.preview());
        REQUIRE(p);
        CHECK(p->elements);
        CHECK_FALSE(p->error.has_value());
        REQUIRE(p->results.size() == 1);
        CHECK(replacedObjects(editor.preview()) == std::vector<ObjectId>{id});
        CHECK(editor.document().find(id)->isMeshGroup() == false); // only a preview so far
        tools.pointerDown(test::downAt(editor, 1.0, 0.9, {40, 10}));
        CHECK_FALSE(tools.active());
        CHECK(nearVec(worldVertex(editor.document(), id, 5), {0.0, 0.4, 1.0}));
        CHECK(editor.undoLabel().starts_with("Move edge"));
        CHECK(editor.selectionSet().containsElement(edgeRef(id, 5)));
        CHECK(near(solidVolume(editor.document(), id), 1.6, 1e-9));
    }
    SUBCASE("typed x, y, z") {
        REQUIRE_FALSE(tools.submitMoveDistance("0, 0, 500").has_value());
        CHECK(nearVec(worldVertex(editor.document(), id, 6), {2.0, 0.0, 1.5}));
        CHECK(near(solidVolume(editor.document(), id), 2.5, 1e-9));
    }
    SUBCASE("copy mode is refused for elements") {
        tools.toggleMoveCopy();
        CHECK(tools.submitMoveDistance("0, 400, 0").has_value());
        CHECK_FALSE(editor.document().find(id)->isMeshGroup());
    }
}

TEST_CASE("Rotate tool: a face turns about an edge dragged along from the centre") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    Editor editor;
    editor.resetDocument(doc);
    ToolController tools(editor);
    selectElements(editor, {faceRef(id, 2)});
    tools.setTool(Tool::Rotate);
    const Vec3 eye{0.5, -2.0, 4.0};
    // Press on the back edge's midpoint and drag to its end: the axis is that edge (+x).
    tools.pointerDown(look(editor, eye, {0.5, 1.0, 1.0}, {100, 100}));
    REQUIRE(tools.active());
    tools.pointerUp(look(editor, eye, {1.0, 1.0, 1.0}, {160, 100}));
    const auto* p = std::get_if<RotatePreview>(&editor.preview());
    REQUIRE(p);
    CHECK(nearVec(p->pivot, {0.5, 1.0, 1.0}, 1e-9));
    CHECK(nearVec(p->axis, {1.0, 0.0, 0.0}, 1e-9));
    REQUIRE_FALSE(tools.submitRotateAngle("-30").has_value());
    const Document& d = editor.document();
    CHECK(nearVec(worldVertex(d, id, 5), {0.0, 1.0 - std::cos(kPi / 6.0), 1.0 + std::sin(kPi / 6.0)}, 1e-9));
    CHECK(nearVec(worldVertex(d, id, 7), {1.0, 1.0, 1.0}));
    CHECK(editor.undoLabel().starts_with("Rotate face"));
    solidVolume(d, id);
}

TEST_CASE("Scale tool: grips on the bounding box, and -1 mirrors") {
    auto [doc, id] = lDoc();
    Editor editor;
    editor.resetDocument(doc);
    ToolController tools(editor);
    tools.snap().points = false;
    editor.select(Selection{id, std::nullopt});
    tools.setTool(Tool::Scale);
    // Activating shows the box (0..2 in x and y, 0..1 in z) with 26 grips.
    const auto* idle = std::get_if<ScalePreview>(&editor.preview());
    REQUIRE(idle);
    CHECK(idle->grips.size() == 26);
    CHECK(idle->box.size() == 8);
    CHECK_FALSE(replacedObject(editor.preview()).has_value());

    const Vec3 grip{2.0, 1.0, 0.5}; // the +x face grip
    const Vec3 eye = grip + Vec3{4.0, -3.0, 5.0};
    tools.pointerMove(look(editor, eye, grip, {200, 200}));
    idle = std::get_if<ScalePreview>(&editor.preview());
    REQUIRE(idle);
    REQUIRE(idle->hotGrip);
    CHECK(nearVec(idle->grips[*idle->hotGrip], grip));

    SUBCASE("drag the face grip: width only, about the opposite face") {
        tools.pointerDown(look(editor, eye, grip, {200, 200}));
        REQUIRE(tools.active());
        tools.pointerMove(look(editor, eye, {3.0, 1.0, 0.5}, {260, 200}));
        const auto* p = std::get_if<ScalePreview>(&editor.preview());
        REQUIRE(p);
        CHECK(nearVec(p->factors, {1.5, 1.0, 1.0}));
        tools.pointerDown(look(editor, eye, {3.0, 1.0, 0.5}, {260, 200}));
        CHECK_FALSE(tools.active());
        const Bounds b = objectBounds(*editor.document().find(id));
        CHECK(nearVec(b.min, {0.0, 0.0, 0.0}));
        CHECK(nearVec(b.max, {3.0, 2.0, 1.0}));
    }
    SUBCASE("drag past the opposite face mirrors") {
        tools.pointerDown(look(editor, eye, grip, {200, 200}));
        tools.pointerMove(look(editor, eye, {-2.0, 1.0, 0.5}, {100, 200}));
        const auto* p = std::get_if<ScalePreview>(&editor.preview());
        REQUIRE(p);
        CHECK(nearVec(p->factors, {-1.0, 1.0, 1.0}));
        CHECK_FALSE(p->error.has_value());
    }
    SUBCASE("roadmap check: type -1 on the grip") {
        tools.pointerDown(look(editor, eye, grip, {200, 200}));
        REQUIRE_FALSE(tools.submitScaleFactors("-1").has_value());
        CHECK_FALSE(tools.active());
        const Document& d = editor.document();
        // Mirrored across x = 0 (the opposite face): the notch is now at -x.
        CHECK(hasVertexAt(d, id, {-1.0, 1.0, 0.0}));
        CHECK(hasVertexAt(d, id, {-2.0, 1.0, 0.0}));
        CHECK(hasVertexAt(d, id, {-1.0, 2.0, 0.0}));
        CHECK_FALSE(hasVertexAt(d, id, {-2.0, 2.0, 0.0}));
        CHECK(near(solidVolume(editor.document(), id), 3.0, 1e-9));
        CHECK(editor.undoLabel() == "Scale -100%, 100%, 100%");
    }
}

TEST_CASE("Scale tool: a selected cap scales on its own into a frustum") {
    auto [doc, id] = boxDoc(1.0, 1.0, 1.0);
    Editor editor;
    editor.resetDocument(doc);
    ToolController tools(editor);
    selectElements(editor, {faceRef(id, 2)});
    tools.setTool(Tool::Scale);
    // The cap is flat: its box has grips only for width and depth.
    const auto* idle = std::get_if<ScalePreview>(&editor.preview());
    REQUIRE(idle);
    CHECK(idle->grips.size() == 8);
    REQUIRE_FALSE(tools.submitScaleFactors("50%").has_value());
    CHECK(nearVec(worldVertex(editor.document(), id, 5), {0.25, 0.25, 1.0}));
    CHECK(nearVec(worldVertex(editor.document(), id, 1), {0.0, 0.0, 0.0}));
    // A square frustum: h/3 (A1 + A2 + sqrt(A1 A2)) = (1 + 0.25 + 0.5) / 3.
    CHECK(near(solidVolume(editor.document(), id), 1.75 / 3.0, 1e-9));
    CHECK(editor.undoLabel().starts_with("Scale face"));
}
