#pragma once

#include "app/tools/tool.hpp"
#include "core/commands/editor.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/snap.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace pf {

/// A pointer position resolved for measuring: the snapped world point and the
/// anchor a dimension would use there (attached to a corner or edge midpoint,
/// or free).
struct MeasurePoint {
    Vec3 point{0.0};
    DimensionAnchor anchor;
};

/// Tape measure and dimension tools (one instance per mode). Each pointer
/// event is snapped with ToolContext::measureSnap.
///
/// Tape: click a first point, then a second; the distance and its X, Y and Z
/// parts show in the measurements box and the status bar. Nothing enters the
/// document. A third click starts a new measurement.
///
/// Dimension: click two points, move to pull the dimension line away from
/// them, and click (or type the offset distance and press Enter) to place it.
/// That commits one "Add dimension" history entry. Ends placed on a corner or
/// an edge midpoint stay attached to the object.
class MeasureTool final : public ToolBase {
public:
    using Mode = MeasurePreview::Mode;

    MeasureTool(ToolContext& ctx, Mode mode) : ToolBase(ctx), mode_(mode) {}

    void activate() override { start(mode_); }
    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    /// Typed value: the dimension line offset while placing a dimension.
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override {
        return mode_ == Mode::Tape ? "Distance" : "Offset";
    }
    [[nodiscard]] bool active() const override { return start_.has_value(); }
    void cancel() override { (void)reset(); }
    [[nodiscard]] std::string hint() const override;

    void start(Mode mode);
    /// Drops the gesture. Returns whether one was in progress.
    bool reset();

    /// Resolves a snapped pointer to a measuring point. A point on a corner or
    /// an edge midpoint attaches to that object; anything else is free.
    [[nodiscard]] MeasurePoint pointAt(const SnapResult& snap) const;

private:
    /// `snap` is empty when the pointer reaches neither the model nor the ground.
    void click(const PointerInput& input, const std::optional<SnapResult>& snap);
    [[nodiscard]] bool placingLine() const { return mode_ == Mode::Dimension && fixed_ && start_ && end_; }
    void publish(const MeasurePoint& cursor);
    bool commitDimension(const Vec3& offset);

    Mode mode_ = Mode::Tape;
    std::optional<MeasurePoint> start_;
    std::optional<MeasurePoint> end_;
    bool fixed_ = false;
    std::optional<Vec3> offset_;
    MeasurePoint cursor_;
    Vec2 pressScreen_{0.0};
    bool pressed_ = false;
};

} // namespace pf
