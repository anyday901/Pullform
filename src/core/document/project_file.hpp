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
inline constexpr int kProjectVersion = 1;
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
};

std::string serializeProject(const Document& document, const std::optional<CameraView>& view);

/// Parses and fully validates a project. Nothing is applied on failure, so the
/// caller keeps the current project intact. Unknown formats and versions other
/// than kProjectVersion are rejected; an invalid optional view is ignored.
Expected<LoadedProject> parseProject(std::string_view text);

/// Reads a whole file (refusing files over kMaxProjectBytes).
Expected<std::string> readProjectFile(const std::filesystem::path& path);

/// Writes `contents` to a temporary file beside `path`, flushes it, then renames
/// it over `path`, so an interrupted save never leaves a truncated project.
/// Returns an error message on failure (the original file is left untouched).
std::optional<std::string> writeFileAtomically(const std::filesystem::path& path, std::string_view contents);

/// UTF-8 text of a path (safe for non-ASCII names on Windows).
std::string pathToUtf8(const std::filesystem::path& path);

/// Appends ".pullform.json" unless the name already ends with it (or ".json").
std::filesystem::path withProjectExtension(std::filesystem::path path);

} // namespace pf
