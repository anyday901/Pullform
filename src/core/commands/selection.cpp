#include "core/commands/selection.hpp"

#include "core/geometry/solid.hpp"

#include <algorithm>

namespace pf {

std::optional<Selection> sanitizeSelection(const Document& doc, const std::optional<Selection>& sel) {
    if (!sel) return std::nullopt;
    const auto* o = doc.find(sel->objectId);
    if (!o) return std::nullopt;
    if (!sel->face) return sel;
    const auto faces = buildFaces(*o);
    const bool exists = std::any_of(faces.begin(), faces.end(), [&](const FacePolygon& f) { return f.id == *sel->face; });
    if (exists) return sel;
    if (sel->face->kind == FaceKind::Profile && o->extrusion) return Selection{o->id, FaceId::cap()};
    if (sel->face->kind == FaceKind::Cap && !o->extrusion) return Selection{o->id, FaceId::profile()};
    return Selection{o->id, std::nullopt};
}

} // namespace pf
