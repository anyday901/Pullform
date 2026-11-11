#pragma once

#include "core/geometry/pick.hpp"
#include "core/geometry/topology.hpp"

#include <optional>
#include <span>

namespace pf {

/// One object offered to the element picker.
struct PickTarget {
    ObjectId object;
    const ElementTopology* topology = nullptr;
};

struct ElementPickOptions {
    double pixelAngle = 0.0;    ///< radians per pixel at unit distance (PointerInput::pixelAngle)
    double vertexPixels = 7.0;  ///< a vertex wins within this many pixels of the pointer
    double edgePixels = 5.0;    ///< an edge wins within this many pixels of the pointer
};

/// The element under the pointer.
struct ElementHit {
    ElementRef element;
    Vec3 point{0.0};       ///< on the element (vertex, closest point of the edge, or face hit)
    double distance = 0.0; ///< along the ray
};

/// Picks the vertex, edge or face under the pointer, in that priority: a
/// vertex within `vertexPixels`, else an edge within `edgePixels`, else the
/// nearest face the ray hits. Vertices and edges hidden behind a face (of any
/// target) are skipped; the faces they lie on never hide them. Single-sided
/// faces are hit only from the front, matching back-face culling.
std::optional<ElementHit> pickElement(const Ray& ray, std::span<const PickTarget> targets, const ElementPickOptions& options);

} // namespace pf
