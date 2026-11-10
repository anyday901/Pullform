#include "helpers.hpp"

#include "core/document/project_file.hpp"
#include "core/geometry/solid.hpp"

#include <filesystem>
#include <fstream>
#include <random>

using namespace pf;
using pf::test::downAt;
using pf::test::levelAt;

namespace {

std::filesystem::path tempDir() {
    auto dir = std::filesystem::temp_directory_path() / ("pullform-tests-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("scenario 7: save, reload, compare, and keep editing") {
    Editor editor;
    ToolController tools(editor);
    const ObjectId a = test::makeBox(editor, tools, "2000, 1000", "750");
    const ObjectId b = test::makeBox(editor, tools, "300, 400", nullptr, {4.0, 4.0});
    REQUIRE(editor.commit("Units", [](const Document& d) { return setDisplayUnit(d, LengthUnit::Centimeter); }));

    const auto dir = tempDir();
    const auto path = dir / "model.pullform.json";
    const CameraView view{{5, -6, 4}, {1, 0.5, 0}};
    REQUIRE_FALSE(writeFileAtomically(path, serializeProject(editor.document(), view)).has_value());
    editor.markSaved();

    const auto text = readProjectFile(path);
    REQUIRE(text.ok());
    const auto loaded = parseProject(*text);
    REQUIRE_MESSAGE(loaded.ok(), (loaded.ok() ? "" : loaded.error()));
    const Document& doc = loaded->document;
    CHECK(sameContent(doc, editor.document()));
    CHECK(doc.displayUnit == LengthUnit::Centimeter);
    CHECK(doc.nextId == editor.document().nextId);
    REQUIRE(doc.find(a));
    REQUIRE(doc.find(b));
    CHECK(doc.find(a)->extrusion->height == 0.75);
    CHECK_FALSE(doc.find(b)->extrusion.has_value());
    CHECK(objectBounds(*doc.find(a)).size().x == doctest::Approx(2.0));
    REQUIRE(loaded->view);
    CHECK(loaded->view->position == view.position);

    // Reopen into a fresh session: history starts empty, editing continues on the same IDs.
    Editor reopened;
    ToolController tools2(reopened);
    reopened.resetDocument(doc);
    CHECK_FALSE(reopened.isDirty());
    CHECK_FALSE(reopened.canUndo());
    tools2.setTool(Tool::PushPull);
    tools2.pointerDown(downAt(reopened, 1.0, 0.5, {100, 100}));
    CHECK(reopened.selection()->face == FaceId::cap());
    tools2.pointerMove(levelAt(1.0, 0.5, 1.0, {100, 40}));
    tools2.pointerUp(levelAt(1.0, 0.5, 1.0, {100, 40}));
    CHECK(reopened.document().find(a)->extrusion->height == doctest::Approx(1.0));
    REQUIRE_FALSE(tools2.setTotalHeight(b, "50").has_value()); // cm: display unit survived
    CHECK(reopened.document().find(b)->extrusion->height == doctest::Approx(0.5));
    const ObjectId c = test::makeBox(reopened, tools2, "100, 100", nullptr, {8.0, 8.0});
    CHECK(c.value == editor.document().nextId); // ID allocator survived
    CHECK(reopened.isDirty());
    reopened.undo();
    reopened.undo();
    reopened.undo();
    CHECK_FALSE(reopened.isDirty());

    // Saving over an existing file replaces it atomically and leaves no temp files.
    REQUIRE_FALSE(writeFileAtomically(path, serializeProject(reopened.document(), std::nullopt)).has_value());
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        (void)entry;
        ++files;
    }
    CHECK(files == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("serialized file matches the documented v1 schema") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "2000, 1000", "750");
    const auto text = serializeProject(editor.document(), std::nullopt);
    CHECK(text.find("\"format\": \"pullform-project\"") != std::string::npos);
    CHECK(text.find("\"version\": 1") != std::string::npos);
    CHECK(text.find("\"lengthUnit\": \"m\"") != std::string::npos);
    CHECK(text.find("\"id\": \"obj-1\"") != std::string::npos);
    CHECK(text.find("\"kind\": \"rectangle\"") != std::string::npos);
    CHECK(text.find("\"height\": 0.75") != std::string::npos);
}

TEST_CASE("files written by the earlier web version open") {
    const char* v1 = R"({
  "format": "pullform-project", "version": 1, "generator": "Pullform 0.1.0",
  "document": { "lengthUnit": "m", "displayUnit": "mm", "nextId": 3, "objects": [
    { "id": "obj-2", "type": "profile", "name": "Box 2",
      "profile": { "kind": "rectangle",
        "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
        "boundary": [[0,0],[2,0],[2,1],[0,1]] },
      "extrusion": { "height": 0.75 }, "transform": { "translation": [1, -1, 0] } } ] },
  "view": { "position": [6, -6, 5], "target": [0, 0, 0] } })";
    const auto r = parseProject(v1);
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error()));
    CHECK(r->document.objects.size() == 1);
    CHECK(r->document.objects[0]->id == ObjectId{2});
}

