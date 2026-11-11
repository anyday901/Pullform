// Solid tools (roadmap item 20): Union, Subtract, Intersect, Trim, Split and
// Outer Shell on closed solid objects, built on the edge/face mesh.
#include "helpers.hpp"

#include "core/commands/editor.hpp"
#include "core/commands/operations.hpp"
#include "core/commands/solid_tools.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/circle.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/solid.hpp"
#include "core/geometry/solid_boolean.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>

using namespace pf;
using pf::test::near;

#define REQUIRE_OK(r) REQUIRE_MESSAGE((r).ok(), ((r).ok() ? std::string{} : (r).error()))

namespace {

/// A w x d x h box with its lowest corner at `at`.
ObjectId addBox(Document& doc, const Vec3& at, double w, double d, double h) {
    auto made = createRectangle(doc, RectanglePlacement{at, w, d});
    REQUIRE(made);
    const ObjectId id = *made->objectId;
    auto box = extrudeProfile(made->document, id, h);
    REQUIRE(box);
    doc = box->document;
    return id;
}

/// A cylinder of radius r and height h standing on `base` (its axis along +Z).
ObjectId addCylinder(Document& doc, const Vec3& base, double r, double h, int segments) {
    auto placement = circleFromRadius(base, r, 0.0, segments);
    REQUIRE(placement);
    auto made = createCircle(doc, *placement, ProfileKind::Circle);
    REQUIRE(made);
    const ObjectId id = *made->objectId;
    auto cyl = extrudeProfile(made->document, id, h);
    REQUIRE(cyl);
    doc = cyl->document;
    return id;
}

double volume(const mesh::Mesh& m) { return checkSolid(mesh::solidMeshData(m)).signedVolume; }

const mesh::Mesh& meshOf(const Document& doc, ObjectId id) {
    const ProfileObject* o = doc.find(id);
    REQUIRE(o);
    REQUIRE(o->mesh);
    return *o->mesh;
}

/// The object is a valid closed solid of the given volume.
void requireSolid(const Document& doc, ObjectId id, double expectedVolume, double eps = 1e-9) {
    const ProfileObject* o = doc.find(id);
    REQUIRE(o);
    const auto invalid = validateObject(*o, "object");
    REQUIRE_MESSAGE(!invalid, invalid.value_or(""));
    REQUIRE(o->mesh);
    const auto problem = mesh::solidProblem(*o->mesh);
    REQUIRE_MESSAGE(!problem, problem.value_or(""));
    const SolidCheck check = checkSolid(mesh::solidMeshData(*o->mesh));
    REQUIRE_MESSAGE(check.ok(), check.problem);
    CHECK(near(check.signedVolume, expectedVolume, eps));
}

double polygonArea(double r, int n) { return 0.5 * n * r * r * std::sin(2.0 * std::numbers::pi / n); }

int holeCount(const mesh::Mesh& m) {
    int holes = 0;
    for (const auto& [id, f] : m.faces()) holes += static_cast<int>(f.loops.size()) - 1;
    return holes;
}

} // namespace

