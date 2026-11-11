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

/// The last Move or Rotate copy, kept so that typing "5x", "/5" or a new
/// distance or angle edits it into an array (SketchUp-style) until anything
/// else happens: another edit, undo, a selection change or a tool change.
struct LastCopy {
    std::vector<ObjectId> originals; ///< what was copied
    std::size_t primaryIndex = 0;    ///< the picked object among `originals` (its copies keep the face)
    std::optional<FaceId> face;
    ArrayCount array;                ///< one plain copy until an array is typed
    std::uint64_t stateId = 0;       ///< editor state right after the copy (or its last edit)
    SelectionSet selection;          ///< the selection then: the last set of copies

    /// Nothing has happened since the copy (or its last edit).
    [[nodiscard]] bool current(const Editor& editor) const {
        return editor.stateId() == stateId && editor.selectionSet() == selection;
    }
    /// Selects the last set of copies of `r` (an array's farthest copies), the
    /// picked object's copy with its face.
    [[nodiscard]] SelectionSet lastSet(const OpResult& r) const;
    /// Records the editor's state after a successful copy or edit.
    void settle(const Editor& editor) {
        stateId = editor.stateId();
        selection = editor.selectionSet();
    }
};

/// What to tell the user when "5x" or "/5" is typed with no copy to edit.
std::string noCopyToArray(const char* tool);

/// " (3 objects)" for a group, empty for one object: appended to undo labels.
std::string groupSuffix(std::size_t count);

} // namespace pf
