#pragma once

#include "app/tools/tool.hpp"
#include "core/geometry/arc.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

/// The Arc tools, one instance per mode. Every arc is drawn into the edge/face
/// mesh with drawArc (one "Draw arc" or "Draw pie" undo entry), so an arc that
/// closes a loop of edges makes a face.
///
/// - TwoPoint (A): click the start, click the end, then move to set the bulge
///   and click. Starting from the open end of a line or arc, the bulge snaps
///   to the tangent arc (shown cyan); double-clicking the end point takes it
///   right away. The bulge also snaps to a half circle.
/// - ThreePoint: click the start, a point the arc passes through, and the end.
/// - Pie: click the centre, the start (sets the radius), then the end angle;
///   draws the arc and both radii, which close into a face.
///
/// Typed values: the chord length (TwoPoint, second point), the bulge (third
/// step), the radius (Pie, second point), the angle in degrees (Pie, third
/// step), and at any time "12s" for the number of segments.
///
/// The drawing plane is the face the first point is on, or else the
/// horizontal plane through it.
class ArcTool final : public ToolBase {
public:
    enum class Mode { TwoPoint, ThreePoint, Pie };
    ArcTool(ToolContext& ctx, Mode mode) : ToolBase(ctx), mode_(mode) {}

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override;
    [[nodiscard]] bool active() const override { return !points_.empty(); }
    void cancel() override;
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] std::optional<Vec3> groundSkip() const override {
        return points_.empty() ? std::nullopt : std::optional(points_.front());
    }

    /// Segments for the next arc; 0 means the default for its sweep (24 per turn).
    [[nodiscard]] int segments() const { return segments_; }
    /// Whether the arc being placed starts tangent to an edge end.
    [[nodiscard]] bool hasTangent() const { return tangent_.has_value(); }

private:
    struct Plane {
        Vec3 origin{0.0};
        Vec3 normal{0.0, 0.0, 1.0};
    };

    /// The pointer on the drawing plane (snapped where possible), and shows the marker.
    std::optional<Vec3> planePoint(const PointerInput& input);
    void start(const PointerInput& input);
    /// The arc for the current points and `cursor`, with snaps applied, or why there is none.
    Expected<ArcGeometry> arcFor(const Vec3& cursor, const PointerInput* input, bool* tangent, bool* half);
    void updatePreview(const Vec3& cursor, const PointerInput* input);
    [[nodiscard]] int segmentsFor(const ArcGeometry& arc) const;
    bool commit(const ArcGeometry& arc);
    void reset();

    Mode mode_;
    Plane plane_;
    std::vector<Vec3> points_;    ///< placed points (start / centre first)
    std::optional<Vec3> tangent_; ///< direction arriving at the start, when it is an open edge end
    std::optional<Vec3> cursor_;  ///< last pointer point on the plane
    double sweep_ = 0.0;          ///< Pie: running sweep, so it can pass half a turn
    int segments_ = 0;
};

} // namespace pf
