#include "core/document/project_file.hpp"

#include "core/document/validate.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pf {

using nlohmann::json;

namespace {

json vec3ToJson(const Vec3& v) { return json::array({v.x, v.y, v.z}); }
json vec2ToJson(const Vec2& v) { return json::array({v.x, v.y}); }

// Structural JSON reading. Each helper throws ParseError with a path-qualified
// message; parseProject turns that into an Error. Semantic checks (ranges,
// rectangles, IDs) are done afterwards by validateDocument, the same validation
// every committed edit goes through.
struct ParseError {
    std::string message;
};

[[noreturn]] void fail(const std::string& path, const std::string& message) { throw ParseError{path + ": " + message}; }

double number(const json& v, const std::string& path) {
    if (!v.is_number()) fail(path, "must be a finite number");
    const double d = v.get<double>();
    if (!std::isfinite(d)) fail(path, "must be a finite number");
    return d;
}

Vec3 vec3(const json& v, const std::string& path) {
    if (!v.is_array() || v.size() != 3) fail(path, "must be an array of 3 numbers");
    return {number(v[0], path + "[0]"), number(v[1], path + "[1]"), number(v[2], path + "[2]")};
}

Vec2 vec2(const json& v, const std::string& path) {
    if (!v.is_array() || v.size() != 2) fail(path, "must be an array of 2 numbers");
    return {number(v[0], path + "[0]"), number(v[1], path + "[1]")};
}

// Returns a pointer (not a reference) so GCC's -Wdangling-reference does not
// flag call sites that pass temporary path strings.
const json* member(const json& obj, const char* key, const std::string& path) {
    const auto it = obj.find(key);
    if (it == obj.end()) fail(path + "." + key, "is missing");
    return &*it;
}

ProfileObject readObject(const json& v, const std::string& path) {
    if (!v.is_object()) fail(path, "must be an object");
    ProfileObject o;
    const auto& id = *member(v, "id", path);
    if (!id.is_string()) fail(path + ".id", "must look like \"obj-1\"");
    const auto parsedId = parseObjectId(id.get<std::string>());
    if (!parsedId) fail(path + ".id", "must look like \"obj-1\"");
    o.id = *parsedId;

    const auto& type = *member(v, "type", path);
    if (!type.is_string() || type.get<std::string>() != "profile") fail(path + ".type", "unsupported object type " + type.dump());
    const auto& name = *member(v, "name", path);
    if (!name.is_string()) fail(path + ".name", "must be a string");
    o.name = name.get<std::string>();

    const auto& profile = *member(v, "profile", path);
    if (!profile.is_object()) fail(path + ".profile", "must be an object");
    const auto& kind = *member(profile, "kind", path + ".profile");
    if (!kind.is_string() || kind.get<std::string>() != "rectangle") {
        fail(path + ".profile.kind", "unsupported profile kind " + kind.dump());
    }
    o.profile.kind = ProfileKind::Rectangle;
    const auto& plane = *member(profile, "plane", path + ".profile");
    if (!plane.is_object()) fail(path + ".profile.plane", "must be an object");
    o.profile.plane.origin = vec3(*member(plane, "origin", path + ".profile.plane"), path + ".profile.plane.origin");
    o.profile.plane.xAxis = vec3(*member(plane, "xAxis", path + ".profile.plane"), path + ".profile.plane.xAxis");
    o.profile.plane.yAxis = vec3(*member(plane, "yAxis", path + ".profile.plane"), path + ".profile.plane.yAxis");
    const auto& boundary = *member(profile, "boundary", path + ".profile");
    if (!boundary.is_array()) fail(path + ".profile.boundary", "must be an array");
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        o.profile.boundary.push_back(vec2(boundary[i], path + ".profile.boundary[" + std::to_string(i) + "]"));
    }

    const auto& extrusion = *member(v, "extrusion", path);
    if (!extrusion.is_null()) {
        if (!extrusion.is_object()) fail(path + ".extrusion", "must be null or an object");
        o.extrusion = Extrusion{number(*member(extrusion, "height", path + ".extrusion"), path + ".extrusion.height")};
    }
    const auto& transform = *member(v, "transform", path);
    if (!transform.is_object()) fail(path + ".transform", "must be an object");
    o.transform.translation = vec3(*member(transform, "translation", path + ".transform"), path + ".transform.translation");
    return o;
}

Document readDocument(const json& v) {
    if (!v.is_object()) fail("document", "must be an object");
    const auto& lengthUnit = *member(v, "lengthUnit", "document");
    if (!lengthUnit.is_string() || lengthUnit.get<std::string>() != "m") fail("document.lengthUnit", "must be \"m\"");
    Document doc;
    const auto& displayUnit = *member(v, "displayUnit", "document");
    std::optional<LengthUnit> unit;
    if (displayUnit.is_string()) {
        const auto s = displayUnit.get<std::string>();
        if (s != "\"" && s != "'") unit = unitFromSuffix(s);
    }
    if (!unit) fail("document.displayUnit", "must be one of mm, cm, m, in, ft");
    doc.displayUnit = *unit;
    const auto& nextId = *member(v, "nextId", "document");
    if (!nextId.is_number_integer() || nextId.get<std::int64_t>() < 1) fail("document.nextId", "must be a positive integer");
    doc.nextId = nextId.get<std::uint64_t>();
    const auto& objects = *member(v, "objects", "document");
    if (!objects.is_array()) fail("document.objects", "must be an array");
    for (std::size_t i = 0; i < objects.size(); ++i) {
        doc.objects.push_back(std::make_shared<const ProfileObject>(
            readObject(objects[i], "document.objects[" + std::to_string(i) + "]")));
    }
    return doc;
}

