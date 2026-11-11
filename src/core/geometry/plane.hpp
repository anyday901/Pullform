#pragma once

#include "core/document/document.hpp"

#include <optional>

namespace pf {

// Drawing planes. A PlaneBasis (document.hpp) with unit, perpendicular axes is
// a right-handed frame: xAxis and yAxis lie in the plane and their cross
// product is the plane normal. Drawing tools keep points in this frame so a
// rectangle, circle or line drawn on a wall is the same code as on the ground.

/// Unit normal of a plane (xAxis x yAxis).
Vec3 planeNormal(const PlaneBasis& plane);

/// Plane coordinates of a world point (its projection onto the plane).
Vec2 toPlane(const PlaneBasis& plane, const Vec3& world);
/// World point at plane coordinates `p`, lifted by `offset` along the normal.
Vec3 fromPlane(const PlaneBasis& plane, const Vec2& p, double offset = 0.0);
/// The point of the plane nearest `world` (straight along the normal).
Vec3 projectToPlane(const PlaneBasis& plane, const Vec3& world);
/// Signed distance of `world` from the plane, positive on the normal side.
double planeDistance(const PlaneBasis& plane, const Vec3& world);

/// The rotation that takes local X, Y and Z to the plane's xAxis, yAxis and
/// normal: an object placed with it has its local XY plane on this plane.
Quat planeRotation(const PlaneBasis& plane);

/// The ground: z = 0 with the world X and Y axes.
PlaneBasis groundPlane();

/// The plane through `point` with unit `normal`, with axes chosen the same
/// way every time so that rectangles line up with the model axes:
///  - horizontal planes (normal +/-Z) use world X as their x axis;
///  - other planes use the horizontal direction in the plane as x and the
///    steepest uphill direction as y (so a wall's y axis is world Z).
/// The origin is the plane's point nearest the world origin, so grid steps in
/// plane coordinates line up with the world grid on axis-aligned planes.
/// Normals within about 1e-9 of a world axis are snapped onto it.
PlaneBasis planeThrough(const Vec3& point, const Vec3& normal);

/// `plane`'s normal flipped, if needed, so that it faces against `viewDirection`
/// (towards the viewer). The axes are re-derived with planeThrough.
PlaneBasis facingViewer(const PlaneBasis& plane, const Vec3& viewDirection);

/// Whether two planes are the same (same normal and position), regardless of
/// their axes and origin.
bool samePlane(const PlaneBasis& a, const PlaneBasis& b);

} // namespace pf
