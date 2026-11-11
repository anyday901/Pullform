#pragma once

#include "app/tools/drawing_plane.hpp"
#include "app/tools/tool.hpp"
#include "core/geometry/circle.hpp"

namespace pf {

/// Circle (C) and Polygon: click the center on a face or the ground (arrow keys
/// lock a plane, see DrawingPlane), then a point on the circle (or
/// press-drag-release), or type the radius. The plane is fixed by the first click. Typing a count such as
/// "24s" sets the number of segments (circle) or sides (polygon), before or
/// during drawing; the count is kept for the next one. Commits one
/// "Draw circle" or "Draw polygon" entry.
class CircleTool final : public ToolBase {
public:
    /// `kind` is ProfileKind::Circle or ProfileKind::Polygon.
    CircleTool(ToolContext& ctx, ProfileKind kind);

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    bool key(ToolKey key) override;
    void toolChanged() override { plane_.end(); }
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override;
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override {
        gesture_.reset();
        plane_.end();
    }
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] std::optional<Vec3> groundSkip() const override {
        return gesture_ ? std::optional(gesture_->center) : std::nullopt;
    }

    [[nodiscard]] ProfileKind kind() const { return kind_; }
    /// Segments (circle) or sides (polygon) the next shape gets.
    [[nodiscard]] int segments() const { return segments_; }
    [[nodiscard]] const DrawingPlane& drawingPlane() const { return plane_; }

private:
    struct Gesture {
        Vec3 center{0.0};
        Vec3 edge{0.0}; ///< last pointer point on the ground
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };

    void begin(const PointerInput& input);
    [[nodiscard]] PlaneBasis plane() const { return plane_.plane().value_or(groundPlane()); }
    void publish();
    void finish(const PointerInput& input);
    bool commit(const CirclePlacement& placement);
    [[nodiscard]] const char* noun() const { return kind_ == ProfileKind::Circle ? "circle" : "polygon"; }

    ProfileKind kind_;
    int segments_;
    std::optional<Gesture> gesture_;
    DrawingPlane plane_;
    std::optional<PointerInput> lastInput_; ///< to redraw at once when an arrow key changes the plane
};

} // namespace pf
