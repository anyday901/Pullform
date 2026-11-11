#include "core/geometry/plane.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace pf {

namespace {

/// Largest off-axis component (of a unit vector) still treated as rounding.
/// Checked on the transverse components: 1 - |dominant| shrinks with the
/// square of the tilt, so it would accept far larger tilts.
constexpr double kAxisSnap = 1e-12;

/// `v` made exactly a world axis when its other two components are rounding noise.
Vec3 snapToAxis(const Vec3& v) {
    for (int i = 0; i < 3; ++i) {
        const double j = v[(i + 1) % 3];
        const double k = v[(i + 2) % 3];
        if (std::abs(j) <= kAxisSnap && std::abs(k) <= kAxisSnap && std::abs(v[i]) > 0.5) {
            Vec3 out{0.0};
            out[i] = v[i] > 0.0 ? 1.0 : -1.0;
            return out;
        }
    }
    return v;
}

} // namespace

Vec3 planeNormal(const PlaneBasis& plane) { return glm::normalize(glm::cross(plane.xAxis, plane.yAxis)); }

Vec2 toPlane(const PlaneBasis& plane, const Vec3& world) {
    const Vec3 d = world - plane.origin;
    return {glm::dot(d, plane.xAxis), glm::dot(d, plane.yAxis)};
}

Vec3 fromPlane(const PlaneBasis& plane, const Vec2& p, double offset) {
    Vec3 out = plane.origin + plane.xAxis * p.x + plane.yAxis * p.y;
    if (offset != 0.0) out += planeNormal(plane) * offset;
    return out;
}

double planeDistance(const PlaneBasis& plane, const Vec3& world) { return glm::dot(world - plane.origin, planeNormal(plane)); }

Vec3 projectToPlane(const PlaneBasis& plane, const Vec3& world) { return fromPlane(plane, toPlane(plane, world)); }

Quat planeRotation(const PlaneBasis& plane) {
    const Mat3 m(plane.xAxis, plane.yAxis, planeNormal(plane));
    return glm::normalize(glm::quat_cast(m));
}

PlaneBasis groundPlane() { return PlaneBasis{}; }

PlaneBasis planeThrough(const Vec3& point, const Vec3& normal) {
    const Vec3 n = snapToAxis(glm::normalize(normal));
    PlaneBasis plane;
    if (std::abs(n.z) == 1.0) {
        plane.xAxis = {1.0, 0.0, 0.0};
        plane.yAxis = glm::cross(n, plane.xAxis); // +Y facing up, -Y facing down
    } else {
        // Uphill in the plane: world Z with its normal part removed.
        plane.yAxis = snapToAxis(glm::normalize(Vec3{0.0, 0.0, 1.0} - n * n.z));
        plane.xAxis = snapToAxis(glm::normalize(glm::cross(plane.yAxis, n)));
    }
    const double d = glm::dot(point, n);
    plane.origin = n * d;
    // Keep an exact zero (not -0.0) where the normal has none, for tidy values.
    for (int i = 0; i < 3; ++i) {
        if (plane.origin[i] == 0.0) plane.origin[i] = 0.0;
    }
    return plane;
}

PlaneBasis facingViewer(const PlaneBasis& plane, const Vec3& viewDirection) {
    const Vec3 n = planeNormal(plane);
    if (glm::dot(n, viewDirection) <= 0.0) return plane;
    return planeThrough(plane.origin, -n);
}

bool samePlane(const PlaneBasis& a, const PlaneBasis& b) {
    const Vec3 na = planeNormal(a);
    const Vec3 nb = planeNormal(b);
    return glm::dot(na, nb) > 1.0 - 1e-9 && std::abs(planeDistance(a, b.origin)) < 1e-9;
}

} // namespace pf
