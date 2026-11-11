#pragma once

#include "core/document/document.hpp"

#include <string>

namespace pf {

/// A text label pinned to a point in the 3D view (dimension values, the live
/// tape measurement). Window coordinates; drawn by the UI over the viewport.
struct ViewportLabel {
    enum class Style { Dimension, Live };
    Vec2 position{0.0};
    std::string text;
    Style style = Style::Dimension;
};

} // namespace pf
