#pragma once

#include "app/tools/tool.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

// Helpers shared by several tools: axis locks, snapping steps and numeric entry.

/// "red", "green" or "blue": the viewport colour of a world axis.
const char* axisColorName(MoveAxis axis);

/// The axis a move runs along when exactly one component is non-zero.
std::optional<MoveAxis> singleAxis(const Vec3& offset);

/// The axis an arrow key picks (Right X, Left Y, Up Z), as in other modelers.
std::optional<MoveAxis> arrowAxis(ToolKey key);
/// The axis a letter key picks (X, Y or Z).
std::optional<MoveAxis> letterAxis(ToolKey key);

/// Axis lock toggle: locking the axis that is already locked releases it.
inline void toggleAxisLock(std::optional<MoveAxis>& lock, MoveAxis axis) {
    if (lock == axis) lock.reset();
    else lock = axis;
}

inline double snapValue(double v, double step) { return std::round(v / step) * step; }

/// `s` without leading and trailing spaces and tabs.
std::string trimmed(std::string_view s);

/// Whether the pointer travelled far enough between press and release to be a drag.
bool isDrag(const Vec2& press, const Vec2& release);

/// World-space centre of an object's base (the default rotation and the scale centre).
Vec3 baseCenter(const ProfileObject& object);

/// The objects a Move, Rotate or Scale of `primary` acts on: the whole
/// selection when `primary` is one of several selected objects, else just it.
std::vector<ObjectId> transformGroup(const Editor& editor, ObjectId primary);

/// Default centre for rotating or scaling `ids`: the object's base centre for
/// one, the base centre of the group's bounding box for several.
Vec3 groupBaseCenter(const Document& doc, const std::vector<ObjectId>& ids);

/// The objects that own the selection's vertices, edges and faces, in
/// selection order (empty when none is selected).
std::vector<ObjectId> elementOwners(const SelectionSet& selection);

/// Base centre (lowest z, centre in x and y) of the world bounding box of the
/// elements' vertices: the default rotation and scale centre for elements.
Vec3 elementsBaseCenter(const Document& doc, const std::vector<ElementRef>& elements);

/// "edge", "3 faces", "4 elements": what an element transform's undo label names.
std::string elementsLabel(const std::vector<ElementRef>& elements);

/// " (3 objects)" for a group, empty for one object: appended to undo labels.
std::string groupSuffix(std::size_t count);

} // namespace pf
