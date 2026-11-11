#include "core/geometry/freehand.hpp"

#include <algorithm>
#include <cstddef>

namespace pf::freehand {
namespace {

constexpr int kSmoothingPasses = 3;

double distanceToSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const double len2 = glm::dot(ab, ab);
    if (len2 <= 0.0) return glm::length(p - a);
    const double t = std::clamp(glm::dot(p - a, ab) / len2, 0.0, 1.0);
    return glm::length(p - (a + ab * t));
}

/// Marks the points of pts[first..last] that Ramer-Douglas-Peucker keeps.
/// Iterative, so a long stroke cannot overflow the stack.
void simplify(const std::vector<Vec3>& pts, std::size_t first, std::size_t last, double tolerance, std::vector<bool>& keep) {
    std::vector<std::pair<std::size_t, std::size_t>> spans{{first, last}};
    while (!spans.empty()) {
        const auto [a, b] = spans.back();
        spans.pop_back();
        keep[a] = true;
        keep[b] = true;
        double worst = -1.0;
        std::size_t at = a;
        for (std::size_t i = a + 1; i < b; ++i) {
            const double d = distanceToSegment(pts[i], pts[a], pts[b]);
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > tolerance) {
            spans.emplace_back(a, at);
            spans.emplace_back(at, b);
        }
    }
}

std::vector<Vec3> smoothPass(const std::vector<Vec3>& pts, bool closed) {
    const std::size_t n = pts.size();
    std::vector<Vec3> out(pts);
    // The first point stays put (it may be snapped to existing geometry), and
    // so does the last point of an open stroke.
    for (std::size_t i = 1; i < n; ++i) {
        if (!closed && i + 1 == n) continue;
        const Vec3& prev = pts[(i + n - 1) % n];
        const Vec3& next = pts[(i + 1) % n];
        out[i] = (prev + pts[i] * 2.0 + next) * 0.25;
    }
    return out;
}

} // namespace

std::vector<Vec3> smoothStroke(const std::vector<Vec3>& raw, double tolerance, bool closed) {
    if (raw.empty() || !(tolerance > 0.0)) return {};
    // Drop jitter: points that barely moved from the last kept one.
    const double spacing = tolerance * 0.5;
    std::vector<Vec3> pts{raw.front()};
    for (std::size_t i = 1; i < raw.size(); ++i) {
        if (glm::length(raw[i] - pts.back()) >= spacing) pts.push_back(raw[i]);
    }
    if (!closed) {
        // The release point is where the user aimed: keep it, replacing a kept
        // point that sits too close to it.
        if (pts.size() > 1 && glm::length(raw.back() - pts.back()) < spacing) pts.pop_back();
        if (glm::length(raw.back() - pts.front()) >= spacing) pts.push_back(raw.back());
        if (pts.size() < 2) return {};
    } else {
        // The loop closes on its first point: drop a last point on top of it.
        while (pts.size() > 1 && glm::length(pts.back() - pts.front()) < spacing) pts.pop_back();
        if (pts.size() < 3) return {};
    }

    for (int pass = 0; pass < kSmoothingPasses && pts.size() > 2; ++pass) pts = smoothPass(pts, closed);

    std::vector<bool> keep(pts.size(), false);
    if (!closed) {
        simplify(pts, 0, pts.size() - 1, tolerance, keep);
    } else {
        // Split the loop at the point furthest from the first one and simplify both halves.
        std::size_t far = 0;
        double best = -1.0;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            const double d = glm::length(pts[i] - pts[0]);
            if (d > best) {
                best = d;
                far = i;
            }
        }
        std::vector<Vec3> ring(pts);
        ring.push_back(pts[0]);
        std::vector<bool> ringKeep(ring.size(), false);
        simplify(ring, 0, far, tolerance, ringKeep);
        simplify(ring, far, ring.size() - 1, tolerance, ringKeep);
        for (std::size_t i = 0; i < pts.size(); ++i) keep[i] = ringKeep[i];
    }
    std::vector<Vec3> out;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (keep[i]) out.push_back(pts[i]);
    }
    if (closed && out.size() < 3) {
        // A very flat loop simplifies to a line; keep the three most spread points instead.
        out.clear();
        const std::size_t n = pts.size();
        for (const std::size_t i : {std::size_t{0}, n / 3, (2 * n) / 3}) out.push_back(pts[i]);
    }
    return out;
}

bool closesLoop(const std::vector<Vec3>& raw, double radius) {
    if (raw.size() < 3 || !(radius > 0.0)) return false;
    if (glm::length(raw.back() - raw.front()) > radius) return false;
    return std::ranges::any_of(raw, [&](const Vec3& p) { return glm::length(p - raw.front()) > 2.0 * radius; });
}

} // namespace pf::freehand
