#pragma once

#include "core/commands/operations.hpp"
#include "core/document/document.hpp"
#include "core/document/result.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Solid tools (roadmap item 20): booleans between closed solid objects. Each
// object is a group holding its own mesh (docs/GEOMETRY.md); a box, a
// cylinder, a prism or a mesh group whose mesh is closed is a solid. The
// booleans are core/geometry/solid_boolean.*. The tools are written against
// objects so groups and components (item 22) can use them unchanged.

namespace pf {

enum class SolidTool {
    Union,      ///< all selected solids become one solid
    Subtract,   ///< the first selected solid is cut out of the second, and removed
    Intersect,  ///< only what all selected solids share is left
    Trim,       ///< like Subtract, but the first solid stays
    Split,      ///< both solids are cut where they overlap: three solids
    OuterShell, ///< like Union, and any shell left inside another (a void) is removed
};

inline constexpr SolidTool kSolidTools[] = {SolidTool::Union, SolidTool::Subtract, SolidTool::Intersect,
                                            SolidTool::Trim,  SolidTool::Split,    SolidTool::OuterShell};

/// Menu name ("Outer Shell") and a one-line explanation for a tooltip.
std::string_view solidToolName(SolidTool tool);
std::string_view solidToolHelp(SolidTool tool);

/// How many objects a tool needs: Subtract, Trim and Split take exactly two;
/// Union and Intersect two or more; Outer Shell one or more.
bool solidToolAccepts(SolidTool tool, std::size_t count);

/// Why `object` is not a solid (empty when it is one).
std::optional<std::string> solidObjectProblem(const ProfileObject& object);

/// Runs a solid tool on objects in selection order (`ids` unique). The result
/// keeps the id, name and placement of the object it replaces: the first
/// object for Union, Intersect and Outer Shell, the second (the one cut) for
/// Subtract and Trim. Split keeps both and adds their overlap as a new object.
/// Objects used up are removed. The changed objects become mesh groups, and
/// dimensions attached to them are removed (their corners are renumbered).
/// OpResult::objectIds lists the resulting objects; objectId is the main one.
/// Fails, changing nothing, when an object is not a closed solid, when the
/// solids do not overlap where that matters (Subtract, Intersect, Trim,
/// Split), or when nothing would be left.
Expected<OpResult> applySolidTool(const Document& doc, SolidTool tool, const std::vector<ObjectId>& ids);

} // namespace pf
