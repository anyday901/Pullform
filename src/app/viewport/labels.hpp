#pragma once

#include "core/document/document.hpp"

#include <string>

namespace pf {

/// A text label pinned to a point in the 3D view (dimension values, the live
/// tape measurement, the inference tooltip). Window coordinates; drawn by the
/// UI over the viewport. Dimension and Live labels are centered on
/// `position`; an Inference tooltip starts there (to the lower right of the
/// snap marker).
struct ViewportLabel {
    enum class Style { Dimension, Live, Inference };
    Vec2 position{0.0};
    std::string text;
    Style style = Style::Dimension;
};

} // namespace pf
