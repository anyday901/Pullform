#pragma once

namespace pf {

struct UiContext;

/// Edge and face cleanup in the interface (roadmap item 21): the Edit menu
/// items (Hide, Unhide, Reverse and Orient Faces, Flip Along, Soften/Smooth
/// Edges) and the Soften Edges window with its angle slider.
struct CleanupUi {
    bool softenOpen = false; ///< the Soften Edges window is showing
    float angleDegrees = 20.0f;
    bool smooth = true;
    bool coplanar = true;

    /// Items for the Edit menu (call between BeginMenu("Edit") and EndMenu).
    void editMenuItems(UiContext& ctx);
    /// The Soften Edges window, when open. Changes apply to the selection as
    /// soon as the slider is released or a box is ticked, one undo entry each.
    void softenWindow(UiContext& ctx);
};

} // namespace pf
