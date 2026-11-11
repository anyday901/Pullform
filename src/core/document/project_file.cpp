#include "core/document/project_file.hpp"

#include "core/document/validate.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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

/// A rotation stored as a unit quaternion [x, y, z, w] (glTF order). Hand-edited
/// values like [0, 0, 0.7071, 0.7071] are accepted and normalized; anything
/// further than kRotationLengthTolerance from unit length is rejected.
Quat quaternion(const json& v, const std::string& path) {
    if (!v.is_array() || v.size() != 4) fail(path, "must be an array of 4 numbers [x, y, z, w]");
    const Quat q{number(v[3], path + "[3]"), number(v[0], path + "[0]"), number(v[1], path + "[1]"), number(v[2], path + "[2]")};
    const double len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(std::abs(len - 1.0) <= 1e-4)) fail(path, "must be a unit quaternion [x, y, z, w]");
    return q / len;
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
    o.transform.rotation = quaternion(*member(transform, "rotation", path + ".transform"), path + ".transform.rotation");
    return o;
}

DimensionAnchor readAnchor(const json& v, const std::string& path) {
    if (!v.is_object()) fail(path, "must be an object");
    DimensionAnchor a;
    const auto object = v.find("object");
    if (object == v.end()) {
        a.point = vec3(*member(v, "point", path), path + ".point");
        return a;
    }
    if (!object->is_string()) fail(path + ".object", "must look like \"obj-1\"");
    const auto id = parseObjectId(object->get<std::string>());
    if (!id) fail(path + ".object", "must look like \"obj-1\"");
    a.object = *id;
    const auto& vertices = *member(v, "vertices", path);
    if (!vertices.is_array()) fail(path + ".vertices", "must be an array of corner numbers");
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& n = vertices[i];
        if (!n.is_number_integer() || n.get<std::int64_t>() < 0 || n.get<std::int64_t>() > 1'000'000) {
            fail(path + ".vertices[" + std::to_string(i) + "]", "must be a corner number");
        }
        a.vertices.push_back(static_cast<int>(n.get<std::int64_t>()));
    }
    return a;
}

json anchorToJson(const DimensionAnchor& a) {
    if (!a.object) return {{"point", vec3ToJson(a.point)}};
    return {{"object", idString(*a.object)}, {"vertices", a.vertices}};
}

Dimension readDimension(const json& v, const std::string& path) {
    if (!v.is_object()) fail(path, "must be an object");
    Dimension d;
    const auto& id = *member(v, "id", path);
    if (!id.is_string()) fail(path + ".id", "must look like \"dim-1\"");
    const auto parsed = parseDimensionId(id.get<std::string>());
    if (!parsed) fail(path + ".id", "must look like \"dim-1\"");
    d.id = *parsed;
    d.a = readAnchor(*member(v, "a", path), path + ".a");
    d.b = readAnchor(*member(v, "b", path), path + ".b");
    d.offset = vec3(*member(v, "offset", path), path + ".offset");
    return d;
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
    // Optional: files without annotations omit it. Version 1 files written while
    // dimensions were new may carry it too; the v1 -> v2 migration keeps it as is.
    if (const auto dims = v.find("dimensions"); dims != v.end()) {
        if (!dims->is_array()) fail("document.dimensions", "must be an array");
        for (std::size_t i = 0; i < dims->size(); ++i) {
            doc.dimensions.push_back(readDimension((*dims)[i], "document.dimensions[" + std::to_string(i) + "]"));
        }
    }
    return doc;
}

// ---- Migrations ---------------------------------------------------------------
//
// Each migration rewrites a parsed file from version N to N + 1 in place. They
// work on JSON, before any reading, so the reader only ever knows the current
// schema. Only the parts a migration needs are checked here; everything else is
// checked by the reader and validateDocument afterwards.

/// Version 1 -> 2: objects gain transform.rotation. Version 1 transforms were
/// translation only, so every object gets the identity rotation [0, 0, 0, 1]
/// (a "rotation" key in a version 1 file never had a meaning and is replaced).
void migrateV1ToV2(json& file) {
    const auto doc = file.find("document");
    if (doc == file.end() || !doc->is_object()) return; // reported by the reader
    const auto objects = doc->find("objects");
    if (objects == doc->end() || !objects->is_array()) return;
    for (std::size_t i = 0; i < objects->size(); ++i) {
        json& object = (*objects)[i];
        if (!object.is_object()) continue;
        const auto transform = object.find("transform");
        if (transform == object.end() || !transform->is_object()) continue;
        (*transform)["rotation"] = json::array({0.0, 0.0, 0.0, 1.0});
    }
}

