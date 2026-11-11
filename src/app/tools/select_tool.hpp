#pragma once

#include "app/tools/tool.hpp"

namespace pf {

class PushPullTool;

/// Select: a click on an object selects it (with the face under the pointer),
/// a click on empty space clears the selection. The typed value sets the
/// selected object's total height.
class SelectTool final : public ToolBase {
public:
    SelectTool(ToolContext& ctx, PushPullTool& heights) : ToolBase(ctx), heights_(heights) {}

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string_view measurementLabel() const override { return "Total height"; }
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] bool showsSnapInHint() const override { return false; }

private:
    PushPullTool& heights_;
};

} // namespace pf
