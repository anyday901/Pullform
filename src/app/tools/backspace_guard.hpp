#pragma once

#include <optional>

namespace pf {

/// Decides whether Backspace may delete the selection. Backspace both erases
/// typed measurement characters and deletes the selected object, so a press
/// that arrives just after the measurement was emptied (for example a third
/// press after typing "12") must not fall through and delete. Only Backspace
/// is held back for a short window after typing; the Delete key always works.
class BackspaceGuard {
public:
    /// Seconds after the last typed measurement during which Backspace is erase-only.
    static constexpr double kWindowSeconds = 1.0;

    /// Call every frame in which the measurement box holds text.
    void noteTyping(double now) { lastTyping_ = now; }

    [[nodiscard]] bool mayDelete(double now) const {
        return !lastTyping_ || now - *lastTyping_ >= kWindowSeconds;
    }

private:
    std::optional<double> lastTyping_;
};

} // namespace pf
