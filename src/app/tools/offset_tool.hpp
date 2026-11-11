#pragma once

#include "app/tools/tool.hpp"

#include <cstdint>
#include <optional>

namespace pf {

/// Offset (F): click a face, move the pointer into or out of it and click
/// again (or press, drag and release), or type a distance. Every loop of the
/// face moves by the distance in the face's plane (mesh::offsetFace): inward
/// the face becomes a ring around a smaller copy of itself, outward a ring
/// forms around it. The distance follows the pointer: how much nearer to or
/// further from the face's edges it is than where the face was clicked.
/// A typed distance keeps the direction the pointer shows (inward before the
/// pointer has moved); a negative one goes the other way. With no gesture, a
/// typed distance offsets the selected face (positive inward). Double-clicking
/// a face repeats the last offset on it. Commits one "Offset" entry.
class OffsetTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override { return "Distance"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// The last distance committed (signed: positive inward), for double-click.
    [[nodiscard]] std::optional<double> lastDistance() const { return last_; }

private:
    struct Gesture {
        ObjectId objectId;
        std::uint32_t face = 0; ///< mesh face id
        Vec3 anchor{0.0};       ///< the clicked point (world)
        double startDistance = 0.0; ///< signed distance of the anchor from the face's edges
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };

    /// The object and mesh face under the pointer, if it can be offset.
    [[nodiscard]] std::optional<std::pair<ObjectId, std::uint32_t>> faceUnder(const PointerInput& input) const;
    void setPreview(double distance);
    void finish();
    bool commit(ObjectId id, std::uint32_t face, double distance);

    std::optional<Gesture> gesture_;
    std::optional<double> last_;
};

} // namespace pf
