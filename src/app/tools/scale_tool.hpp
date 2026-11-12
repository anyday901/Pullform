#pragma once

#include "app/tools/tool.hpp"

#include <vector>

namespace pf {

/// Scale: the selection's bounding box shows grips (corners, edge midpoints
/// and face centres). Dragging a grip scales about the opposite grip (about
/// the box centre with Ctrl held at the press): a face grip along one axis, an
/// edge grip along two, a corner grip uniformly. Dragging past the opposite
/// grip, or typing a negative factor, mirrors. Clicking anywhere else on the
/// object and moving away from or toward the base centre scales uniformly
/// about that centre. Click again, or type a factor ("2", "50%", "-1") or
/// "width, depth, height" factors, to finish. With several objects selected the
/// box is around all of them; with vertices, edges or faces selected it is
/// around those, and just they scale (the connected geometry stretches). One
/// object's box follows its own axes. Commits one "Scale" entry.
class ScaleTool final : public ToolBase {
public:
    static constexpr double kSnapStep = 0.1; ///< factor steps with grid snap on
    static constexpr double kGripPx = 8.0;   ///< how close (screen pixels) the pointer must be to a grip

    using ToolBase::ToolBase;

    void activate() override { showGrips(std::nullopt); }
    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override { return submitFactors(text); }
    [[nodiscard]] std::string_view measurementLabel() const override { return "Scale (factor, or w, d, h)"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// Exact scale: one factor or "width, depth, height" factors. While a grip
    /// is dragged, one factor applies along that grip's axes (so "-1" on a side
    /// grip mirrors across that side's axis) and three apply along the box's
    /// axes, about the opposite grip. Otherwise the factors apply about the
    /// centre of the selection's base. Applies to the active gesture or the
    /// selection.
    std::optional<std::string> submitFactors(std::string_view factors);

    /// The selection's bounding box in its own frame, with its grips.
    struct Box {
        Mat3 frame{1.0}; ///< columns: the box's axes in world space
        Vec3 lo{0.0};    ///< corners in frame coordinates
        Vec3 hi{0.0};
        /// Each grip's side along each axis (-1, 0 or +1) and its world point.
        std::vector<std::pair<Vec3, Vec3>> grips;
        [[nodiscard]] Vec3 world(const Vec3& side) const; ///< box point at sides (-1..1) per axis
        void show(ScalePreview& preview) const;           ///< puts the box's corners and grips on a preview
    };
    /// The current selection's box, or nothing when nothing is selected.
    [[nodiscard]] std::optional<Box> selectionBox() const;

private:
    /// What a scale changes: whole objects, or the selection's elements (ids: their owners).
    struct Targets {
        ObjectId primary;
        std::vector<ObjectId> ids;
        std::vector<ElementRef> elements;
    };
    struct Gesture {
        Targets targets;
        Vec3 pivot{0.0};
        Vec3 normal{0.0, 0.0, 1.0}; ///< radial scale: the plane the pointer moves in
        Vec3 start{0.0};            ///< radial scale: the first click; grip scale: the grip
        Mat3 frame{1.0};            ///< axes the factors apply along
        std::optional<Vec3> side;   ///< the dragged grip's sides (-1, 0, +1 per axis); empty for a radial scale
        std::optional<std::size_t> grip; ///< index into the box's grips
    };

    [[nodiscard]] std::optional<Targets> targets() const;
    /// The grip under the pointer, if any.
    [[nodiscard]] std::optional<std::size_t> gripAt(const Box& box, const PointerInput& input) const;
    /// Idle: shows the selection's box and grips (highlighting `hot`), or clears them.
    void showGrips(std::optional<std::size_t> hot) const;
    /// Starts a grip drag when the pointer is on a grip.
    bool startGrip(const PointerInput& input);
    /// Starts a uniform scale about the base centre from a click off the grips.
    void startRadial(const PointerInput& input);
    /// The factors a grip drag gives for the pointer ray (1 along axes the grip does not move).
    [[nodiscard]] std::optional<Vec3> gripFactors(const Ray& ray) const;
    /// The preview for `factors` (checked, with the result drawn in place).
    [[nodiscard]] ScalePreview preview(const Vec3& factors, const std::optional<Vec3>& current) const;
    /// One undo entry for every target, scaled about `pivot` along `frame`.
    bool commit(const Targets& targets, const Vec3& pivot, const Vec3& factors, const Mat3& frame);

    std::optional<Gesture> gesture_;
};

} // namespace pf
