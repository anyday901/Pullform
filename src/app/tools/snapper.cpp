#include "app/tools/snapper.hpp"

#include "core/document/tolerance.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace pf {

double SnapSettings::toGrid(double v) const { return gridOn() ? std::round(v / step) * step : v; }

Vec3 SnapSettings::toGrid(const Vec3& p) const { return {toGrid(p.x), toGrid(p.y), p.z}; }

double PointSnapper::tolerance(const PointerInput& input, const Vec3& at) const {
    return input.pixelAngle * settings_.radiusPx * glm::length(at - input.ray.origin);
}

void PointSnapper::dropStaleReferences() {
    // New / Open replace the project: points from the old one must not guide the new one.
    if (referencesGeneration_ == editor_.projectGeneration()) return;
    references_.clear();
    referencesGeneration_ = editor_.projectGeneration();
}

void PointSnapper::remember(const Vec3& p) {
    dropStaleReferences();
    // Most recent first, no duplicates, at most kMaxReferences.
    const Vec3 point = p;
    std::erase(references_, point);
    if (references_.size() >= SnapSettings::kMaxReferences) references_.resize(SnapSettings::kMaxReferences - 1);
    references_.push_back(point);
    std::rotate(references_.rbegin(), references_.rbegin() + 1, references_.rend());
}

std::optional<SnapResult> PointSnapper::point(const PointerInput& input, std::optional<ObjectId> exclude) {
    if (!active(input)) return std::nullopt;
    index_.sync(editor_.document());
    // The face under the pointer hides points behind it, unless it belongs to
    // the excluded object (the one being edited, drawn as a preview).
    std::optional<double> occluder;
    if (input.pick && !(exclude && input.pick->objectId == *exclude)) occluder = input.pick->distance;
    auto hit = index_.nearest(input.ray, input.pixelAngle * settings_.radiusPx, occluder, exclude);
    if (hit) remember(hit->featurePoint);
    return hit;
}

std::optional<SnapResult> PointSnapper::ground(const PointerInput& input, std::optional<Vec3> skip) {
    if (auto hit = point(input)) {
        // Rectangles are drawn on the ground: use the point straight below.
        hit->point.z = 0.0;
        if (std::abs(hit->featurePoint.z) > tol::kLengthEpsilon) hit->guides.emplace_back(hit->featurePoint, hit->point, 2);
        return hit;
    }
    const auto raw = intersectHorizontalPlane(input.ray, 0.0);
    if (!raw) return std::nullopt;
    SnapResult res;
    res.point = settings_.toGrid(*raw);
    res.kind = settings_.gridOn() ? SnapKind::Grid : SnapKind::None;
    res.featurePoint = res.point;
    if (active(input)) infer(*raw, tolerance(input, *raw), skip, res);
    return res;
}

void PointSnapper::infer(const Vec3& raw, double tolerance, std::optional<Vec3> skip, SnapResult& res) {
    dropStaleReferences();
    // Line up with a remembered point along X or Y. The rectangle's own first
    // corner is skipped: lining up with it would give zero width or depth.
    const auto isSkipped = [&skip](const Vec3& r) {
        return skip && std::abs(r.x - skip->x) <= tol::kLengthEpsilon && std::abs(r.y - skip->y) <= tol::kLengthEpsilon;
    };
    const Vec3* refX = nullptr; // same x as the pointer: guide runs along Y
    const Vec3* refY = nullptr; // same y: guide runs along X
    double bestX = tolerance;
    double bestY = tolerance;
    for (const auto& r : references_) {
        if (isSkipped(r)) continue;
        if (const double d = std::abs(raw.x - r.x); d < bestX) {
            bestX = d;
            refX = &r;
        }
        if (const double d = std::abs(raw.y - r.y); d < bestY) {
            bestY = d;
            refY = &r;
        }
    }
    if (!refX && !refY) return;
    if (refX) res.point.x = refX->x;
    if (refY) res.point.y = refY->y;
    res.kind = SnapKind::Inference;
    res.featurePoint = res.point;
    if (refX) res.guides.emplace_back(Vec3{refX->x, refX->y, 0.0}, res.point, 1);
    if (refY) res.guides.emplace_back(Vec3{refY->x, refY->y, 0.0}, res.point, 0);
}

std::optional<SnapResult> PointSnapper::height(const PointerInput& input, const Vec3& axisPoint, ObjectId exclude) {
    if (!active(input)) return std::nullopt;
    index_.sync(editor_.document());
    return index_.nearestHeight(axisPoint, tolerance(input, axisPoint), exclude);
}

} // namespace pf
