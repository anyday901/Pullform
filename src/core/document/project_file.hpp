#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace pf {

// Native project file: application-owned, versioned UTF-8 JSON that stores the
// editable model (never meshes). Schema: docs/ARCHITECTURE.md.

inline constexpr std::string_view kProjectFormat = "pullform-project";
/// Current file version. Version 2 added transform.rotation; version 3 made every
/// object a group holding an edge/face mesh. Older files are migrated when opened
/// and saved as the current version.
inline constexpr int kProjectVersion = 4;
inline constexpr std::string_view kProjectExtension = ".pullform.json";
inline constexpr std::size_t kMaxProjectBytes = 50u * 1024u * 1024u;

/// Optional camera state. Stored beside the model, not part of undo history.
struct CameraView {
    Vec3 position{0.0};
    Vec3 target{0.0};
};

struct LoadedProject {
    Document document;
    std::optional<CameraView> view;
    int fileVersion = kProjectVersion; ///< version the file was written in (before migration)
};

std::string serializeProject(const Document& document, const std::optional<CameraView>& view);

/// Parses and fully validates a project. Nothing is applied on failure, so the
/// caller keeps the current project intact. Older versions are migrated to
/// kProjectVersion; unknown formats and newer versions are rejected. An invalid
/// optional view is ignored.
Expected<LoadedProject> parseProject(std::string_view text);

/// Reads a whole file (refusing files over kMaxProjectBytes).
Expected<std::string> readProjectFile(const std::filesystem::path& path);

/// Writes `contents` to a temporary file beside `path`, flushes it to the disk
/// (fsync / FlushFileBuffers), then renames it over `path`, so an interrupted
/// save or a power cut never leaves a truncated or empty project. An existing
/// file keeps its permissions, and a symbolic link keeps pointing at the saved
/// file (the link's target is replaced, not the link). Returns an error message
/// on failure; the original file is left untouched.
std::optional<std::string> writeFileAtomically(const std::filesystem::path& path, std::string_view contents);

/// UTF-8 text of a path (safe for non-ASCII names on Windows).
std::string pathToUtf8(const std::filesystem::path& path);

/// Gives a name chosen in the save dialog the ".pullform.json" extension:
/// "house" and "house.json" both become "house.pullform.json"; a name that
/// already ends with ".pullform.json" (any case) is kept.
std::filesystem::path withProjectExtension(std::filesystem::path path);

/// True when Save As must ask before replacing `finalPath`: the name was changed
/// after the native dialog (which only checked `chosen`) and a file already
/// exists there.
bool saveAsNeedsConfirmation(const std::filesystem::path& chosen, const std::filesystem::path& finalPath);

} // namespace pf
