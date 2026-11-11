#pragma once

#include "app/tools/tool.hpp"

#include <vector>

namespace pf {

/// Scale: pick the object, click a grip point, move away from or toward the
/// base centre and click (or type a factor or "width, depth, height" factors).
/// With several objects selected, the whole selection scales about the base
/// centre of its bounding box. Commits one "Scale" entry.
class ScaleTool final : public ToolBase {
public:
    static constexpr double kSnapStep = 0.1; ///< factor steps with grid snap on

    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override { return submitFactors(text); }
    [[nodiscard]] std::string_view measurementLabel() const override { return "Scale (factor, or w, d, h)"; }
    [[nodiscard]] bool active() const override { return gesture_.has_value(); }
    void cancel() override { gesture_.reset(); }
    [[nodiscard]] std::string hint() const override;

    /// Exact scale: one uniform factor or "width, depth, height" factors, about
    /// the centre of the object's base. Applies to the active gesture's object
    /// or the selected object.
    std::optional<std::string> submitFactors(std::string_view factors);

private:
    struct Gesture {
        ObjectId objectId;
        Vec3 pivot{0.0};
        Vec3 normal{0.0, 0.0, 1.0};
        Vec3 start{0.0};
        std::vector<ObjectId> ids; ///< every object that scales (objectId and the rest of a multi-selection)
    };

    /// One undo entry for all of `ids`, scaled about `pivot`.
    bool commit(const std::vector<ObjectId>& ids, const Vec3& pivot, const Vec3& factors);

    std::optional<Gesture> gesture_;
};

} // namespace pf
