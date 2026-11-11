#pragma once

#include "core/document/document.hpp"
#include "core/geometry/pick.hpp"
#include "core/geometry/snap.hpp"

#include <optional>
#include <span>

namespace pf {

/// Snapping to construction guides (Document::guides).
///
/// Tolerances are angular, as in SnapIndex::nearest: a candidate at distance t
/// along the ray counts when it is within `radiusPerDistance * t` of the ray,
/// and candidates farther than `occluder` (the face under the pointer) are
/// hidden behind it.

/// A snap and how near the pointer it is: the candidate's angular distance from
/// the ray, weighted by its kind (lower wins), comparable with snapScore().
struct ScoredSnap {
    SnapResult snap;
    double score = 0.0;
};

/// Angular distance of `p` from the ray (perpendicular distance over depth),
/// or nothing when `p` is behind the ray origin.
std::optional<double> angularDistance(const Ray& ray, const Vec3& p);

/// Score of a snap from SnapIndex::nearest, on the same scale as the guide
/// candidates: endpoints are preferred over midpoints as they are there.
std::optional<double> snapScore(const Ray& ray, const SnapResult& snap);

/// Nearest guide point, or crossing of a guide line with another guide line
/// or with an object edge in `edges`. Guide points weigh like endpoints and
/// crossings a little less, so they compete fairly with SnapIndex::nearest.
std::optional<ScoredSnap> nearestGuidePoint(std::span<const Guide> guides, const SnapIndex& edges, const Ray& ray,
                                            double radiusPerDistance, std::optional<double> occluder = std::nullopt);

/// The point on the nearest guide line under the pointer (the point of the
/// line closest to the ray), or nothing when no line is within the radius.
std::optional<SnapResult> nearestOnGuide(std::span<const Guide> guides, const Ray& ray, double radiusPerDistance,
                                         std::optional<double> occluder = std::nullopt);

/// Parameters (s, t) of the closest points of the lines p + s * u and q + t * v,
/// or nothing when they are (nearly) parallel.
std::optional<std::pair<double, double>> closestLineParameters(const Vec3& p, const Vec3& u, const Vec3& q, const Vec3& v);

} // namespace pf
