#pragma once

#include "app/tools/tool.hpp"
#include "core/document/element_ref.hpp"

#include <optional>
#include <vector>

namespace pf {

/// Follow Me: sweeps a profile face along a path of edges (open or closed),
/// or around a face's edge (core/geometry/follow_me.*). Two ways in:
///  - select the path first (edges, or a face to go around), then click the
///    profile face; hovering a face shows the sweep;
///  - or click the profile face, then the path: an edge brings its whole arc,
///    circle or run of edges, a face its edge. Hovering shows the sweep.
/// Each sweep commits one "Follow Me" entry. Escape drops the picked profile.
class FollowMeTool final : public ToolBase {
public:
    using ToolBase::ToolBase;

    void pointerMove(const PointerInput& input) override;
    void pointerUp(const PointerInput& input) override;
    std::optional<std::string> submit(std::string_view text) override;
    /// True once the profile is picked and the tool waits for the path.
    [[nodiscard]] bool active() const override { return profile_.has_value(); }
    void cancel() override { profile_.reset(); }
    [[nodiscard]] std::string hint() const override;
    [[nodiscard]] bool showsSnapInHint() const override { return false; }

    /// The selected edges and faces, which a click on a profile face sweeps along.
    [[nodiscard]] std::vector<ElementRef> selectedPath() const;

private:
    /// Shows the sweep of `profile` along `path` (or why it cannot be made).
    void preview(const ElementRef& profile, const std::vector<ElementRef>& path);
    bool sweep(const ElementRef& profile, const std::vector<ElementRef>& path);

    std::optional<ElementRef> profile_;
};

/// The face under the pointer, ignoring a nearer edge or vertex (a profile is
/// a face even when the pointer is close to its border).
std::optional<ElementRef> faceUnder(const Document& document, const PointerInput& input);

} // namespace pf
