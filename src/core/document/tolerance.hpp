#pragma once

#include <cmath>

namespace pf::tol {

// Central geometry tolerances. All lengths are in meters (the internal unit).
//
// Supported scale range: dimensions from 0.1 mm (kMinDimension) up to
// coordinates of +/-10 km (kMaxCoordinate). Doubles resolve far finer than
// this across that range; the limits exist so degenerate or absurd input is
// rejected before it reaches the document.

inline constexpr double kLengthEpsilon = 1e-9;   // coordinate equality (1 nm)
inline constexpr double kMinDimension = 1e-4;    // smallest width/depth/height: 0.1 mm
inline constexpr double kMaxCoordinate = 1e4;    // largest |coordinate| or dimension: 10 km
inline constexpr double kMinArea = kMinDimension * kMinDimension;

inline bool isFinite(double v) { return std::isfinite(v); }

inline bool nearlyEqual(double a, double b, double eps = kLengthEpsilon) {
    return std::abs(a - b) <= eps;
}

} // namespace pf::tol
