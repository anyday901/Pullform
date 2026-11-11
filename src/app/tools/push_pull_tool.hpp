#pragma once

#include "app/tools/tool.hpp"

namespace pf {

/// Push/Pull: click a rectangle or an extrusion's top cap and move to set the
/// height (click-move-click or press-drag-release), or type a distance.
/// Commits one "Push/pull to" or "Set height to" entry.
class PushPullTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override { return submitDistance(text); }
    [[nodiscard]] std::string_view measurementLabel() const override { return "Distance (incremental)"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// Explanation when a face cannot be push/pulled in this milestone.
    static std::optional<std::string> pushableReason(const FaceId& face);

    /// Exact distance: incremental from the face's current position (for a
    /// planar face it equals the total height). Applies to the active
    /// gesture, or else to the selected object.
    std::optional<std::string> submitDistance(std::string_view distance);

    /// Sets the total extrusion height of an object (properties panel, or a
    /// typed value with the Select tool).
    std::optional<std::string> setTotalHeight(ObjectId id, std::string_view height);

private:
    struct Gesture {
        ObjectId objectId;
        FaceKind face = FaceKind::Profile;
        Vec3 anchor{0.0};
        double startHeight = 0.0;
        double baseZ = 0.0; ///< world height of the profile plane
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };

    void finish();
    void setPreview(double delta);
    std::optional<double> snapHeight(const PointerInput& input, std::optional<double> raw);
    bool commitHeight(ObjectId id, FaceKind face, double height);

    std::optional<Gesture> gesture_;
};

} // namespace pf
