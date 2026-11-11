#pragma once

#include "core/document/document.hpp"

#include <glm/geometric.hpp>

namespace pf {

/// Counts rapid clicks at one spot: 1 for a single click, 2 for a double
/// click, 3 for a triple click. A fourth rapid click starts again at 1. Times
/// are in seconds, positions in window pixels. Pure logic, so it is testable
/// without a window.
class ClickCounter {
public:
    static constexpr double kIntervalSeconds = 0.4; ///< max gap between clicks of a series
    static constexpr double kSlopPixels = 4.0;      ///< max pointer travel between clicks of a series

    /// Records a click and returns its position in the series (1, 2 or 3).
    int click(double timeSeconds, const Vec2& position) {
        const bool continues = count_ > 0 && count_ < 3 && timeSeconds - lastTime_ <= kIntervalSeconds &&
                               timeSeconds >= lastTime_ && glm::length(position - lastPosition_) <= kSlopPixels;
        count_ = continues ? count_ + 1 : 1;
        lastTime_ = timeSeconds;
        lastPosition_ = position;
        return count_;
    }
    void reset() { count_ = 0; }

private:
    int count_ = 0;
    double lastTime_ = 0.0;
    Vec2 lastPosition_{0.0};
};

} // namespace pf
