// File format version 3 (objects are groups holding an edge/face mesh) and the
// migration of every earlier version. See docs/ARCHITECTURE.md (Persistence).
#include "core/commands/editor.hpp"
#include "core/commands/operations.hpp"
#include "core/document/project_file.hpp"
#include "core/document/validate.hpp"
#include "core/geometry/measure.hpp"
#include "core/geometry/mesh.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/solid.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace pf;

namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

/// World corners of a version 1/2 object computed straight from its JSON (base
/// corners, then cap corners), independently of the reader and migrations.
std::vector<Vec3> cornersFromJson(const nlohmann::json& o) {
    const auto& t = o["transform"];
    const Vec3 translation{t["translation"][0].get<double>(), t["translation"][1].get<double>(), t["translation"][2].get<double>()};
    Quat q{1, 0, 0, 0};
    if (t.contains("rotation") && t["rotation"].is_array()) {
        const auto& r = t["rotation"];
        q = Quat{r[3].get<double>(), r[0].get<double>(), r[1].get<double>(), r[2].get<double>()};
    }
    std::vector<Vec3> out;
    const auto add = [&](double z) {
        for (const auto& p : o["profile"]["boundary"]) out.push_back(translation + q * Vec3{p[0].get<double>(), p[1].get<double>(), z});
    };
    add(0.0);
    if (o["extrusion"].is_object()) add(o["extrusion"]["height"].get<double>());
    return out;
}

bool sameMeshData(const MeshData& a, const MeshData& b) {
    if (a.positions.size() != b.positions.size() || a.indices != b.indices || a.edges.size() != b.edges.size()) return false;
    for (std::size_t i = 0; i < a.positions.size(); ++i) {
        if (glm::length(a.positions[i] - b.positions[i]) > 1e-12) return false;
    }
    for (std::size_t i = 0; i < a.edges.size(); ++i) {
        if (glm::length(a.edges[i] - b.edges[i]) > 1e-12) return false;
    }
    return a.triangleFaces == b.triangleFaces;
}

} // namespace

TEST_CASE("format v3: every version 1 and 2 test file opens, renders the same and survives a v3 round trip") {
    int opened = 0;
    for (const auto& entry : std::filesystem::directory_iterator(PULLFORM_TEST_DATA)) {
        const auto path = entry.path();
        const std::string name = path.filename().string();
        if (name.rfind("v1-", 0) != 0 && name.rfind("v2-", 0) != 0) continue;
        CAPTURE(name);
        const std::string text = readText(path);
        const auto raw = nlohmann::json::parse(text);
        const auto loaded = parseProject(text);
        REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
        CHECK(loaded->fileVersion == raw["version"].get<int>());
        const Document& doc = loaded->document;
        REQUIRE(doc.objects.size() == raw["document"]["objects"].size());

        for (std::size_t i = 0; i < doc.objects.size(); ++i) {
            const ProfileObject& o = *doc.objects[i];
            CHECK_FALSE(o.isMeshGroup()); // rectangles and boxes stay parametric
            const auto expected = cornersFromJson(raw["document"]["objects"][i]);
            REQUIRE(vertexCount(o) == static_cast<int>(expected.size()));
            const auto m = mesh::objectMesh(o);
            REQUIRE_FALSE(mesh::validateMesh(*m, "mesh").has_value());
            for (int k = 0; k < vertexCount(o); ++k) {
                // The rendered corner and the mesh vertex (id k + 1) are where the file said.
                CHECK(glm::length(*objectVertex(o, k) - expected[static_cast<std::size_t>(k)]) < 1e-9);
                const auto* v = m->vertex(mesh::VertexId{static_cast<std::uint32_t>(k + 1)});
                REQUIRE(v);
                CHECK(glm::length(localToWorld(o, v->position) - expected[static_cast<std::size_t>(k)]) < 1e-9);
            }
            const auto faces = buildFaces(o);
            CHECK(faces.size() == (o.extrusion ? 6u : 1u));
            for (const auto& f : faces) CHECK(m->face(mesh::faceIdFor(f.id)));
            if (o.extrusion) CHECK(checkSolid(buildMesh(o)).ok());
        }
        if (raw["document"].contains("dimensions")) {
            CHECK(doc.dimensions.size() == raw["document"]["dimensions"].size());
            for (const auto& d : doc.dimensions) CHECK(layoutDimension(doc, d).has_value());
        }

        // Saved as the current version, reopened: identical content and identical render meshes.
        const std::string saved = serializeProject(doc, loaded->view);
        CHECK(saved.find("\"version\": 4") != std::string::npos);
        const auto again = parseProject(saved);
        REQUIRE_MESSAGE(again.ok(), (again.ok() ? "" : again.error()));
        CHECK(again->fileVersion == 4);
        CHECK(sameContent(again->document, doc));
        for (std::size_t i = 0; i < doc.objects.size(); ++i) {
            CHECK(sameMeshData(buildMesh(*again->document.objects[i]), buildMesh(*doc.objects[i])));
        }
        ++opened;
    }
    CHECK(opened >= 3);
}

