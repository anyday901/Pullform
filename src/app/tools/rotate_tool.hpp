#pragma once

#include "app/tools/tool.hpp"

#include <numbers>

namespace pf {

/// Rotate: pick the object, click the centre, click a reference direction,
/// then move to turn and click (or type an angle). The arrow keys pick the
/// world axis. Commits one "Rotate" entry.
class RotateTool final : public ToolBase {
public:
    static constexpr double kSnapRadians = std::numbers::pi / 12.0; ///< 15 degrees with grid snap on

    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    bool key(ToolKey key) override;
    std::optional<std::string> submit(std::string_view text) override { return submitAngle(text); }
    [[nodiscard]] std::string_view measurementLabel() const override { return "Angle (degrees)"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// Rotation axis. Changing it during a gesture keeps the centre and asks
    /// for a new reference direction. It persists across tool changes.
    void setAxis(MoveAxis axis);
    [[nodiscard]] MoveAxis axis() const { return axis_; }

    /// Exact rotation in degrees. Turns the active gesture's object about its
    /// centre, or else the selected object about the centre of its base.
    /// Positive angles turn counter-clockwise looking down the axis; once a
    /// reference direction is set, the typed angle follows the pointer's
    /// direction instead.
    std::optional<std::string> submitAngle(std::string_view angle);

private:
    struct Gesture {
        ObjectId objectId;
        Vec3 pivot{0.0};
        Vec3 axis{0.0, 0.0, 1.0};
        std::optional<Vec3> reference;
    };

    bool commit(ObjectId id, const Vec3& pivot, const Vec3& axis, double angle);

    std::optional<Gesture> gesture_;
    MoveAxis axis_ = MoveAxis::Z; ///< world axis Rotate turns about
};

} // namespace pf
