#pragma once

#include "app/tools/drawing_plane.hpp"
#include "app/tools/tool.hpp"

namespace pf {

/// Rectangle on the face under the pointer (tilted and vertical faces too) or
/// the ground; the arrow keys lock a vertical or horizontal plane (see
/// DrawingPlane), and the plane stays fixed once the first corner is placed.
///  - Corners mode: click (or press-drag-release) two opposite corners, or
///    type "width, depth". The sides follow the plane's axes.
///  - Rotated mode (three clicks): the first corner, the end of the first
///    side (any direction in the plane), then the depth across it. Type the
///    side's length after the first click and the depth after the second.
/// Commits one "Draw rectangle" entry.
class RectangleTool final : public ToolBase {
public:
    enum class Mode { Corners, Rotated };

    explicit RectangleTool(ToolContext& ctx, Mode mode = Mode::Corners) : ToolBase(ctx), mode_(mode) {}

    void toolChanged() override { plane_.end(); }
    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    bool key(ToolKey key) override;
    std::optional<std::string> submit(std::string_view text) override { return submitSize(text); }
    [[nodiscard]] std::string_view measurementLabel() const override;
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override {
        gesture_.reset();
        plane_.end();
    }
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] std::optional<Vec3> groundSkip() const override {
        return gesture_ ? std::optional(gesture_->start) : std::nullopt;
    }

    /// Exact size. `depth` empty means `width` holds "w, d". With a first
    /// corner placed, the size is drawn from it toward the pointer on the
    /// drawing plane; otherwise from the origin on the ground. In Rotated
    /// mode a single value after the first click is the first side's length
    /// (toward the pointer), and after the second click the depth.
    std::optional<std::string> submitSize(std::string_view width, std::string_view depth = {});

    [[nodiscard]] const DrawingPlane& drawingPlane() const { return plane_; }

private:
    struct Gesture {
        Vec3 start{0.0};
        Vec2 pressScreen{0.0};
        bool pressed = false;
        std::optional<Vec3> side; ///< Rotated: the end of the first side, once placed
        Vec3 pointer{0.0};        ///< last pointer point on the plane
    };

    void begin(const PointerInput& input);
    void update(const Vec3& p);
    void finish(const PointerInput& input);
    void placeSide(const Vec3& end);
    [[nodiscard]] Vec3 normal() const;
    /// Unit direction of the first side toward `p` (the plane's x axis when `p` is on the start).
    [[nodiscard]] Vec3 sideDirection(const Vec3& p) const;
    /// Rotated mode: the signed depth of `p` across the first side.
    [[nodiscard]] double depthAt(const Vec3& p) const;
    bool commit(const RectanglePlacement& placement);

    Mode mode_;
    DrawingPlane plane_;
    std::optional<Gesture> gesture_;
    std::optional<PointerInput> lastInput_;
};

} // namespace pf