std::optional<CameraView> readView(const json& file) {
    // Camera state is optional and non-essential: an invalid view is ignored.
    const auto it = file.find("view");
    if (it == file.end() || !it->is_object()) return std::nullopt;
    try {
        CameraView view{vec3(*member(*it, "position", "view"), "view.position"), vec3(*member(*it, "target", "view"), "view.target")};
        return view;
    } catch (const ParseError&) {
        return std::nullopt;
    }
}

} // namespace

std::string serializeProject(const Document& doc, const std::optional<CameraView>& view) {
    json objects = json::array();
    for (const auto& ptr : doc.objects) {
        const auto& o = *ptr;
        json boundary = json::array();
        for (const auto& p : o.profile.boundary) boundary.push_back(vec2ToJson(p));
        json obj = {
            {"id", idString(o.id)},
            {"type", "profile"},
            {"name", o.name},
            {"profile",
             {{"kind", "rectangle"},
              {"plane",
               {{"origin", vec3ToJson(o.profile.plane.origin)},
                {"xAxis", vec3ToJson(o.profile.plane.xAxis)},
                {"yAxis", vec3ToJson(o.profile.plane.yAxis)}}},
              {"boundary", boundary}}},
            {"extrusion", o.extrusion ? json{{"height", o.extrusion->height}} : json(nullptr)},
            {"transform", {{"translation", vec3ToJson(o.transform.translation)}}},
        };
        objects.push_back(std::move(obj));
    }
    json file = {
        {"format", kProjectFormat},
        {"version", kProjectVersion},
        {"generator", std::string("Pullform ") + PULLFORM_VERSION},
        {"document",
         {{"lengthUnit", "m"},
          {"displayUnit", std::string(unitSuffix(doc.displayUnit))},
          {"nextId", doc.nextId},
          {"objects", objects}}},
    };
    if (view) file["view"] = {{"position", vec3ToJson(view->position)}, {"target", vec3ToJson(view->target)}};
    return file.dump(2) + "\n";
}

Expected<LoadedProject> parseProject(std::string_view text) {
    if (text.size() > kMaxProjectBytes) return Error{"File is too large to be a project file."};
    const json file = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (file.is_discarded()) return Error{"The file is not valid JSON, so it cannot be a Pullform project."};
    if (!file.is_object()) return Error{"The file is not a Pullform project."};
    const auto format = file.find("format");
    if (format == file.end() || !format->is_string() || format->get<std::string>() != kProjectFormat) {
        return Error{"The file is not a Pullform project (expected format \"" + std::string(kProjectFormat) + "\")."};
    }
    const auto version = file.find("version");
    if (version == file.end() || !version->is_number_integer()) return Error{"The project file has no valid version number."};
    const auto v = version->get<std::int64_t>();
    if (v != kProjectVersion) {
        if (v > kProjectVersion) {
            return Error{"This project uses file version " + std::to_string(v) +
                         ", which is newer than this app supports (version " + std::to_string(kProjectVersion) +
                         "). Update Pullform to open it."};
        }
        return Error{"Unsupported project file version " + std::to_string(v) + "."};
    }
    const auto docJson = file.find("document");
    if (docJson == file.end()) return Error{"The project file is invalid: document: is missing"};
    LoadedProject project;
    try {
        project.document = readDocument(*docJson);
    } catch (const ParseError& e) {
        return Error{"The project file is invalid: " + e.message};
    }
    if (auto e = validateDocument(project.document)) return Error{"The project file is invalid: " + *e};
    project.view = readView(file);
    return project;
}

Expected<std::string> readProjectFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return Error{"Could not read " + pathToUtf8(path.filename()) + ": " + ec.message()};
    if (size > kMaxProjectBytes) return Error{"File is too large to be a project file."};
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"Could not open " + pathToUtf8(path.filename()) + "."};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return Error{"Could not read " + pathToUtf8(path.filename()) + "."};
    return buffer.str();
}

std::optional<std::string> writeFileAtomically(const std::filesystem::path& path, std::string_view contents) {
    namespace fs = std::filesystem;
    std::random_device rd;
    const auto tmp = fs::path(path).concat(".tmp-" + std::to_string(rd() % 1000000u));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return "Could not create a temporary file next to " + pathToUtf8(path.filename()) + ".";
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            fs::remove(tmp, ignored);
            return "Could not write " + pathToUtf8(path.filename()) + " (is the disk full?).";
        }
    }
    std::error_code ec;
#ifdef _WIN32
    // Atomic replace on NTFS; write-through so the rename is durable.
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    fs::rename(tmp, path, ec); // rename(2) atomically replaces the destination
#endif
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return "Could not save " + pathToUtf8(path.filename()) + ": " + ec.message();
    }
    return std::nullopt;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path withProjectExtension(std::filesystem::path path) {
    const auto name = pathToUtf8(path.filename());
    const auto endsWith = [&](std::string_view suffix) {
        if (name.size() < suffix.size()) return false;
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(name[name.size() - suffix.size() + i])));
            if (a != suffix[i]) return false;
        }
        return true;
    };
    if (endsWith(".json")) return path;
    path += std::string(kProjectExtension);
    return path;
}

} // namespace pf
