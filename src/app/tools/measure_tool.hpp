#pragma once

#include "core/commands/editor.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/snap.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace pf {

struct PointerInput;

/// A pointer position resolved for measuring: the snapped world point and the
/// anchor a dimension would use there (attached to a corner or edge midpoint,
/// or free).
struct MeasurePoint {
    Vec3 point{0.0};
    DimensionAnchor anchor;
};

/// Tape measure and dimension tools. Driven by ToolController, which snaps
/// each pointer event (ToolController::snapPointer) and passes the result in.
///
/// Tape: click a first point, then a second; the distance and its X, Y and Z
/// parts show in the measurements box and the status bar. Nothing enters the
/// document. A third click starts a new measurement.
///
/// Dimension: click two points, move to pull the dimension line away from
/// them, and click (or type the offset distance and press Enter) to place it.
/// That commits one "Add dimension" history entry. Ends placed on a corner or
/// an edge midpoint stay attached to the object.
class MeasureTool {
public:
    using Mode = MeasurePreview::Mode;

    explicit MeasureTool(Editor& editor) : editor_(editor) {}

    void start(Mode mode);
    /// Drops the gesture (Escape or tool change). Returns whether one was in progress.
    bool cancel();
    [[nodiscard]] bool active() const { return start_.has_value(); }

    void pointerDown(const PointerInput& input, const SnapResult& snap);
    void pointerMove(const PointerInput& input, const SnapResult& snap);
    void pointerUp(const PointerInput& input, const SnapResult& snap);

    /// Typed value: the dimension line offset while placing a dimension.
    std::optional<std::string> submit(std::string_view text);

    [[nodiscard]] std::string hint() const;

    /// Resolves a snapped pointer to a measuring point. A point on a corner or
    /// an edge midpoint attaches to that object; anything else is free.
    [[nodiscard]] MeasurePoint pointAt(const SnapResult& snap) const;

private:
    void click(const PointerInput& input, const SnapResult& snap);
    void publish(const MeasurePoint& cursor);
    bool commitDimension(const Vec3& offset);

    Editor& editor_;
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