void migrateProject(json& file, int fromVersion) {
    switch (fromVersion) {
    case 1: migrateV1ToV2(file); break;
    default: fail("version", "no migration from version " + std::to_string(fromVersion));
    }
    file["version"] = fromVersion + 1;
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
            {"transform",
             {{"translation", vec3ToJson(o.transform.translation)},
              {"rotation", json::array({o.transform.rotation.x, o.transform.rotation.y, o.transform.rotation.z,
                                        o.transform.rotation.w})}}},
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
    if (!doc.dimensions.empty()) {
        json dims = json::array();
        for (const auto& d : doc.dimensions) {
            dims.push_back({{"id", idString(d.id)}, {"a", anchorToJson(d.a)}, {"b", anchorToJson(d.b)}, {"offset", vec3ToJson(d.offset)}});
        }
        file["document"]["dimensions"] = std::move(dims);
    }
    if (view) file["view"] = {{"position", vec3ToJson(view->position)}, {"target", vec3ToJson(view->target)}};
    return file.dump(2) + "\n";
}

Expected<LoadedProject> parseProject(std::string_view text) {
    if (text.size() > kMaxProjectBytes) return Error{"File is too large to be a project file."};
    json file = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (file.is_discarded()) return Error{"The file is not valid JSON, so it cannot be a Pullform project."};
    if (!file.is_object()) return Error{"The file is not a Pullform project."};
    const auto format = file.find("format");
    if (format == file.end() || !format->is_string() || format->get<std::string>() != kProjectFormat) {
        return Error{"The file is not a Pullform project (expected format \"" + std::string(kProjectFormat) + "\")."};
    }
    const auto version = file.find("version");
    if (version == file.end() || !version->is_number_integer()) return Error{"The project file has no valid version number."};
    const auto v = version->get<std::int64_t>();
    if (v > kProjectVersion) {
        return Error{"This project uses file version " + std::to_string(v) +
                     ", which is newer than this app supports (version " + std::to_string(kProjectVersion) +
                     "). Update Pullform to open it."};
    }
    if (v < 1) return Error{"Unsupported project file version " + std::to_string(v) + "."};
    // Bring older files up to the current schema one version at a time, so each
    // migration only has to know about its neighbour.
    try {
        for (std::int64_t from = v; from < kProjectVersion; ++from) migrateProject(file, static_cast<int>(from));
    } catch (const ParseError& e) {
        return Error{"The project file is invalid: " + e.message};
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
    project.fileVersion = static_cast<int>(v);
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

namespace {

namespace fs = std::filesystem;

/// Follows a chain of symbolic links so a save replaces the file a link points
/// at, never the link itself. A dangling link resolves to the missing target.
fs::path resolveSaveTarget(const fs::path& path) {
    fs::path p = path;
    for (int hops = 0; hops < 40; ++hops) {
        std::error_code ec;
        if (!fs::is_symlink(fs::symlink_status(p, ec))) break;
        const fs::path link = fs::read_symlink(p, ec);
        if (ec) break;
        p = link.is_absolute() ? link : p.parent_path() / link;
    }
    return p;
}

/// Random part of the temporary file name. The file is always created
/// exclusively (O_EXCL / CREATE_NEW), so the name only has to make clashes rare.
std::string tempSuffix() {
    std::random_device rd;
    static constexpr char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s = ".tmp-";
    std::uniform_int_distribution<int> pick(0, 35);
    for (int i = 0; i < 8; ++i) s += kDigits[pick(rd)];
    return s;
}

#ifdef _WIN32

std::string windowsErrorText(DWORD code) { return std::system_category().message(static_cast<int>(code)); }

/// Creates `tmp` (failing if it exists), writes everything and flushes it to the disk.
std::optional<DWORD> writeAndFlushTemp(const fs::path& tmp, std::string_view contents, bool& created) {
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    created = true;
    std::optional<DWORD> err;
    std::size_t done = 0;
    while (!err && done < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(contents.size() - done, 1u << 30));
        DWORD written = 0;
        if (!WriteFile(h, contents.data() + done, chunk, &written, nullptr)) err = GetLastError();
        done += written;
    }
    if (!err && !FlushFileBuffers(h)) err = GetLastError();
    if (!CloseHandle(h) && !err) err = GetLastError();
    return err;
}

#else

/// Flushes a file descriptor to stable storage. On macOS plain fsync only
/// reaches the drive's cache; F_FULLFSYNC asks the drive to commit it.
bool syncDescriptor(int fd) {
#ifdef F_FULLFSYNC
    if (::fcntl(fd, F_FULLFSYNC) == 0) return true;
#endif
    while (::fsync(fd) != 0) {
        if (errno != EINTR) return false;
    }
    return true;
}

/// Creates `tmp` (failing if it exists) with the permissions of `existing` when
/// there is one, writes everything and flushes it to the disk.
std::optional<int> writeAndSyncTemp(const fs::path& tmp, std::string_view contents, const struct stat* existing,
                                    bool& created) {
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) return errno;
    created = true;
    std::optional<int> err;
    if (existing) {
        // Keep the replaced file's owner and mode. Changing the owner needs
        // privileges and usually fails; that is fine, the mode still applies.
        if (::fchown(fd, existing->st_uid, existing->st_gid) != 0) {
        }
        if (::fchmod(fd, existing->st_mode & 07777) != 0) err = errno;
    }
    std::size_t done = 0;
    while (!err && done < contents.size()) {
        const ssize_t n = ::write(fd, contents.data() + done, contents.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = errno;
        } else {
            done += static_cast<std::size_t>(n);
        }
    }
    if (!err && !syncDescriptor(fd)) err = errno;
    if (::close(fd) != 0 && !err) err = errno;
    return err;
}

/// Makes a rename durable by flushing the directory entry. Best effort: some
/// file systems do not support syncing a directory.
void syncDirectory(const fs::path& dir) {
    const int fd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    (void)syncDescriptor(fd);
    ::close(fd);
}

#endif

} // namespace