TEST_CASE("scenario 9: invalid files are rejected and leave the open project intact") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "2000, 1000", "750");
    const Document before = editor.document();
    const auto depth = editor.undoDepth();

    const auto good = serializeProject(editor.document(), std::nullopt);
    const auto replace = [&](std::string from, std::string to) {
        auto s = good;
        const auto at = s.find(from);
        REQUIRE(at != std::string::npos);
        s.replace(at, from.size(), to);
        return s;
    };

    const std::vector<std::pair<std::string, std::string>> cases = {
        {"not json", "{ nope"},
        {"array", "[1,2,3]"},
        {"wrong format", replace("pullform-project", "other-app")},
        {"newer version", replace("\"version\": 1", "\"version\": 2")},
        {"older version", replace("\"version\": 1", "\"version\": 0")},
        {"string version", replace("\"version\": 1", "\"version\": \"1\"")},
        {"bad length unit", replace("\"lengthUnit\": \"m\"", "\"lengthUnit\": \"mm\"")},
        {"bad display unit", replace("\"displayUnit\": \"mm\"", "\"displayUnit\": \"km\"")},
        {"bad id", replace("\"obj-1\"", "\"box-1\"")},
        {"nextId too small", replace("\"nextId\": 2", "\"nextId\": 1")},
        {"degenerate rectangle", replace("2.0,\n", "0.0,\n")},
        {"zero height", replace("\"height\": 0.75", "\"height\": 0")},
        {"negative height", replace("\"height\": 0.75", "\"height\": -1")},
        {"non-finite", replace("\"height\": 0.75", "\"height\": 1e999")},
        {"string number", replace("\"height\": 0.75", "\"height\": \"0.75\"")},
        {"unsupported kind", replace("\"kind\": \"rectangle\"", "\"kind\": \"circle\"")},
        {"rotated plane", replace("\"xAxis\": [\n              1.0", "\"xAxis\": [\n              0.5")},
        {"missing transform", replace("\"transform\"", "\"transfrm\"")},
        {"out of range", replace("\"translation\": [\n            0.0", "\"translation\": [\n            99999.0")},
    };
    for (const auto& [label, text] : cases) {
        CAPTURE(label);
        const auto r = parseProject(text);
        CHECK_FALSE(r.ok());
        if (!r.ok()) {
            CHECK_FALSE(r.error().empty());
            MESSAGE(label << ": " << r.error());
        }
    }
    const auto newer = parseProject(replace("\"version\": 1", "\"version\": 2"));
    CHECK(newer.error().find("newer") != std::string::npos);

    // Duplicate IDs.
    Document dup = editor.document();
    dup.objects.push_back(dup.objects[0]);
    dup.nextId = 5;
    CHECK_FALSE(parseProject(serializeProject(dup, std::nullopt)).ok());

    // An invalid camera view is ignored, not fatal.
    const auto badView = parseProject(good.substr(0, good.rfind('}')) + ", \"view\": {\"position\": [1, 2]}}");
    REQUIRE(badView.ok());
    CHECK_FALSE(badView->view.has_value());

    // Nothing above touched the open project.
    CHECK(sameContent(editor.document(), before));
    CHECK(editor.undoDepth() == depth);
}

TEST_CASE("reading a missing file reports an error") {
    CHECK_FALSE(readProjectFile("/definitely/not/here.pullform.json").ok());
}

TEST_CASE("project extension") {
    CHECK(withProjectExtension("house").filename() == "house.pullform.json");
    CHECK(withProjectExtension("house.pullform.json").filename() == "house.pullform.json");
    CHECK(withProjectExtension("house.JSON").filename() == "house.JSON");
}