TEST_CASE("format v3: a migrated box keeps the deterministic mesh ids in the file") {
    const auto loaded = parseProject(readText(std::filesystem::path(PULLFORM_TEST_DATA) / "v2-rotated.pullform.json"));
    REQUIRE(loaded.ok());
    const auto file = nlohmann::json::parse(serializeProject(loaded->document, std::nullopt));
    const auto& box = file["document"]["objects"][0];
    CHECK(box["type"] == "group");
    CHECK(box.contains("profile"));
    const auto& m = box["mesh"];
    CHECK(m["vertices"].size() == 8);
    CHECK(m["edges"].size() == 12);
    CHECK(m["faces"].size() == 6);
    CHECK(m["next"] == nlohmann::json::array({9, 13, 7, 1}));
    // Face 2 is the cap: the top loop, edges 5..8 forward.
    CHECK(m["faces"][1]["id"] == 2);
    CHECK(m["faces"][1]["loops"][0] == nlohmann::json::array({5, 6, 7, 8}));
}

TEST_CASE("format v3: drawn edges and faces save and reopen with their ids") {
    Editor editor;
    const Vec3 a{0, 0, 0}, b{2, 0, 0}, c{2, 1, 0}, d{0, 1, 0};
    for (const auto& [p, q] : std::vector<std::pair<Vec3, Vec3>>{{a, b}, {b, c}, {c, d}, {d, a}}) {
        REQUIRE(editor.commit("Line", [&](const Document& doc) { return drawEdge(doc, std::nullopt, p, q); }));
    }
    REQUIRE(editor.document().objects.size() == 1); // each line stuck to the last
    const ProfileObject& group = *editor.document().objects[0];
    REQUIRE(group.isMeshGroup());
    CHECK(group.mesh->faces().size() == 1);
    // Split it with a line across, then reopen.
    REQUIRE(editor.commit("Line", [&](const Document& doc) { return drawEdge(doc, group.id, {1, 0, 0}, {1, 1, 0}); }));
    const auto& before = *editor.document().objects[0]->mesh;
    CHECK(before.faces().size() == 2);

    const std::string text = serializeProject(editor.document(), std::nullopt);
    const auto loaded = parseProject(text);
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
    CHECK(sameContent(loaded->document, editor.document()));
    const auto& after = *loaded->document.objects[0]->mesh;
    CHECK(after.nextFaceId() == before.nextFaceId());
    CHECK(after.edges() == before.edges());

    // The faces render and pick: a ray down onto the left half hits a mesh face.
    const auto hit = pickDocument(Ray{{0.5, 0.5, 5.0}, {0, 0, -1}}, loaded->document);
    REQUIRE(hit);
    CHECK(hit->faceId.kind == FaceKind::Mesh);
    const auto data = buildMesh(*loaded->document.objects[0]);
    CHECK(data.edges.size() == 2 * 7);
    CHECK(data.faces.size() == 2);
}

TEST_CASE("format v3: invalid meshes are rejected") {
    Editor editor;
    for (const auto& [p, q] : std::vector<std::pair<Vec3, Vec3>>{{{0, 0, 0}, {1, 0, 0}}, {{1, 0, 0}, {1, 1, 0}}, {{1, 1, 0}, {0, 0, 0}}}) {
        REQUIRE(editor.commit("Line", [&](const Document& doc) { return drawEdge(doc, std::nullopt, p, q); }));
    }
    const std::string good = serializeProject(editor.document(), std::nullopt);
    REQUIRE(parseProject(good).ok());
    const auto broken = [&](auto change) {
        auto file = nlohmann::json::parse(good);
        change(file["document"]["objects"][0]["mesh"]);
        return parseProject(file.dump());
    };
    CHECK_FALSE(broken([](auto& m) { m["edges"][0][2] = 99; }).ok());          // missing vertex
    CHECK_FALSE(broken([](auto& m) { m["faces"][0]["loops"][0][0] = 42; }).ok()); // missing edge
    CHECK_FALSE(broken([](auto& m) { m["faces"][0]["loops"][0].erase(0); }).ok()); // open loop
    CHECK_FALSE(broken([](auto& m) { m["next"][0] = 1; }).ok());                // next below used ids
    CHECK_FALSE(broken([](auto& m) { m["vertices"][0][1] = 1e9; }).ok());       // out of range
    CHECK_FALSE(broken([](auto& m) { m.erase("next"); }).ok());

    // A profile group whose stored mesh disagrees with its profile.
    Document doc;
    auto rect = createRectangle(doc, RectanglePlacement{{0, 0, 0}, 1.0, 1.0});
    REQUIRE(rect.ok());
    auto file = nlohmann::json::parse(serializeProject(rect->document, std::nullopt));
    file["document"]["objects"][0]["mesh"]["vertices"][2][1] = 5.0;
    const auto mismatch = parseProject(file.dump());
    REQUIRE_FALSE(mismatch.ok());
    CHECK(mismatch.error().find("does not match") != std::string::npos);
}