std::optional<std::string> writeFileAtomically(const std::filesystem::path& path, std::string_view contents) {
    const std::string name = pathToUtf8(path.filename());
    const fs::path target = resolveSaveTarget(path);
    const auto diskError = [&](const std::string& reason) {
        return "Could not save " + name + ": " + reason + ". The previous version of the file was not changed.";
    };

#ifdef _WIN32
    for (int attempt = 0; attempt < 16; ++attempt) {
        const fs::path tmp = fs::path(target).concat(tempSuffix());
        bool created = false;
        if (const auto err = writeAndFlushTemp(tmp, contents, created)) {
            if (!created && *err == ERROR_FILE_EXISTS) continue; // name collision: pick another
            if (created) DeleteFileW(tmp.c_str());
            if (!created) return "Could not create a temporary file next to " + name + ": " + windowsErrorText(*err);
            return diskError(windowsErrorText(*err));
        }
        // ReplaceFileW keeps the replaced file's security descriptor, attributes
        // and creation time. It needs an existing destination and is not
        // available on every file system, so fall back to a write-through move.
        bool replaced = false;
        DWORD err = 0;
        if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
            replaced = ReplaceFileW(target.c_str(), tmp.c_str(), nullptr,
                                    REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, nullptr, nullptr) != 0;
            if (!replaced) err = GetLastError();
        }
        if (!replaced) {
            replaced = MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!replaced && err == 0) err = GetLastError();
        }
        if (!replaced) {
            DeleteFileW(tmp.c_str());
            return diskError(windowsErrorText(err));
        }
        return std::nullopt;
    }
    return "Could not create a temporary file next to " + name + ".";
#else
    struct stat existing {};
    const bool hasExisting = ::stat(target.c_str(), &existing) == 0 && S_ISREG(existing.st_mode);
    for (int attempt = 0; attempt < 16; ++attempt) {
        const fs::path tmp = fs::path(target).concat(tempSuffix());
        bool created = false;
        if (const auto err = writeAndSyncTemp(tmp, contents, hasExisting ? &existing : nullptr, created)) {
            if (!created && *err == EEXIST) continue; // name collision: pick another
            if (created) ::unlink(tmp.c_str());
            const std::string reason = std::generic_category().message(*err);
            if (!created) return "Could not create a temporary file next to " + name + ": " + reason;
            return diskError(*err == ENOSPC ? "the disk is full" : reason);
        }
        // rename(2) atomically replaces the destination.
        if (::rename(tmp.c_str(), target.c_str()) != 0) {
            const int err = errno;
            ::unlink(tmp.c_str());
            return diskError(std::generic_category().message(err));
        }
        syncDirectory(target.parent_path());
        return std::nullopt;
    }
    return "Could not create a temporary file next to " + name + ".";
#endif
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

namespace {

bool endsWithIgnoringCase(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto a = std::tolower(static_cast<unsigned char>(text[text.size() - suffix.size() + i]));
        const auto b = std::tolower(static_cast<unsigned char>(suffix[i]));
        if (a != b) return false;
    }
    return true;
}

} // namespace

std::filesystem::path withProjectExtension(std::filesystem::path path) {
    const auto name = pathToUtf8(path.filename());
    if (endsWithIgnoringCase(name, kProjectExtension)) return path;
    if (endsWithIgnoringCase(name, ".json")) {
        // The save dialog's filter is "*.json", so a typed "house" comes back as
        // "house.json". Turn it into "house.pullform.json".
        const auto stem = name.substr(0, name.size() - std::string_view(".json").size());
        path.replace_filename(std::filesystem::path(std::u8string(stem.begin(), stem.end())));
    }
    path += std::string(kProjectExtension);
    return path;
}

bool saveAsNeedsConfirmation(const std::filesystem::path& chosen, const std::filesystem::path& finalPath) {
    // The native dialog already asked about `chosen`. Anything else is a file
    // the user never saw a warning for.
    if (finalPath == chosen) return false;
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::symlink_status(finalPath, ec));
}

} // namespace pf
