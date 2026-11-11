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

TEST_CASE("serialized file matches the documented v3 schema") {
    Editor editor;
    ToolController tools(editor);
    test::makeBox(editor, tools, "2000, 1000", "750");
    const auto text = serializeProject(editor.document(), std::nullopt);
    CHECK(text.find("\"format\": \"pullform-project\"") != std::string::npos);
    CHECK(text.find("\"version\": 3") != std::string::npos);
    CHECK(text.find("\"type\": \"group\"") != std::string::npos);
    CHECK(text.find("\"mesh\": {") != std::string::npos);
    CHECK(text.find("\"rotation\": [") != std::string::npos);
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
    CHECK(r->fileVersion == 1);
    CHECK(r->document.objects.size() == 1);
    CHECK(r->document.objects[0]->id == ObjectId{2});
    // Migrated to version 2: identity rotation, same placement.
    CHECK(r->document.objects[0]->transform.rotation == Quat{1.0, 0.0, 0.0, 0.0});
    CHECK(r->document.objects[0]->transform.translation == Vec3{1.0, -1.0, 0.0});
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
        {"newer version", replace("\"version\": 3", "\"version\": 4")},
        {"older version", replace("\"version\": 3", "\"version\": 0")},
        {"string version", replace("\"version\": 3", "\"version\": \"3\"")},
        {"missing rotation", replace("\"rotation\"", "\"rotatio\"")},
        {"rotation not unit", replace("\"rotation\": [\n            0.0", "\"rotation\": [\n            2.0")},
        {"rotation wrong size", replace("\"rotation\": [", "\"rotation\": [1.0, ")},
        {"rotation not a number", replace("\"rotation\": [\n            0.0", "\"rotation\": [\n            null")},
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
    const auto newer = parseProject(replace("\"version\": 3", "\"version\": 4"));
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

namespace {

std::string readAll(const std::filesystem::path& path) {
    const auto text = readProjectFile(path);
    REQUIRE(text.ok());
    return *text;
}

/// Files in `dir` whose name contains ".tmp-" (leftover temporary files).
int leftoverTempFiles(const std::filesystem::path& dir) {
    int n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (pathToUtf8(entry.path().filename()).find(".tmp-") != std::string::npos) ++n;
    }
    return n;
}

} // namespace

TEST_CASE("project extension") {
    CHECK(withProjectExtension("house").filename() == "house.pullform.json");
    CHECK(withProjectExtension("house.pullform.json").filename() == "house.pullform.json");
    CHECK(withProjectExtension("house.PullForm.JSON").filename() == "house.PullForm.JSON");
    // The save dialog filters on "*.json", so typing "house" can come back as
    // "house.json". Save As must never produce a bare ".json" name.
    CHECK(withProjectExtension("house.json").filename() == "house.pullform.json");
    CHECK(withProjectExtension("house.JSON").filename() == "house.pullform.json");
    CHECK(withProjectExtension("my.house").filename() == "my.house.pullform.json");
    CHECK(withProjectExtension(std::filesystem::path("dir") / "house.json") ==
          std::filesystem::path("dir") / "house.pullform.json");
    const std::u8string accented = u8"maison \u00e9t\u00e9";
    CHECK(withProjectExtension(std::filesystem::path(accented + u8".json")).filename() ==
          std::filesystem::path(accented + u8".pullform.json"));
}

TEST_CASE("Save As asks before replacing a file the dialog did not check") {
    const auto dir = tempDir();
    const auto existing = dir / "house.pullform.json";
    REQUIRE_FALSE(writeFileAtomically(existing, "old").has_value());

    // The dialog returned the final name, so its own overwrite prompt covered it.
    CHECK_FALSE(saveAsNeedsConfirmation(existing, existing));
    // "house" or "house.json" became an existing "house.pullform.json": ask.
    CHECK(saveAsNeedsConfirmation(dir / "house", existing));
    CHECK(saveAsNeedsConfirmation(dir / "house.json", existing));
    // Nothing to replace: no question.
    CHECK_FALSE(saveAsNeedsConfirmation(dir / "barn", dir / "barn.pullform.json"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("durable save writes, replaces and cleans up") {
    const auto dir = tempDir();
    const auto file = dir / "house.pullform.json";

    REQUIRE_FALSE(writeFileAtomically(file, "first").has_value());
    CHECK(readAll(file) == "first");
    REQUIRE_FALSE(writeFileAtomically(file, "second, longer contents").has_value());
    CHECK(readAll(file) == "second, longer contents");
    REQUIRE_FALSE(writeFileAtomically(file, "").has_value());
    CHECK(readAll(file).empty());
    CHECK(leftoverTempFiles(dir) == 0);

    // Non-ASCII names work.
    const auto named = dir / std::filesystem::path(u8"\u00e9t\u00e9 \u5bb6.pullform.json");
    const auto namedError = writeFileAtomically(named, "x");
    INFO(namedError.value_or(""));
    REQUIRE_FALSE(namedError.has_value());
    CHECK(readAll(named) == "x");

    // A folder that does not exist is reported, not thrown.
    const auto missing = writeFileAtomically(dir / "no-such-folder" / "a.pullform.json", "x");
    REQUIRE(missing.has_value());
    CHECK(missing->find("a.pullform.json") != std::string::npos);
    CHECK(leftoverTempFiles(dir) == 0);
    std::filesystem::remove_all(dir);
}

#ifndef _WIN32
TEST_CASE("durable save keeps permissions and symbolic links") {
    namespace fs = std::filesystem;
    const auto dir = tempDir();
    const auto file = dir / "house.pullform.json";
    REQUIRE_FALSE(writeFileAtomically(file, "first").has_value());

    // A file the user made private stays private after a save.
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    REQUIRE_FALSE(writeFileAtomically(file, "second").has_value());
    CHECK((fs::status(file).permissions() & fs::perms::all) == (fs::perms::owner_read | fs::perms::owner_write));

    // Saving through a link updates the file it points at and keeps the link.
    const auto link = dir / "link.pullform.json";
    fs::create_symlink("house.pullform.json", link);
    REQUIRE_FALSE(writeFileAtomically(link, "through the link").has_value());
    CHECK(fs::is_symlink(fs::symlink_status(link)));
    CHECK(readAll(file) == "through the link");

    // A link to a missing file creates that file.
    const auto dangling = dir / "dangling.pullform.json";
    fs::create_symlink("target.pullform.json", dangling);
    REQUIRE_FALSE(writeFileAtomically(dangling, "new").has_value());
    CHECK(fs::is_symlink(fs::symlink_status(dangling)));
    CHECK(readAll(dir / "target.pullform.json") == "new");
    CHECK(leftoverTempFiles(dir) == 0);
    fs::remove_all(dir);
}
#endif