TEST_CASE("solid tools accept check: a cylinder subtracted from a box leaves a round hole") {
    Document doc;
    const ObjectId box = addBox(doc, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId cyl = addCylinder(doc, {0.5, 0.5, -0.5}, 0.25, 2.0, 48);

    auto cut = applySolidTool(doc, SolidTool::Subtract, {cyl, box});
    REQUIRE_OK(cut);
    const Document& out = cut->document;
    CHECK(out.objects.size() == 1); // the cylinder is used up
    CHECK(cut->objectId == box);
    requireSolid(out, box, 1.0 - polygonArea(0.25, 48));

    const mesh::Mesh& m = meshOf(out, box);
    // Top and bottom each have one round hole; the bore has 48 walls.
    CHECK(holeCount(m) == 2);
    CHECK(m.faces().size() == 6 + 48);
    // The bore keeps the cylinder's soft, smooth segment edges; the rims are hard.
    int soft = 0;
    for (const auto& [id, e] : m.edges()) soft += e.soft && e.smooth ? 1 : 0;
    CHECK(soft == 48);
    // checkSolid on the render path's own input too (front faces only).
    CHECK(checkSolid(mesh::solidMeshData(m)).ok());
    // The name follows the object into a group.
    CHECK(out.find(box)->name == "Group " + std::to_string(box.value));
}

TEST_CASE("solid tools: union, intersect and subtract of two overlapping boxes") {
    Document doc;
    const ObjectId a = addBox(doc, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId b = addBox(doc, {0.5, 0.5, 0.5}, 1.0, 1.0, 1.0);

    auto u = applySolidTool(doc, SolidTool::Union, {a, b});
    REQUIRE_OK(u);
    CHECK(u->document.objects.size() == 1);
    requireSolid(u->document, a, 2.0 - 0.125);
    CHECK(meshOf(u->document, a).faces().size() == 12); // three faces of each box are cut to L shapes or kept

    auto i = applySolidTool(doc, SolidTool::Intersect, {a, b});
    REQUIRE_OK(i);
    requireSolid(i->document, a, 0.125);
    CHECK(meshOf(i->document, a).faces().size() == 6);
    CHECK(meshOf(i->document, a).vertices().size() == 8);

    auto s = applySolidTool(doc, SolidTool::Subtract, {b, a});
    REQUIRE_OK(s);
    CHECK(s->document.objects.size() == 1);
    requireSolid(s->document, a, 0.875);
}

TEST_CASE("solid tools: boxes side by side on the ground merge into clean faces") {
    Document doc;
    const ObjectId a = addBox(doc, {0, 0, 0}, 2.0, 1.0, 1.0);
    const ObjectId b = addBox(doc, {1, 0, 0}, 2.0, 1.0, 1.0); // overlaps a by half, same depth and height
    auto u = applySolidTool(doc, SolidTool::Union, {a, b});
    REQUIRE_OK(u);
    requireSolid(u->document, a, 3.0);
    const mesh::Mesh& m = meshOf(u->document, a);
    CHECK(m.faces().size() == 6);
    CHECK(m.vertices().size() == 8);
    CHECK(m.edges().size() == 12);

    // Flush faces (touching, not overlapping) join too.
    Document flush;
    const ObjectId c = addBox(flush, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId d = addBox(flush, {1, 0, 0}, 1.0, 1.0, 1.0);
    auto joined = applySolidTool(flush, SolidTool::Union, {c, d});
    REQUIRE_OK(joined);
    requireSolid(joined->document, c, 2.0);
    CHECK(meshOf(joined->document, c).faces().size() == 6);

    // Boxes sharing only the ground plane: an L-shaped footprint.
    Document ground;
    const ObjectId e = addBox(ground, {0, 0, 0}, 2.0, 2.0, 1.0);
    const ObjectId f = addBox(ground, {1, 1, 0}, 2.0, 2.0, 2.0);
    auto l = applySolidTool(ground, SolidTool::Union, {e, f});
    REQUIRE_OK(l);
    requireSolid(l->document, e, 4.0 + 8.0 - 1.0);
}

TEST_CASE("solid tools: Trim keeps the cutter, Split leaves three solids") {
    Document doc;
    const ObjectId a = addBox(doc, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId b = addBox(doc, {0.5, 0.5, 0.5}, 1.0, 1.0, 1.0);

    auto t = applySolidTool(doc, SolidTool::Trim, {a, b});
    REQUIRE_OK(t);
    CHECK(t->document.objects.size() == 2);
    CHECK(t->document.find(a)->extrusion.has_value()); // the cutter is unchanged
    requireSolid(t->document, b, 0.875);

    auto s = applySolidTool(doc, SolidTool::Split, {a, b});
    REQUIRE_OK(s);
    REQUIRE(s->document.objects.size() == 3);
    requireSolid(s->document, a, 0.875);
    requireSolid(s->document, b, 0.875);
    const ObjectId piece = *s->objectId;
    CHECK(piece.value == doc.nextId);
    CHECK(s->document.nextId == doc.nextId + 1);
    requireSolid(s->document, piece, 0.125);
    CHECK(s->objectIds.size() == 3);
}

TEST_CASE("solid tools: Outer Shell removes a void, Union swallows a solid inside another") {
    Document doc;
    const ObjectId outer = addBox(doc, {0, 0, 0}, 3.0, 3.0, 3.0);
    const ObjectId inner = addBox(doc, {1, 1, 1}, 1.0, 1.0, 1.0);

    // Subtracting the inner box leaves a hollow box: two shells, the void pointing in.
    auto hollow = applySolidTool(doc, SolidTool::Subtract, {inner, outer});
    REQUIRE_OK(hollow);
    requireSolid(hollow->document, outer, 26.0);
    CHECK(meshOf(hollow->document, outer).faces().size() == 12);

    auto shell = applySolidTool(hollow->document, SolidTool::OuterShell, {outer});
    REQUIRE_OK(shell);
    requireSolid(shell->document, outer, 27.0);
    CHECK(meshOf(shell->document, outer).faces().size() == 6);
    // Nothing more to remove.
    CHECK_FALSE(applySolidTool(shell->document, SolidTool::OuterShell, {outer}).ok());

    auto u = applySolidTool(doc, SolidTool::Union, {outer, inner});
    REQUIRE_OK(u);
    requireSolid(u->document, outer, 27.0);
    CHECK(meshOf(u->document, outer).faces().size() == 6);
}

TEST_CASE("solid tools: rotated and separate solids") {
    Document doc;
    const ObjectId a = addBox(doc, {0, 0, 0}, 2.0, 2.0, 1.0);
    const ObjectId b = addBox(doc, {0.5, 0.5, -1.0}, 1.0, 1.0, 3.0);
    // Turn the post 45 degrees about its own axis: a diamond hole through the slab.
    auto turned = rotateObject(doc, b, {1.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, std::numbers::pi / 4.0);
    REQUIRE(turned);
    auto cut = applySolidTool(turned->document, SolidTool::Subtract, {b, a});
    REQUIRE_OK(cut);
    requireSolid(cut->document, a, 4.0 - 1.0, 1e-9);

    // Union of solids that do not touch: one object with two shells.
    Document apart;
    const ObjectId c = addBox(apart, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId d = addBox(apart, {5, 0, 0}, 1.0, 1.0, 1.0);
    auto u = applySolidTool(apart, SolidTool::Union, {c, d});
    REQUIRE_OK(u);
    requireSolid(u->document, c, 2.0);
    CHECK(u->document.objects.size() == 1);
    // ...but they have nothing to subtract, intersect or split.
    CHECK_FALSE(applySolidTool(apart, SolidTool::Subtract, {c, d}).ok());
    CHECK_FALSE(applySolidTool(apart, SolidTool::Intersect, {c, d}).ok());
    CHECK_FALSE(applySolidTool(apart, SolidTool::Split, {c, d}).ok());
}

TEST_CASE("solid tools: what they refuse") {
    Document doc;
    const ObjectId box = addBox(doc, {0, 0, 0}, 1.0, 1.0, 1.0);
    auto flat = createRectangle(doc, RectanglePlacement{{0.2, 0.2, 1.0}, 0.5, 0.5});
    REQUIRE(flat);
    const Document withFlat = flat->document;
    const ObjectId face = *flat->objectId;

    const auto notSolid = applySolidTool(withFlat, SolidTool::Union, {box, face});
    REQUIRE_FALSE(notSolid);
    CHECK(notSolid.error().find("is not a solid") != std::string::npos);
    CHECK_FALSE(applySolidTool(withFlat, SolidTool::Union, {box}).ok());             // needs two
    CHECK_FALSE(applySolidTool(withFlat, SolidTool::Subtract, {box, box}).ok());     // listed twice
    CHECK_FALSE(applySolidTool(withFlat, SolidTool::Subtract, {box, ObjectId{999}}).ok());

    // A cutter that swallows the target leaves nothing.
    Document swallow;
    const ObjectId small = addBox(swallow, {1, 1, 1}, 1.0, 1.0, 1.0);
    const ObjectId big = addBox(swallow, {0, 0, 0}, 3.0, 3.0, 3.0);
    CHECK_FALSE(applySolidTool(swallow, SolidTool::Subtract, {big, small}).ok());

    // Two boxes touching along one edge would share it between four faces.
    Document edge;
    const ObjectId e1 = addBox(edge, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId e2 = addBox(edge, {1, 1, 0}, 1.0, 1.0, 1.0);
    CHECK_FALSE(applySolidTool(edge, SolidTool::Union, {e1, e2}).ok());
}

TEST_CASE("solid tools: dimensions on changed objects are removed; undo and files keep the result") {
    Editor editor;
    Document doc;
    const ObjectId box = addBox(doc, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId cyl = addCylinder(doc, {0.5, 0.5, -0.5}, 0.25, 2.0, 24);
    auto dim = addDimension(doc, DimensionAnchor{{0, 0, 0}, box, {0}}, DimensionAnchor{{0, 0, 0}, box, {1}}, {0, -0.3, 0});
    REQUIRE(dim);
    REQUIRE(editor.commit("Setup", [&](const Document&) { return Expected<OpResult>(OpResult{dim->document, std::nullopt, {}}); }));
    REQUIRE(editor.document().dimensions.size() == 1);

    REQUIRE(editor.commit("Subtract", [&](const Document& d) { return applySolidTool(d, SolidTool::Subtract, {cyl, box}); }));
    CHECK(editor.document().objects.size() == 1);
    CHECK(editor.document().dimensions.empty());

    const std::string text = serializeProject(editor.document(), std::nullopt);
    auto loaded = parseProject(text);
    REQUIRE_OK(loaded);
    CHECK(sameContent(loaded->document, editor.document()));
    requireSolid(loaded->document, box, 1.0 - polygonArea(0.25, 24));

    editor.undo();
    CHECK(editor.document().objects.size() == 2);
    CHECK(editor.document().find(box)->extrusion.has_value());
    CHECK(editor.document().dimensions.size() == 1);
}

TEST_CASE("solid booleans: mesh-level checks") {
    Document doc;
    const ObjectId box = addBox(doc, {0, 0, 0}, 1.0, 1.0, 1.0);
    const mesh::MeshPtr m = mesh::objectMesh(*doc.find(box));
    CHECK(mesh::isClosedSolid(*m));
    CHECK(near(volume(*m), 1.0));

    // A box with one face removed is open.
    mesh::Mesh open = *m;
    open.eraseFace(open.faces().begin()->first == 0 ? mesh::FaceId{1} : mesh::FaceId{open.faces().begin()->first});
    REQUIRE(mesh::solidProblem(open));
    CHECK(*mesh::solidProblem(open) == "its surface is not closed");
    CHECK_FALSE(mesh::booleanSolids(open, *m, mesh::BooleanOp::Union).ok());

    // Moving a mesh keeps it a solid of the same volume.
    const mesh::Mesh moved = mesh::transformedMesh(*m, rotationMatrix(Transform{{}, axisAngle({0, 0, 1}, 0.3)}), {5, 0, 0});
    CHECK(mesh::isClosedSolid(moved));
    CHECK(near(volume(moved), 1.0, 1e-12));
}

TEST_CASE("solid booleans: volumes add up for rotated boxes and cylinders") {
    // |A u B| + |A n B| = |A| + |B| and |A - B| = |A| - |A n B|, for shapes
    // cut at odd angles: the classification and the welding must agree.
    struct Shape {
        bool round;
        Vec3 at;
        Vec3 size; // box w, d, h; cylinder radius, -, h
        double turnZ;
        double tiltX;
    };
    const Shape shapes[] = {
        {false, {0, 0, 0}, {1.0, 1.0, 1.0}, 0.0, 0.0},
        {false, {0.3, -0.2, 0.25}, {0.8, 0.6, 1.2}, 0.37, 0.21},
        {true, {0.5, 0.5, -0.3}, {0.3, 0, 1.6}, 0.0, 0.0},
        {true, {0.2, 0.6, 0.1}, {0.45, 0, 1.0}, 0.9, 1.1},
        {false, {0.5, 0.5, 0.5}, {0.7, 0.7, 0.7}, 0.785398, 0.6},
    };
    const auto make = [](Document& doc, const Shape& s) {
        const ObjectId id = s.round ? addCylinder(doc, s.at, s.size.x, s.size.z, 32) : addBox(doc, s.at, s.size.x, s.size.y, s.size.z);
        if (s.turnZ != 0.0) doc = rotateObject(doc, id, s.at, {0, 0, 1}, s.turnZ)->document;
        if (s.tiltX != 0.0) doc = rotateObject(doc, id, s.at, {1, 0, 0}, s.tiltX)->document;
        return id;
    };
    for (std::size_t i = 0; i < std::size(shapes); ++i) {
        for (std::size_t j = 0; j < std::size(shapes); ++j) {
            if (i == j) continue;
            CAPTURE(i);
            CAPTURE(j);
            Document doc;
            const ObjectId a = make(doc, shapes[i]);
            const ObjectId b = make(doc, shapes[j]);
            const double va = volume(*mesh::objectMesh(*doc.find(a)));
            const double vb = volume(*mesh::objectMesh(*doc.find(b)));
            auto u = applySolidTool(doc, SolidTool::Union, {a, b});
            auto n = applySolidTool(doc, SolidTool::Intersect, {a, b});
            auto d = applySolidTool(doc, SolidTool::Subtract, {b, a});
            REQUIRE_OK(u);
            REQUIRE_OK(n);
            REQUIRE_OK(d);
            const double vu = volume(meshOf(u->document, a));
            const double vn = volume(meshOf(n->document, a));
            const double vd = volume(meshOf(d->document, a));
            CHECK(vn > 0.0);
            CHECK(near(vu + vn, va + vb, 1e-9));
            CHECK(near(vd, va - vn, 1e-9));
            requireSolid(u->document, a, vu);
            requireSolid(n->document, a, vn);
            requireSolid(d->document, a, vd);
        }
    }
}

TEST_CASE("solid booleans: drilling twice and crossing bores") {
    Document doc;
    const ObjectId block = addBox(doc, {0, 0, 0}, 2.0, 1.0, 1.0);
    const ObjectId d1 = addCylinder(doc, {0.5, 0.5, -0.5}, 0.2, 2.0, 24);
    const ObjectId d2 = addCylinder(doc, {1.5, 0.5, -0.5}, 0.2, 2.0, 24);
    auto first = applySolidTool(doc, SolidTool::Subtract, {d1, block});
    REQUIRE_OK(first);
    auto second = applySolidTool(first->document, SolidTool::Subtract, {d2, block});
    REQUIRE_OK(second);
    requireSolid(second->document, block, 2.0 - 2.0 * polygonArea(0.2, 24));
    CHECK(holeCount(meshOf(second->document, block)) == 4);

    // A bore across the first one, through the block's long sides.
    ObjectId cross = addCylinder(doc, {0.5, -0.5, 0.5}, 0.15, 2.0, 24);
    Document crossed = second->document;
    crossed.objects.push_back(doc.objects.back());
    crossed.nextId = doc.nextId;
    crossed = rotateObject(crossed, cross, {0.5, -0.5, 0.5}, {1, 0, 0}, -std::numbers::pi / 2.0)->document;
    auto third = applySolidTool(crossed, SolidTool::Subtract, {cross, block});
    REQUIRE_OK(third);
    const double v = volume(meshOf(third->document, block));
    requireSolid(third->document, block, v);
    CHECK(v < 2.0 - 2.0 * polygonArea(0.2, 24));
    CHECK(v > 2.0 - 2.0 * polygonArea(0.2, 24) - polygonArea(0.15, 24) * 1.0);

    // A notch: a cylinder over the block's edge.
    Document notch;
    const ObjectId b2 = addBox(notch, {0, 0, 0}, 1.0, 1.0, 1.0);
    const ObjectId c2 = addCylinder(notch, {1.0, 0.5, -0.5}, 0.25, 2.0, 32);
    auto cut = applySolidTool(notch, SolidTool::Subtract, {c2, b2});
    REQUIRE_OK(cut);
    requireSolid(cut->document, b2, 1.0 - 0.5 * polygonArea(0.25, 32));
}
