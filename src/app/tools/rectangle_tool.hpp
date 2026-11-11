#pragma once

#include "app/tools/tool.hpp"

namespace pf {

/// Rectangle: click (or press-drag-release) two opposite corners on the ground,
/// or type "width, depth". Commits one "Draw rectangle" entry.
class RectangleTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override { return submitSize(text); }
    [[nodiscard]] std::string_view measurementLabel() const override { return "Dimensions (width, depth)"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] std::optional<Vec3> groundSkip() const override {
        return gesture_ ? std::optional(gesture_->start) : std::nullopt;
    }

    /// Exact size. `depth` empty means `width` holds "w, d". With a first
    /// corner placed, the size is drawn from it toward the pointer; otherwise
    /// from the origin.
    std::optional<std::string> submitSize(std::string_view width, std::string_view depth = {});

private:
    struct Gesture {
        Vec3 start{0.0};
        Vec2 pressScreen{0.0};
        bool pressed = false;
    };

    std::optional<Vec3> groundPoint(const PointerInput& input) { return ctx_.groundPoint(input, groundSkip()); }
    void finish(const PointerInput& input);
    bool commit(const RectanglePlacement& placement);

    std::optional<Gesture> gesture_;
};

} // namespace pf
