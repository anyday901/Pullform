#pragma once

#include "app/tools/tool.hpp"

namespace pf {

/// Line (L): click a start point, then click each next point to draw a chain
/// of edges. Each segment is one "Draw line" undo entry and sticks to the
/// drawn geometry it touches (core drawEdge). A segment that closes a planar
/// loop makes a face and ends the chain; so does clicking the chain's start or
/// the last point again. Escape ends the chain and keeps what was drawn.
///
/// While drawing, the pointer infers the X, Y and Z axes through the last
/// point; the arrow keys (or X, Y, Z) lock one. A typed length draws the next
/// segment that long toward the pointer (or along the locked axis); "x, y, z"
/// draws it by that offset.
class LineTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void toolChanged() override { lock_.reset(); }
    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    bool key(ToolKey key) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override { return "Length"; }
    [[nodiscard]] bool active() const override { return chain_.has_value(); }
    void cancel() override {
        chain_.reset();
        lock_.reset();
        lastInput_.reset();
    }
    [[nodiscard]] std::string hint() const override;

    /// Axis lock: locking the axis that is already locked releases it.
    void toggleAxisLock(MoveAxis axis);
    [[nodiscard]] const std::optional<MoveAxis>& axisLock() const { return lock_; }

private:
    struct Chain {
        Vec3 first{0.0};                ///< where the chain started
        Vec3 last{0.0};                 ///< the end of the last segment
        std::optional<ObjectId> group;  ///< the group the chain draws into, once it has a segment
        Vec2 pressScreen{0.0};
        bool pressed = false;
        int segments = 0;
    };
    struct Target {
        Vec3 point{0.0};
        std::optional<MoveAxis> axis; ///< the segment from the last point runs along this axis
    };

    /// The snapped point for the pointer (and the snap marker): point snaps,
    /// then the axes through the last point, then faces and the ground.
    std::optional<Target> locate(const PointerInput& input);
    void showPreview(const Target& target);
    /// Draws the segment from the last point to `end` (one undo entry). Returns
    /// false, keeping the chain, when it could not be drawn.
    bool addSegment(const Vec3& end);
    /// Ends the chain, keeping what was drawn.
    void endChain();

    std::optional<Chain> chain_;
    std::optional<MoveAxis> lock_;
    std::optional<PointerInput> lastInput_; ///< for refreshing the preview when the lock changes
};

} // namespace pf
