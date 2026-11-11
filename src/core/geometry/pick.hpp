#pragma once

#include "core/document/document.hpp"
#include "core/geometry/solid.hpp"

#include <optional>

namespace pf {

/// A world-space ray. `direction` is expected to be normalized.
struct Ray {
    Vec3 origin{0.0};
    Vec3 direction{0.0, 0.0, -1.0};
};

/// Ray/triangle intersection (Moller-Trumbore). Returns the ray parameter of the
/// hit. With `cullBack`, triangles facing away from the ray are ignored, which
/// matches single-sided rendering.
std::optional<double> intersectTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, bool cullBack);

/// A pick resolved to the semantic model: object, logical face and hit point.
struct PickHit {
    ObjectId objectId;
    FaceId faceId;
    Vec3 point{0.0};
    double distance = 0.0;
};

/// CPU ray cast against one object's derived mesh.
std::optional<PickHit> pickObject(const Ray& ray, const ProfileObject& object, const MeshData& mesh);

/// Whether `hit` should replace `best` as the nearest pick. Hits at the same
/// distance (a rectangle drawn on a wall lies exactly on it) go to the object
/// with the higher id, which is the one drawn later, on top.
bool nearerHit(const PickHit& hit, const std::optional<PickHit>& best);

/// CPU ray cast against every object in the document; returns the nearest hit.
std::optional<PickHit> pickDocument(const Ray& ray, const Document& document);

/// Intersection with the horizontal plane z = `z`, in front of the ray origin.
std::optional<Vec3> intersectHorizontalPlane(const Ray& ray, double z);

/// Intersection with the plane through `point` with unit `normal`, in front of
/// the ray origin; nothing when the ray is (nearly) parallel to the plane.
std::optional<Vec3> intersectPlane(const Ray& ray, const Vec3& point, const Vec3& normal);

/// Parameter t of the point on the line `origin + t * axis` closest to the ray,
/// or nothing when the ray is (nearly) parallel to the axis.
std::optional<double> closestParameterOnAxis(const Vec3& origin, const Vec3& axis, const Ray& ray);

} // namespace pf
