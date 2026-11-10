#pragma once

#include "core/document/document.hpp"

#include <optional>

namespace pf {

/// A selection is a whole object, optionally with the targeted logical face
/// (needed for push/pull). Selection always selects and deletes whole objects.
struct Selection {
    ObjectId objectId;
    std::optional<FaceId> face;
    friend bool operator==(const Selection&, const Selection&) = default;
};

/// Drops references that no longer exist (for example after undo). A planar
/// profile's face becomes the cap when extruded, and vice versa.
std::optional<Selection> sanitizeSelection(const Document& doc, const std::optional<Selection>& selection);

} // namespace pf
