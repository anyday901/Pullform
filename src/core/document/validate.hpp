#pragma once

#include "core/document/document.hpp"
#include "core/document/result.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace pf {

/// Checks a single length (width, depth or height) against the supported range.
/// Returns a user-facing message, or nothing when valid. `label` starts the message.
std::optional<std::string> checkLength(double value, std::string_view label);

struct RectangleSize {
    double width = 0.0;
    double depth = 0.0;
};

/// Checks that a boundary is the counter-clockwise rectangle [0,0],[w,0],[w,d],[0,d]
/// with valid positive dimensions, and returns its size.
Expected<RectangleSize> rectangleSize(const std::vector<Vec2>& boundary);

/// Validates one object. `path` prefixes messages (e.g. "document.objects[2]").
/// Rejects non-finite or out-of-range values, unsupported planes, degenerate
/// rectangles and zero/negative heights.
std::optional<std::string> validateObject(const ProfileObject& object, std::string_view path);

/// Validates a whole document: every object, unique IDs, and nextId greater than
/// every used ID. Used for every commit and before an opened file replaces the
/// current project.
std::optional<std::string> validateDocument(const Document& document);

} // namespace pf
