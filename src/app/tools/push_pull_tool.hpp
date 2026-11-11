#pragma once

#include "app/tools/tool.hpp"

namespace pf {

/// Push/Pull: click any planar face and move to set the distance
/// (click-move-click or press-drag-release), or type it. A rectangle's face
/// or a box's top cap changes the profile's height; any other face (a box's
/// sides and base, faces drawn on a wall) is pushed through the mesh (see
/// mesh::Mesh::pushPull), so a face drawn on a wall makes a recess or, pushed
/// all the way through, an opening. Tapping Ctrl (Option on macOS) toggles new
/// starting face mode; double-clicking a face repeats the last distance.
/// Commits one "Push/pull to", "Set height to" or "Push/pull" entry.
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
    void toolChanged() override { newFace_ = false; }
    [[nodiscard]] std::string hint() const override;

    /// New starting face mode (tap Ctrl, or Option on macOS): no wall stretches,
    /// a new face is made and the old outline stays as edges. Holding Ctrl
    /// while clicking the face does the same for one push/pull.
    void toggleNewFace();
    [[nodiscard]] bool newFace() const { return newFace_; }
    /// The distance of the last push/pull, which a double-click repeats.
    [[nodiscard]] std::optional<double> lastDistance() const { return lastDistance_; }

    /// Explanation when a face cannot be push/pulled (none are refused up front any more).
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
        double baseZ = 0.0; ///< world height of the profile plane (of the picked point for a drawn face)
        Vec2 pressScreen{0.0};
        bool pressed = false;
        std::uint32_t meshFace = 0;    ///< the drawn face (FaceKind::Mesh)
        Vec3 normal{0.0, 0.0, 1.0};    ///< world normal of the drawn face
        bool newFace = false;          ///< Ctrl: make a new starting face
    };

    void finish();
    void setPreview(double delta);
    std::optional<double> snapHeight(const PointerInput& input, std::optional<double> raw);
    bool commitHeight(ObjectId id, FaceKind face, double height);
    void moveMeshFace(const PointerInput& input);
    bool commitMeshFace(ObjectId id, std::uint32_t face, double distance, bool newFace);
    void repeatLast();

    std::optional<Gesture> gesture_;
    bool newFace_ = false;
    std::optional<double> lastDistance_;
};

} // namespace pf
