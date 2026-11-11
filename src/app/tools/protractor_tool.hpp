#pragma once

#include "app/tools/tool.hpp"

#include <numbers>
#include <optional>

namespace pf {

/// Protractor: adds an angled construction guide line. Click the centre (the
/// protractor lies on the face under the pointer, or flat on the ground), click
/// a point on the baseline the angle is measured from, then move to turn and
/// click, or type an angle in degrees. The guide line passes through the centre
/// at that angle from the baseline: one "Add guide" history entry.
///
/// The arrow keys lock the protractor plane square to a world axis (Right X,
/// Left Y, Up Z; again to release). With grid snap on the angle moves in
/// 15-degree steps; snapping the pointer to a corner, midpoint or guide point
/// aims the guide exactly through it instead.
class ProtractorTool final : public ToolBase {
public:
    static constexpr double kSnapRadians = std::numbers::pi / 12.0; ///< 15 degrees with grid snap on

    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    bool key(ToolKey key) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override { return "Angle (degrees)"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// Locks the protractor plane square to `axis`, or releases the lock when
    /// that axis is already locked. During a gesture the centre stays and a
    /// new baseline is asked for.
    void toggleAxis(MoveAxis axis);
    [[nodiscard]] const std::optional<MoveAxis>& axisLock() const { return axis_; }

private:
    struct Gesture {
        Vec3 center{0.0};
        Vec3 normal{0.0, 0.0, 1.0};
        std::optional<Vec3> reference; ///< baseline direction from the centre
        double angle = 0.0;            ///< current angle from the baseline
    };

    /// The plane normal for a centre picked with `input`.
    [[nodiscard]] Vec3 planeNormal(const PointerInput& input) const;
    /// Where the pointer is in the gesture's plane: a corner, midpoint or
    /// guide point projected onto it, else the ray's hit. `exact` tells which.
    [[nodiscard]] std::optional<Vec3> planePoint(const PointerInput& input, bool& exact);
    /// Updates the angle from the pointer and publishes the preview.
    void track(const PointerInput& input);
    void publish(const std::optional<Vec3>& current);
    bool commit(double angle);

    std::optional<Gesture> gesture_;
    std::optional<MoveAxis> axis_;
};

/// Unit direction at `angle` radians from `reference`, counter-clockwise about
/// unit `normal` (the protractor's guide direction).
Vec3 protractorDirection(const Vec3& reference, const Vec3& normal, double angle);

} // namespace pf
