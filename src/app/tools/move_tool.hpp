#pragma once

#include "app/tools/tool.hpp"
#include "app/tools/tool_support.hpp"

#include <vector>

namespace pf {

/// Move: click an object and move it (click-move-click or press-drag-release),
/// or type a distance or "x, y, z". Clicking one of several selected objects
/// moves (or copies) the whole selection. X, Y, Z or the arrow keys lock an axis;
/// copy mode places a duplicate instead. One undo entry per move. Right after
/// a copy, typing "5x" makes five copies at that spacing, "/5" spreads five
/// between, and a new distance re-spaces them, until the next action.
class MoveTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void toolChanged() override {
        axisLock_.reset();
        copy_ = false;
        lastCopy_.reset();
    }
    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    bool key(ToolKey key) override;
    std::optional<std::string> submit(std::string_view text) override { return submitDistance(text); }
    [[nodiscard]] std::string_view measurementLabel() const override { return "Distance, or x, y, z"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// Axis lock. Locking the axis that is already locked releases it. The
    /// lock lasts until the move is committed or the tool changes, so it can
    /// be set before the first click.
    void toggleAxisLock(MoveAxis axis);
    [[nodiscard]] const std::optional<MoveAxis>& axisLock() const { return axisLock_; }

    /// Copy mode: committing places a duplicate at the destination and leaves
    /// the original; the copy becomes the selection. Lasts like the axis lock.
    void toggleCopy();
    [[nodiscard]] bool copy() const { return copy_; }

    /// Exact move. A single length moves along the locked axis or, while a move
    /// is in progress without a lock, along the current pointer direction.
    /// "x, y, z" moves by that vector. Applies to the active gesture, or else
    /// to the selected object.
    std::optional<std::string> submitDistance(std::string_view distance);

    /// Whether the last copy can still be edited into an array.
    [[nodiscard]] bool arrayEditable() const { return lastCopy_ && lastCopy_->copy.current(editor()); }

private:
    /// The last copy and the offset it was made with (the spacing, or the
    /// whole span for "/n").
    struct MoveCopy {
        LastCopy copy;
        Vec3 offset{0.0};
    };

    struct Gesture {
        ObjectId objectId;
        Vec3 anchor{0.0};           ///< picked point the guide line starts from
        Vec3 startTranslation{0.0}; ///< object translation when the gesture began
        Vec2 pressScreen{0.0};
        bool pressed = false;
        std::optional<Ray> lastRay; ///< re-evaluates the preview when the axis lock changes
        std::optional<Vec3> lastSnap; ///< snapped offset of the last pointer event
        std::vector<ObjectId> ids;  ///< every object that moves (objectId and the rest of a multi-selection)
    };

    void finish();
    /// `snapped`: an offset the inference engine found (see snappedOffset);
    /// it wins over the free or grid move unless an axis is locked.
    void updatePreview(const Ray& ray, std::optional<Vec3> snapped = std::nullopt);
    [[nodiscard]] std::optional<Vec3> offsetFor(const Ray& ray) const;
    /// The offset that puts the picked point on an inferred point: a corner,
    /// midpoint, edge or intersection, or a line from the picked point along
    /// an axis or an edge. Shows the marker; nothing when there is none.
    std::optional<Vec3> snappedOffset(const PointerInput& input);
    [[nodiscard]] MovePreview preview(const Vec3& offset, std::optional<std::string> error) const;
    /// First reason the offset cannot be applied to every moving object.
    [[nodiscard]] std::optional<std::string> checkGroup(const std::vector<ObjectId>& ids, const Vec3& offset) const;
    /// Every move (mouse or typed) commits here: one undo entry for all of
    /// `ids`. `primary` is the object the gesture picked (its copy keeps the face).
    bool commit(const std::vector<ObjectId>& ids, ObjectId primary, const Vec3& offset);
    /// Redoes the last copy as `array` copies of `offset`, replacing its undo entry.
    std::optional<std::string> editCopy(const Vec3& offset, ArrayCount array);
    /// "5x" or "/5": edits the last copy into an array.
    std::optional<std::string> submitArray(const Expected<ArrayCount>& array);
    /// A typed "x, y, z", or a length along `direction` (an error without one).
    /// Never zero.
    [[nodiscard]] Expected<Vec3> typedOffset(std::string_view text, const std::optional<Vec3>& direction) const;

    std::optional<Gesture> gesture_;
    std::optional<MoveAxis> axisLock_;
    bool copy_ = false;
    std::optional<MoveCopy> lastCopy_;
};

} // namespace pf
