#pragma once

#include "core/document/document.hpp"

#include <vector>

namespace pf::freehand {

/// Turns the raw points of a pointer stroke into a smooth polyline with few
/// vertices, as the Freehand tool commits it.
///
/// Points closer than half of `tolerance` to the last kept point are dropped
/// (pointer jitter), the rest are smoothed with a few [1 2 1] passes (the first
/// point, and the last point of an open stroke, stay where they are), and the result is simplified
/// (Ramer-Douglas-Peucker). Smoothing moves no point more than half of
/// `tolerance` and simplifying adds at most the other half, so every kept
/// sample lies within `tolerance` of the result, however sparse the samples. A `closed` stroke is treated as a loop: its first point is
/// not repeated at the end, and the result has at least three points or none.
/// An open stroke keeps its first and last point, and has at least two points
/// or none (nothing drawn).
std::vector<Vec3> smoothStroke(const std::vector<Vec3>& raw, double tolerance, bool closed);

/// Whether a stroke that ends at `end` closes on its start: the end is within
/// `radius` of the start and the stroke went somewhere first (some point is
/// more than twice `radius` from the start), so a short wiggle at the start
/// does not count as a loop.
bool closesLoop(const std::vector<Vec3>& raw, double radius);

} // namespace pf::freehand
