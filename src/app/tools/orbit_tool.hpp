#pragma once

#include "app/tools/tool.hpp"

namespace pf {

/// Orbit: the viewport's navigation owns the drag, so the tool itself does nothing.
class OrbitTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    std::optional<std::string> submit(std::string_view text) override;
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] bool showsSnapInHint() const override { return false; }
};

} // namespace pf
