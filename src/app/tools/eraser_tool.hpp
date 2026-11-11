#pragma once

#include "app/tools/tool.hpp"
#include "core/document/element_ref.hpp"

#include <optional>
#include <vector>

namespace pf {

/// What an Eraser stroke does to the edges it collected, from the keys held
/// when it is released: Shift hides them, Ctrl (Cmd on macOS) softens them,
/// otherwise they are erased.
enum class EraseMode { Erase, Hide, Soften };
EraseMode eraseMode(const PointerInput& input);

/// Eraser (E): click an edge, or press and drag over several, and release.
/// Every edge the stroke touched is erased in one commit (one undo entry):
/// two coplanar faces on either side merge into one, other faces that used the
/// edge go with it (mesh::Mesh::eraseEdge). Shift hides the edges instead and
/// Ctrl softens them (soft and smooth); faces are kept either way. Faces under
/// the pointer are ignored. Escape drops the stroke.
class EraserTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void pointerDown(const PointerInput& input) override;
    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    /// True while the button is held (a stroke is collecting edges).
    [[nodiscard]] bool active() const override { return stroke_.has_value(); }
    void cancel() override { stroke_.reset(); }
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] bool showsSnapInHint() const override { return false; }

    /// Edges collected by the stroke in progress, in the order touched.
    [[nodiscard]] const std::vector<ElementRef>& stroke() const;

private:
    /// Adds the edge under the pointer to the stroke and updates the preview.
    void collect(const PointerInput& input);

    std::optional<std::vector<ElementRef>> stroke_;
};

/// The visible edge under the pointer for the Eraser: the picked edge, or,
/// when the pick is a vertex, the vertex's edge nearest the pointer ray.
std::optional<ElementRef> edgeUnder(const Document& document, const PointerInput& input);

} // namespace pf
