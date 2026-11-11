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
/// parts show in the measurements box and the status bar. A third click
/// starts a new measurement. With guide creation on (the default; tapping
/// Ctrl, or Option on macOS, toggles it) the tape also adds construction guides,
/// one "Add guide" history entry each:
///  - started on an edge or a guide line (not on its ends or midpoint), the
///    second click (or a typed distance) adds a guide line parallel to it
///    through the pointer, at that perpendicular distance;
///  - started on any other point, the second click (or a typed distance along
///    the pointer's direction) adds a guide point there, unless it lands on a
///    corner, midpoint or guide point that is already there.
/// With guide creation off the tape only measures and nothing enters the document.
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
    /// Typed value: the dimension line offset while placing a dimension, or
    /// the guide distance for the tape measure.
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override {
        return mode_ == Mode::Tape ? "Distance" : "Offset";
    }
    [[nodiscard]] bool active() const override { return start_.has_value(); }
    void cancel() override { (void)reset(); }
    [[nodiscard]] std::string hint() const override;

    void start(Mode mode);
    /// Tape measure guide creation (Ctrl tap). Persists across tool changes.
    void toggleGuides();
    [[nodiscard]] bool guides() const { return guides_; }
    /// Drops the gesture. Returns whether one was in progress.
    bool reset();

    /// Resolves a snapped pointer to a measuring point. A point on a corner or
    /// an edge midpoint attaches to that object; anything else is free.
    [[nodiscard]] MeasurePoint pointAt(const SnapResult& snap) const;

private:
    /// A line the tape started on (an edge or a guide line): the second point
    /// places a parallel guide line.
    struct Line {
        Vec3 point{0.0};
        Vec3 direction{1.0, 0.0, 0.0}; ///< unit
    };

    /// The guide a click at `cursor` would add, if any (see the class comment).
    [[nodiscard]] std::optional<Guide> guideAt(const MeasurePoint& cursor, SnapKind kind) const;
    /// Where the tape starts for this press: a line under the pointer (an
    /// edge or a guide line) when the snap is not on a point.
    [[nodiscard]] std::optional<Line> lineAt(const PointerInput& input, const SnapResult& snap) const;
    /// ToolContext::measureSnap, with a pointer on an edge put on the edge.
    std::optional<SnapResult> snapAt(const PointerInput& input);
    bool commitGuide(const Guide& guide);
    std::optional<std::string> submitGuideDistance(std::string_view text);
    /// `snap` is empty when the pointer reaches neither the model nor the ground.
    void click(const PointerInput& input, const std::optional<SnapResult>& snap);
    [[nodiscard]] bool placingLine() const { return mode_ == Mode::Dimension && fixed_ && start_ && end_; }
    /// The point to infer from (the first point, until the second is placed).
    [[nodiscard]] std::optional<Vec3> inferFrom() const;
    void publish(const MeasurePoint& cursor);
    bool commitDimension(const Vec3& offset);

    Mode mode_ = Mode::Tape;
    std::optional<MeasurePoint> start_;
    std::optional<MeasurePoint> end_;
    bool fixed_ = false;
    std::optional<Vec3> offset_;
    MeasurePoint cursor_;
    SnapKind cursorKind_ = SnapKind::None;
    std::optional<Line> line_; ///< the tape started on this line
    bool guides_ = true;
    Vec2 pressScreen_{0.0};
    bool pressed_ = false;
};

} // namespace pf