TEST_CASE("mesh operations: drawing and erasing edges are single undo entries") {
    Editor editor;
    const auto line = [&](Vec3 p, Vec3 q) {
        return editor.commit("Line", [=](const Document& doc) { return drawEdge(doc, std::nullopt, p, q); });
    };
    REQUIRE(line({0, 0, 0}, {1, 0, 0}));
    REQUIRE(line({1, 0, 0}, {1, 1, 0}));
    REQUIRE(line({1, 1, 0}, {0, 1, 0}));
    REQUIRE(line({0, 1, 0}, {0, 0, 0}));
    CHECK(editor.document().objects[0]->mesh->faces().size() == 1);
    editor.undo();
    CHECK(editor.document().objects[0]->mesh->faces().empty());
    editor.redo();
    CHECK(editor.document().objects[0]->mesh->faces().size() == 1);
    // A line far away starts its own group.
    REQUIRE(line({5, 5, 0}, {6, 5, 0}));
    CHECK(editor.document().objects.size() == 2);
    // Zero length is refused and leaves no history entry.
    const auto depth = editor.undoDepth();
    CHECK_FALSE(line({5, 5, 0}, {5, 5, 0}));
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("mesh operations: erasing an edge of a box converts it to a mesh group") {
    Editor editor;
    Document doc;
    auto rect = createRectangle(doc, RectanglePlacement{{1, 1, 0}, 2.0, 1.0});
    REQUIRE(rect.ok());
    auto box = extrudeProfile(rect->document, *rect->objectId, 0.5);
    REQUIRE(box.ok());
    const ObjectId id = *box->objectId;
    auto dimmed = addDimension(box->document, DimensionAnchor{{}, id, {4}}, DimensionAnchor{{}, id, {6}}, {0, 0, 0.3});
    REQUIRE(dimmed.ok());
    editor.resetDocument(dimmed->document);

    // Erase a vertical edge (corner 0 to corner 4: edge id 2n + 1 = 9): both
    // sides touching it go, as they are not coplanar.
    REQUIRE(editor.commit("Erase", [&](const Document& d) { return eraseEdge(d, id, mesh::EdgeId{9}); }));
    const ProfileObject& o = *editor.document().find(id);
    REQUIRE(o.isMeshGroup());
    CHECK_FALSE(o.extrusion);
    CHECK(o.mesh->faces().size() == 4);
    CHECK(o.mesh->edges().size() == 11);
    // The corner ids did not change, so the dimension still measures the cap.
    REQUIRE(editor.document().dimensions.size() == 1);
    const auto layout = layoutDimension(editor.document(), editor.document().dimensions[0]);
    REQUIRE(layout);
    CHECK(layout->length == doctest::Approx(std::sqrt(5.0)));
    CHECK_FALSE(validateDocument(editor.document()).has_value());
    editor.undo();
    CHECK_FALSE(editor.document().find(id)->isMeshGroup());

    // A rectangle: erasing an edge of its only face leaves three loose edges.
    Editor e2;
    e2.resetDocument(rect->document);
    REQUIRE(e2.commit("Erase", [&](const Document& d) { return eraseEdge(d, *rect->objectId, mesh::EdgeId{1}); }));
    const auto& r = *e2.document().find(*rect->objectId);
    CHECK(r.mesh->faces().empty());
    CHECK(r.mesh->edges().size() == 3);
}

TEST_CASE("mesh operations: a group can be moved, rotated, scaled and duplicated") {
    Editor editor;
    for (const auto& [p, q] : std::vector<std::pair<Vec3, Vec3>>{{{0, 0, 0}, {2, 0, 0}}, {{2, 0, 0}, {2, 1, 0}}, {{2, 1, 0}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 0}}}) {
        REQUIRE(editor.commit("Line", [&](const Document& doc) { return drawEdge(doc, std::nullopt, p, q); }));
    }
    const ObjectId id = editor.document().objects[0]->id;
    REQUIRE(editor.commit("Move", [&](const Document& d) { return moveObject(d, id, {1, 0, 0}); }));
    REQUIRE(editor.commit("Rotate", [&](const Document& d) { return rotateObject(d, id, {1, 0, 0}, {0, 0, 1}, 1.0); }));
    REQUIRE(editor.commit("Scale", [&](const Document& d) { return scaleObject(d, id, {2, 3, 1}); }));
    const auto& m = *editor.document().find(id)->mesh;
    double area = 0.0;
    for (const auto& [fid, f] : m.faces()) area += mesh::signedArea(m.loopPositions(f.loops[0]), f.normal);
    CHECK(area == doctest::Approx(12.0));
    REQUIRE(editor.commit("Duplicate", [&](const Document& d) { return duplicateObject(d, id, {0, 5, 0}); }));
    CHECK(editor.document().objects.size() == 2);
    CHECK_FALSE(editor.commit("Push", [&](const Document& d) { return extrudeProfile(d, id, 1.0); }));
}
