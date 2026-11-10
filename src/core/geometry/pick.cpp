#include "core/geometry/pick.hpp"

#include <glm/geometric.hpp>

#include <cmath>

namespace pf {

std::optional<double> intersectTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, bool cullBack) {
    constexpr double kEps = 1e-12;
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p = glm::cross(ray.direction, e2);
    const double det = glm::dot(e1, p);
    if (cullBack ? det < kEps : std::abs(det) < kEps) return std::nullopt;
    const double inv = 1.0 / det;
    const Vec3 s = ray.origin - a;
    const double u = glm::dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return std::nullopt;
    const Vec3 q = glm::cross(s, e1);
    const double v = glm::dot(ray.direction, q) * inv;
    if (v < 0.0 || u + v > 1.0) return std::nullopt;
    const double t = glm::dot(e2, q) * inv;
    if (t <= 0.0) return std::nullopt;
    return t;
}

std::optional<PickHit> pickObject(const Ray& ray, const ProfileObject& object, const MeshData& mesh) {
    std::optional<PickHit> best;
    for (std::size_t t = 0; t < mesh.triangleFaces.size(); ++t) {
        const auto& a = mesh.positions[mesh.indices[3 * t]];
        const auto& b = mesh.positions[mesh.indices[3 * t + 1]];
        const auto& c = mesh.positions[mesh.indices[3 * t + 2]];
        const auto hit = intersectTriangle(ray, a, b, c, /*cullBack=*/true);
        if (hit && (!best || *hit < best->distance)) {
            best = PickHit{object.id, mesh.triangleFaces[t], ray.origin + ray.direction * *hit, *hit};
        }
    }
    return best;
}

std::optional<PickHit> pickDocument(const Ray& ray, const Document& document) {
    std::optional<PickHit> best;
    for (const auto& o : document.objects) {
        const auto hit = pickObject(ray, *o, buildMesh(*o));
        if (hit && (!best || hit->distance < best->distance)) best = hit;
    }
    return best;
}

std::optional<Vec3> intersectHorizontalPlane(const Ray& ray, double z) {
    if (std::abs(ray.direction.z) < 1e-12) return std::nullopt;
    const double t = (z - ray.origin.z) / ray.direction.z;
    if (t <= 0.0) return std::nullopt;
    Vec3 p = ray.origin + ray.direction * t;
    p.z = z;
    return p;
}

std::optional<double> closestParameterOnAxis(const Vec3& origin, const Vec3& axis, const Ray& ray) {
    // Closest points between lines P(t) = origin + t*axis and Q(s) = ray.origin + s*dir.
    const Vec3 w0 = origin - ray.origin;
    const double a = glm::dot(axis, axis);
    const double b = glm::dot(axis, ray.direction);
    const double c = glm::dot(ray.direction, ray.direction);
    const double d = glm::dot(axis, w0);
    const double e = glm::dot(ray.direction, w0);
    const double denom = a * c - b * b;
    if (std::abs(denom) < 1e-9 * a * c) return std::nullopt;
    return (b * e - c * d) / denom;
}

} // namespace pf
