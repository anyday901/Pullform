#pragma once

#include "core/document/document.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace pf {

/// Window selection (dragged left to right) selects objects entirely inside
/// the rectangle. Crossing selection (dragged right to left) also selects
/// objects the rectangle touches.
enum class RegionMode { Window, Crossing };

/// Window when the drag goes left to right (or straight up/down), else Crossing.
RegionMode regionModeForDrag(const Vec2& start, const Vec2& end);

/// A screen rectangle in viewport pixels; corners in any order.
struct ScreenRect {
    Vec2 a{0.0};
    Vec2 b{0.0};
    [[nodiscard]] Vec2 min() const;
    [[nodiscard]] Vec2 max() const;
    [[nodiscard]] bool contains(const Vec2& p) const;
};

/// Projects a world point to viewport pixels; nothing when it is behind the eye.
using ScreenProjector = std::function<std::optional<Vec2>(const Vec3&)>;

/// Whether `object` is selected by a window or crossing rectangle, judged on
/// its real corners and edges (never triangulation diagonals).
///
/// Window: every corner projects inside the rectangle (a corner behind the eye
/// never counts as inside). Crossing: the window test, or any edge meets the
/// rectangle, or the rectangle lies inside one of the object's faces on screen.
/// Edges with an end behind the eye are ignored for crossing.
bool regionSelects(const ProfileObject& object, const ScreenRect& rect, RegionMode mode, const ScreenProjector& project);

/// The objects a rectangle selects, in document order.
std::vector<ObjectId> objectsInRegion(const Document& doc, const ScreenRect& rect, RegionMode mode,
                                      const ScreenProjector& project);

} // namespace pf
