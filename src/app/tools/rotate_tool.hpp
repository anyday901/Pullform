#pragma once

#include "app/tools/tool.hpp"
#include "app/tools/tool_support.hpp"

#include <numbers>
#include <vector>

namespace pf {

/// Rotate: pick the object, click the centre, click a reference direction,
/// then move to turn and click (or type an angle). The arrow keys pick the
/// world axis. With several objects selected, the whole selection turns about
/// the same centre. Commits one "Rotate" entry. In copy mode (tap Ctrl) a
/// turned copy is placed instead; right after it, typing "6x" makes six copies
/// at that angle (a radial array), "/6" spreads six between, and a new angle
/// re-spaces them, until the next action.
class RotateTool final : public ToolBase {
public:
    static constexpr double kSnapRadians = std::numbers::pi / 12.0; ///< 15 degrees with grid snap on

    using ToolBase::ToolBase;

    void toolChanged() override {
        copy_ = false;
        lastCopy_.reset();
    }
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

    /// Copy mode: committing places a turned copy and leaves the original; the
    /// copy becomes the selection. Ends with the commit or a tool change.
    void toggleCopy();
    [[nodiscard]] bool copy() const { return copy_; }
    /// Whether the last copy can still be edited into a radial array.
    [[nodiscard]] bool arrayEditable() const { return lastCopy_ && lastCopy_->copy.current(editor()); }

private:
    /// The last copy and the turn it was made with (the spacing, or the whole
    /// sweep for "/n").
    struct RotateCopy {
        LastCopy copy;
        Vec3 pivot{0.0};
        Vec3 axis{0.0, 0.0, 1.0};
        double angle = 0.0;
    };

    struct Gesture {
        ObjectId objectId;
        Vec3 pivot{0.0};
        Vec3 axis{0.0, 0.0, 1.0};
        std::optional<Vec3> reference;
        std::vector<ObjectId> ids; ///< every object that turns (objectId and the rest of a multi-selection)
    };

    /// One undo entry for all of `ids`.
    bool commit(const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& axis, double angle);
    /// Redoes the last copy as `array` copies of `angle`, replacing its undo entry.
    std::optional<std::string> editCopy(double angle, ArrayCount array);

    std::optional<Gesture> gesture_;
    MoveAxis axis_ = MoveAxis::Z; ///< world axis Rotate turns about
    bool copy_ = false;
    std::optional<RotateCopy> lastCopy_;
};

} // namespace pf
