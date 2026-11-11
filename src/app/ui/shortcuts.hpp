#pragma once

#include <array>
#include <utility>

// Keyboard bindings, shown in menus, tooltips and the Controls list exactly as
// handled in app.cpp. Single-key shortcuts are ignored while a text field has
// keyboard focus or while a measurement is being typed.
namespace pf::shortcuts {

#ifdef __APPLE__
#define PF_MOD "Cmd+"
#else
#define PF_MOD "Ctrl+"
#endif

inline constexpr const char* kNew = PF_MOD "N";
inline constexpr const char* kOpen = PF_MOD "O";
inline constexpr const char* kSave = PF_MOD "S";
inline constexpr const char* kSaveAs = PF_MOD "Shift+S";
inline constexpr const char* kQuit = PF_MOD "Q";
inline constexpr const char* kUndo = PF_MOD "Z";
#ifdef __APPLE__
inline constexpr const char* kRedo = "Cmd+Shift+Z";
#else
inline constexpr const char* kRedo = "Ctrl+Y";
#endif
inline constexpr const char* kDelete = "Delete";
inline constexpr const char* kDuplicate = PF_MOD "D";
inline constexpr const char* kFit = "F";
inline constexpr const char* kSelect = "Space";
inline constexpr const char* kRectangle = "R";
inline constexpr const char* kPushPull = "P";
inline constexpr const char* kMove = "M";
inline constexpr const char* kOrbit = "O";
inline constexpr const char* kSnap = "G";

inline constexpr std::array<std::pair<const char*, const char*>, 22> kTable = {{
    {"Left button", "Use the active tool"},
    {"Middle drag", "Orbit"},
    {"Shift + middle drag", "Pan"},
    {"Right drag", "Pan"},
    {"Mouse wheel", "Zoom toward the cursor"},
    {"Space", "Select tool"},
    {"R", "Rectangle tool"},
    {"P", "Push/Pull tool"},
    {"M", "Move tool"},
    {"X / Y / Z or arrows", "Move: lock to an axis (again to release)"},
#ifdef __APPLE__
    {"Option (tap)", "Move: place a copy instead (again to stop)"},
#else
    {"Ctrl (tap)", "Move: place a copy instead (again to stop)"},
#endif
    {"O", "Orbit tool (left-drag orbits)"},
    {"F", "Fit view to model"},
    {"G", "Toggle grid snap"},
    {"Type digits, Enter", "Exact size or distance for the active tool"},
    {"Esc", "Cancel the operation, or clear the selection"},
    {"Delete / Backspace", "Delete the selected object"},
    {PF_MOD "D", "Duplicate the selected object"},
    {PF_MOD "Z", "Undo"},
#ifdef __APPLE__
    {"Cmd+Shift+Z", "Redo"},
#else
    {"Ctrl+Y or Ctrl+Shift+Z", "Redo"},
#endif
    {PF_MOD "N / O / S", "New / Open / Save"},
    {PF_MOD "Shift+S", "Save As"},
}};

#undef PF_MOD

} // namespace pf::shortcuts
