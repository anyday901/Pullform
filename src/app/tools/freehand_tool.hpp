#pragma once

#include "app/tools/tool.hpp"

#include <vector>

namespace pf {

/// Freehand: press, drag a curve and release. The stroke is smoothed into a
/// polyline of straight edges and committed as one "Freehand" entry. It is
/// drawn on the horizontal plane through the press point (the ground, or the
/// top of whatever the press snapped to); releasing back on the start closes
/// it into a loop, which makes a face.
class FreehandTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    /// Largest distance, in screen pixels, between the drawn stroke and the
    /// committed polyline.
    static constexpr double kSmoothingPx = 3.0;
    /// Used for the smoothing when the event carries no pixel size (tests): 5 mm.
    static constexpr double kFallbackTolerance = 0.005;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] bool active() const override { return stroke_.has_value(); }
    void cancel() override { stroke_.reset(); }
    [[nodiscard]] std::string hint() const override;

private:
    struct Stroke {
        std::vector<Vec3> points; ///< raw pointer points on the drawing plane
        std::optional<ObjectId> target;
        double tolerance = kFallbackTolerance; ///< smoothing tolerance (meters)
        double closeRadius = 0.0;              ///< how near the start a release closes the loop
        Vec2 pressScreen{0.0};
    };

    /// Pointer on the stroke's plane, if the ray reaches it within the model range.
    [[nodiscard]] std::optional<Vec3> planePoint(const PointerInput& input) const;
    void addPoint(const Vec3& p);
    [[nodiscard]] bool closes() const;
    void showPreview();

    std::optional<Stroke> stroke_;
};

} // namespace pf
